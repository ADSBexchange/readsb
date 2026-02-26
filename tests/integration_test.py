#!/usr/bin/env python3
"""
Integration tests for readsb.

Exercises the full pipeline: SBS input -> JSON file output / SBS output / HTTP API.
Requires the readsb binary to be built first (make -j$(nproc)).

Uses only Python 3 stdlib — no external dependencies.
"""

import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from http.client import HTTPConnection
from pathlib import Path

READSB_BIN = Path(__file__).resolve().parent.parent / "readsb"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def get_free_port():
    """Allocate a free TCP port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for_port(port, host="127.0.0.1", timeout=10):
    """Block until *port* is accepting connections (or *timeout* expires)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.1)
    return False


def sbs_msg3(icao, alt=35000, lat=51.5, lon=-0.1, gs=None, track=None):
    """Build an SBS MSG,3 (airborne position) line."""
    d = time.strftime("%Y/%m/%d")
    t = time.strftime("%H:%M:%S.000")
    gs_s = str(gs) if gs is not None else ""
    trk_s = str(track) if track is not None else ""
    return (
        f"MSG,3,1,1,{icao},1,{d},{t},{d},{t},"
        f",{alt},{gs_s},{trk_s},{lat:.5f},{lon:.5f},,,,,,\n"
    )


def sbs_msg1(icao, callsign):
    """Build an SBS MSG,1 (identification / callsign) line."""
    d = time.strftime("%Y/%m/%d")
    t = time.strftime("%H:%M:%S.000")
    return (
        f"MSG,1,1,1,{icao},1,{d},{t},{d},{t},"
        f"{callsign},,,,,,,,,,,\n"
    )


def sbs_msg4(icao, gs=450, track=180, vr=0):
    """Build an SBS MSG,4 (airborne velocity) line."""
    d = time.strftime("%Y/%m/%d")
    t = time.strftime("%H:%M:%S.000")
    return (
        f"MSG,4,1,1,{icao},1,{d},{t},{d},{t},"
        f",,{gs},{track},,,{vr},,,,,\n"
    )


def sbs_msg6(icao, squawk="7000"):
    """Build an SBS MSG,6 (surveillance squawk) line."""
    d = time.strftime("%Y/%m/%d")
    t = time.strftime("%H:%M:%S.000")
    return (
        f"MSG,6,1,1,{icao},1,{d},{t},{d},{t},"
        f",,,,,,,{squawk},,,,\n"
    )


def feed_sbs(sock, lines):
    """Send SBS lines over an already-connected socket."""
    for line in lines:
        sock.sendall(line.encode())


def poll_aircraft_json(json_dir, timeout=8, min_aircraft=1, want_hex=None):
    """Poll aircraft.json until it meets criteria.

    If *want_hex* is given, keep polling until that hex appears in the list
    (regardless of min_aircraft).
    """
    path = Path(json_dir) / "aircraft.json"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            try:
                data = json.loads(path.read_text())
                aircraft = data.get("aircraft", [])
                if want_hex:
                    hexes = {a["hex"] for a in aircraft}
                    if want_hex in hexes:
                        return data
                elif len(aircraft) >= min_aircraft:
                    return data
            except (json.JSONDecodeError, KeyError):
                pass
        time.sleep(0.3)
    # return whatever we have
    if path.exists():
        try:
            return json.loads(path.read_text())
        except Exception:
            pass
    return None


# ---------------------------------------------------------------------------
# Beast / Raw-hex protocol helpers
# ---------------------------------------------------------------------------

# Mode-S CRC polynomial (matching the C implementation in crc.c)
_MODES_GENERATOR_POLY = 0xFFF409
_crc_table = None


def _init_crc_table():
    """Build the 256-entry CRC lookup table (same algorithm as crc.c)."""
    global _crc_table
    if _crc_table is not None:
        return
    _crc_table = [0] * 256
    for i in range(256):
        c = i << 16
        for _ in range(8):
            if c & 0x800000:
                c = (c << 1) ^ _MODES_GENERATOR_POLY
            else:
                c = c << 1
        _crc_table[i] = c & 0x00FFFFFF


def modes_checksum(msg_bytes):
    """Compute the Mode-S CRC over *msg_bytes* (bytes/bytearray).

    Matches modesChecksum() in crc.c.  Returns a 24-bit integer.
    For a valid message the result is 0 (CRC is XORed into the last 3 bytes).
    """
    _init_crc_table()
    n = len(msg_bytes)
    assert n >= 3
    rem = 0
    for i in range(n - 3):
        rem = (_crc_table[msg_bytes[i] ^ ((rem & 0xFF0000) >> 16)] ^ (rem << 8)) & 0xFFFFFF
    rem = rem ^ (msg_bytes[n - 3] << 16) ^ (msg_bytes[n - 2] << 8) ^ msg_bytes[n - 1]
    return rem & 0xFFFFFF


def make_df17_msg(icao_hex, typecode=4, payload_bytes=None):
    """Construct a 14-byte DF17 message with valid CRC.

    *icao_hex* is a 6-char hex string (e.g. "4840D6").
    *typecode* is the 5-bit ADS-B type code (default 4 = identification).
    *payload_bytes* fills bytes 4-10 after the type code nibble; random if None.
    """
    icao = int(icao_hex, 16)
    msg = bytearray(14)
    # Byte 0: DF17 = 10001_XXX (downlink format 17, CA=0)
    msg[0] = 0x8D
    msg[1] = (icao >> 16) & 0xFF
    msg[2] = (icao >> 8) & 0xFF
    msg[3] = icao & 0xFF
    # ME field: 7 bytes (bytes 4-10), first 5 bits = typecode
    msg[4] = (typecode << 3) & 0xFF
    if payload_bytes:
        for i, b in enumerate(payload_bytes[:6]):
            msg[4 + 1 + i] = b
    # Compute CRC over first 11 bytes, embed in last 3
    # Zero out PI field first
    msg[11] = msg[12] = msg[13] = 0
    crc = modes_checksum(msg)
    msg[11] = (crc >> 16) & 0xFF
    msg[12] = (crc >> 8) & 0xFF
    msg[13] = crc & 0xFF
    return bytes(msg)


def beast_escape(data):
    """Escape 0x1A bytes in *data* by doubling them (Beast protocol)."""
    return data.replace(b'\x1a', b'\x1a\x1a')


def beast_frame(msg_bytes, timestamp=0, signal_level=0xA0):
    """Build a complete Beast wire frame for a Mode-S message.

    *msg_bytes* is the raw message (7 or 14 bytes).
    Returns bytes ready to send on a Beast-input TCP connection.
    """
    if len(msg_bytes) == 14:
        msg_type = b'3'  # long message
    elif len(msg_bytes) == 7:
        msg_type = b'2'  # short message
    elif len(msg_bytes) == 2:
        msg_type = b'1'  # Mode A/C
    else:
        raise ValueError(f"Unexpected message length: {len(msg_bytes)}")

    # 6-byte big-endian timestamp + 1-byte signal level
    ts_bytes = struct.pack(">Q", timestamp)[2:]  # take last 6 bytes
    sig = bytes([signal_level])

    body = beast_escape(ts_bytes + sig + msg_bytes)
    return b'\x1a' + msg_type + body


# ---------------------------------------------------------------------------
# CPR encoding helpers for DF17 airborne position messages
# ---------------------------------------------------------------------------

import math

def _nl(lat):
    """Number of longitude zones at a given latitude (NL function)."""
    if abs(lat) >= 87.0:
        return 1
    nz = 15
    cos_lat = math.cos(math.radians(abs(lat)))
    try:
        return int(math.floor(
            2.0 * math.pi / math.acos(
                1.0 - (1.0 - math.cos(math.pi / (2.0 * nz))) / (cos_lat * cos_lat)
            )
        ))
    except (ValueError, ZeroDivisionError):
        return 1


def _cpr_encode(lat, lon, odd):
    """Encode lat/lon to 17-bit CPR values for airborne position.

    Returns (cpr_lat, cpr_lon) as integers in [0, 2^17).
    """
    nz = 15
    dlat = 360.0 / (4 * nz - odd)
    # Normalize latitude into [0, dlat)
    yz = int(math.floor(131072.0 * (lat % dlat) / dlat + 0.5)) & 0x1FFFF
    nl = _nl(lat)
    if odd:
        nl = max(nl - 1, 1)
    else:
        nl = max(nl, 1)
    dlon = 360.0 / nl
    xz = int(math.floor(131072.0 * (lon % dlon) / dlon + 0.5)) & 0x1FFFF
    return (yz, xz)


def _encode_ac12(alt_ft):
    """Encode altitude (feet) into the 12-bit AC12 field with Q-bit.

    Uses 25-ft resolution (Q=1).  altitude = N * 25 - 1000.
    """
    n = (alt_ft + 1000) // 25
    # 12-bit field: bits 11..5 = N[10..4], bit 4 = Q=1, bits 3..0 = N[3..0]
    return ((n & 0x7F0) << 1) | 0x10 | (n & 0x0F)


def make_df17_position(icao_hex, lat, lon, alt_ft, odd, typecode=11):
    """Build a 14-byte DF17 airborne position message with valid CRC.

    Encodes the given lat/lon/alt into CPR format.
    *odd* selects CPR odd (1) or even (0) frame.
    """
    icao = int(icao_hex, 16)
    msg = bytearray(14)
    msg[0] = 0x8D  # DF17, CA=5
    msg[1] = (icao >> 16) & 0xFF
    msg[2] = (icao >> 8) & 0xFF
    msg[3] = icao & 0xFF

    # Build ME field (7 bytes = 56 bits):
    # bits 1-5:  typecode
    # bits 6-7:  surveillance status (00)
    # bit  8:    NIC supplement-B (0)
    # bits 9-20: altitude (12 bits)
    # bit  21:   T flag (0)
    # bit  22:   F flag (odd/even)
    # bits 23-39: CPR latitude (17 bits)
    # bits 40-56: CPR longitude (17 bits)
    ac12 = _encode_ac12(alt_ft)
    cpr_lat, cpr_lon = _cpr_encode(lat, lon, odd)

    # Pack into 56-bit ME field
    # bit positions within the 56-bit ME (1-indexed from MSB per ADS-B spec):
    #   1-5:   typecode    → shift 51
    #   6-7:   SS (0)      → shift 49
    #   8:     NIC-B (0)   → shift 48
    #   9-20:  AC12 (alt)  → shift 36
    #   21:    T flag (0)  → shift 35
    #   22:    F flag (odd) → shift 34
    #   23-39: CPR lat     → shift 17
    #   40-56: CPR lon     → shift 0
    me_bits = (typecode << 51) | (ac12 << 36) | (odd << 34) | (cpr_lat << 17) | cpr_lon
    for i in range(7):
        msg[4 + i] = (me_bits >> (48 - 8 * i)) & 0xFF

    # Compute CRC
    msg[11] = msg[12] = msg[13] = 0
    crc = modes_checksum(msg)
    msg[11] = (crc >> 16) & 0xFF
    msg[12] = (crc >> 8) & 0xFF
    msg[13] = crc & 0xFF
    return bytes(msg)


# ---------------------------------------------------------------------------
# ReadsbInstance — context manager
# ---------------------------------------------------------------------------

class ReadsbInstance:
    """Start / stop a readsb process with unique ports and a temp directory."""

    def __init__(self, extra_args=None, beast_in=False, beast_out=False,
                 raw_in=False, raw_out=False, vrs_out=False,
                 json_out=False, beast_reduce=False):
        self.extra_args = extra_args or []
        self.proc = None
        self.tmpdir = None
        self.sbs_in_port = get_free_port()
        self.sbs_out_port = get_free_port()
        self.api_port = get_free_port()
        self.beast_in_port = get_free_port() if beast_in else 0
        self.beast_out_port = get_free_port() if beast_out else 0
        self.raw_in_port = get_free_port() if raw_in else 0
        self.raw_out_port = get_free_port() if raw_out else 0
        self.vrs_out_port = get_free_port() if vrs_out else 0
        self.json_out_port = get_free_port() if json_out else 0
        self.beast_reduce_port = get_free_port() if beast_reduce else 0
        self._feeder = None

    def __enter__(self):
        self.tmpdir = tempfile.mkdtemp(prefix="readsb-inttest-")
        cmd = [
            str(READSB_BIN),
            "--net-only",
            "--quiet",
            "--write-json", self.tmpdir,
            "--write-json-every", "0.5",
            "--net-sbs-in-port", str(self.sbs_in_port),
            "--net-sbs-port", str(self.sbs_out_port),
            "--net-api-port", str(self.api_port),
            "--auto-exit", "60",
        ]
        if self.beast_in_port:
            cmd += ["--net-bi-port", str(self.beast_in_port)]
        if self.beast_out_port:
            cmd += ["--net-bo-port", str(self.beast_out_port)]
        if self.raw_in_port:
            cmd += ["--net-ri-port", str(self.raw_in_port)]
        if self.raw_out_port:
            cmd += ["--net-ro-port", str(self.raw_out_port)]
        if self.vrs_out_port:
            cmd += ["--net-vrs-port", str(self.vrs_out_port),
                     "--net-vrs-interval", "1"]
        if self.json_out_port:
            cmd += ["--net-json-port", str(self.json_out_port)]
        if self.beast_reduce_port:
            cmd += ["--net-beast-reduce-out-port", str(self.beast_reduce_port),
                     "--net-beast-reduce-interval", "0.25"]
        cmd += self.extra_args
        self.proc = subprocess.Popen(
            cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        # Wait for the primary listen port
        wait_port = self.beast_in_port or self.raw_in_port or self.sbs_in_port
        if not wait_for_port(wait_port):
            self.proc.kill()
            self.proc.wait()
            raise RuntimeError(
                f"readsb failed to listen on port {wait_port}"
            )
        # wait for receiver.json so JSON output is ready
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if (Path(self.tmpdir) / "receiver.json").exists():
                break
            time.sleep(0.2)
        return self

    def feeder(self):
        """Return a persistent TCP connection to the SBS-in port."""
        if self._feeder is None:
            self._feeder = socket.create_connection(
                ("127.0.0.1", self.sbs_in_port), timeout=5
            )
        return self._feeder

    def __exit__(self, *exc):
        if self._feeder:
            try:
                self._feeder.close()
            except Exception:
                pass
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if self.tmpdir:
            shutil.rmtree(self.tmpdir, ignore_errors=True)


# ===================================================================
# A: SBS Input -> JSON File Output
# ===================================================================

class TestSbsToJson(unittest.TestCase):
    """Feed SBS messages, verify aircraft.json and receiver.json."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1",
                        "--json-location-accuracy", "2"]
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _feed(self, lines):
        """Send SBS lines on the persistent feeder connection."""
        feed_sbs(self.inst.feeder(), lines)
        time.sleep(0.3)

    def test_a1_basic_aircraft(self):
        """MSG,3 with position -> aircraft appears with correct fields."""
        self._feed([
            sbs_msg3("406E95", alt=35000, lat=51.5074, lon=-0.1278),
            sbs_msg3("406E95", alt=35000, lat=51.5074, lon=-0.1278),
        ])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="406e95")
        self.assertIsNotNone(data, "aircraft.json never contained 406e95")
        ac = {a["hex"]: a for a in data["aircraft"]}
        self.assertIn("406e95", ac)
        a = ac["406e95"]
        self.assertEqual(a["alt_baro"], 35000)
        self.assertAlmostEqual(a["lat"], 51.5074, places=2)
        self.assertAlmostEqual(a["lon"], -0.1278, places=2)

    def test_a2_multiple_aircraft(self):
        """Three different ICAO addresses all appear."""
        self._feed([
            sbs_msg3("AAAAAA", alt=10000, lat=52.0, lon=0.0),
            sbs_msg3("BBBBBB", alt=20000, lat=53.0, lon=1.0),
            sbs_msg3("CCCCCC", alt=30000, lat=54.0, lon=2.0),
            # repeat for reliability
            sbs_msg3("AAAAAA", alt=10000, lat=52.0, lon=0.0),
            sbs_msg3("BBBBBB", alt=20000, lat=53.0, lon=1.0),
            sbs_msg3("CCCCCC", alt=30000, lat=54.0, lon=2.0),
        ])
        # Previous test already added aircraft, but we need these 3 too
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="cccccc")
        self.assertIsNotNone(data)
        hexes = {a["hex"] for a in data["aircraft"]}
        for h in ("aaaaaa", "bbbbbb", "cccccc"):
            self.assertIn(h, hexes)

    def test_a3_callsign(self):
        """MSG,1 callsign followed by MSG,3 position -> flight field set."""
        self._feed([
            sbs_msg1("DDDDDD", "TEST1234"),
            sbs_msg3("DDDDDD", alt=25000, lat=51.0, lon=-1.0),
            sbs_msg3("DDDDDD", alt=25000, lat=51.0, lon=-1.0),
        ])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="dddddd")
        self.assertIsNotNone(data)
        ac = {a["hex"]: a for a in data["aircraft"]}
        self.assertIn("dddddd", ac)
        self.assertEqual(ac["dddddd"]["flight"].strip(), "TEST1234")

    def test_a4_receiver_json(self):
        """receiver.json has readsb flag and configured lat/lon."""
        path = Path(self.inst.tmpdir) / "receiver.json"
        self.assertTrue(path.exists(), "receiver.json not found")
        data = json.loads(path.read_text())
        self.assertTrue(data.get("readsb"))
        self.assertAlmostEqual(data["lat"], 51.5, places=1)
        self.assertAlmostEqual(data["lon"], -0.1, places=1)

    def test_a5_velocity_fields(self):
        """MSG,4 with ground speed and track -> gs and track in JSON."""
        self._feed([
            sbs_msg3("A50000", alt=28000, lat=51.0, lon=-0.5),
            sbs_msg3("A50000", alt=28000, lat=51.0, lon=-0.5),
            sbs_msg4("A50000", gs=450, track=270, vr=-500),
        ])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="a50000")
        self.assertIsNotNone(data, "aircraft.json never contained a50000")
        ac = {a["hex"]: a for a in data["aircraft"]}
        self.assertIn("a50000", ac)
        a = ac["a50000"]
        self.assertEqual(a.get("gs"), 450)
        self.assertEqual(a.get("track"), 270)

    def test_a6_squawk(self):
        """MSG,6 with squawk -> squawk field appears in JSON."""
        # First, establish the aircraft with position
        self._feed([
            sbs_msg3("A60000", alt=5000, lat=51.2, lon=-0.3),
            sbs_msg3("A60000", alt=5000, lat=51.2, lon=-0.3),
            sbs_msg6("A60000", squawk="4521"),
        ])
        # Squawk requires tentative confirmation: same value must be
        # seen again after 750ms before it's accepted. Use generous
        # delay for slow CI runners.
        time.sleep(1.5)
        self._feed([
            sbs_msg6("A60000", squawk="4521"),
            sbs_msg6("A60000", squawk="4521"),
        ])
        # Poll until squawk field actually appears (not just hex)
        path = Path(self.inst.tmpdir) / "aircraft.json"
        deadline = time.monotonic() + 8
        squawk_val = None
        while time.monotonic() < deadline:
            if path.exists():
                try:
                    data = json.loads(path.read_text())
                    ac = {a["hex"]: a for a in data.get("aircraft", [])}
                    if "a60000" in ac and "squawk" in ac["a60000"]:
                        squawk_val = ac["a60000"]["squawk"]
                        break
                except (json.JSONDecodeError, KeyError):
                    pass
            time.sleep(0.3)
        self.assertEqual(squawk_val, "4521",
                         f"Expected squawk '4521', got {squawk_val!r}")


# ===================================================================
# B: SBS Input -> SBS Output
# ===================================================================

class TestSbsPassthrough(unittest.TestCase):
    """Feed SBS on the input port, read from the output port."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_b1_sbs_passthrough(self):
        """ICAO and position data appear on the SBS output port."""
        # connect to SBS output before feeding data
        out_sock = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out_sock.settimeout(2)

        feeder = self.inst.feeder()
        received = b""
        deadline = time.monotonic() + 10

        # send messages in batches while reading from output
        batch = 0
        while time.monotonic() < deadline:
            # send a batch of messages
            feed_sbs(feeder, [
                sbs_msg3("EEEEEE", alt=15000 + batch, lat=50.0, lon=0.5),
                sbs_msg3("EEEEEE", alt=15000 + batch, lat=50.0, lon=0.5),
            ])
            batch += 1
            time.sleep(0.5)

            # try to read output
            try:
                chunk = out_sock.recv(4096)
                if chunk:
                    received += chunk
                    if b"EEEEEE" in received.upper():
                        break
            except socket.timeout:
                pass
        out_sock.close()

        output = received.decode(errors="replace").upper()
        self.assertIn("EEEEEE", output,
                       "Expected ICAO not found in SBS output")


# ===================================================================
# D: HTTP API
# ===================================================================

class TestApi(unittest.TestCase):
    """Query the readsb HTTP API."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_d1_all_query(self):
        """GET /?all returns JSON containing the fed aircraft."""
        feed_sbs(self.inst.feeder(), [
            sbs_msg3("FF0011", alt=12000, lat=48.0, lon=2.0),
            sbs_msg3("FF0011", alt=12000, lat=48.0, lon=2.0),
        ])
        # wait for readsb to process and update the API
        time.sleep(1.5)

        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", "/?all")
        resp = conn.getresponse()
        self.assertEqual(resp.status, 200)
        data = json.loads(resp.read())
        conn.close()

        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("ff0011", hexes)


# ===================================================================
# E: Lifecycle
# ===================================================================

class TestShutdown(unittest.TestCase):
    """Process lifecycle behaviour."""

    def test_e1_clean_sigterm(self):
        """SIGTERM -> exit code 0 within 5 seconds."""
        inst = ReadsbInstance()
        inst.__enter__()
        try:
            self.assertIsNone(inst.proc.poll(), "process not running")
            inst.proc.send_signal(signal.SIGTERM)
            try:
                ret = inst.proc.wait(timeout=5)
                # Mark process as terminated so __exit__ doesn't try again
                inst.proc = None
                self.assertEqual(ret, 0, f"exit code {ret}, expected 0")
            except subprocess.TimeoutExpired:
                inst.proc.kill()
                self.fail("readsb did not exit within 5 s of SIGTERM")
        finally:
            if inst.tmpdir:
                shutil.rmtree(inst.tmpdir, ignore_errors=True)


# ===================================================================
# C: UAV / Drone (AX-688)
# ===================================================================

class TestUav(unittest.TestCase):
    """Feed $-prefixed UAV addresses with --enable-uav."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(extra_args=[
            "--enable-uav", "--lat", "51.5", "--lon", "-0.1",
        ])
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _feed(self, lines):
        """Send SBS lines on the persistent feeder connection."""
        feed_sbs(self.inst.feeder(), lines)
        time.sleep(0.3)

    def test_c1_uav_in_json(self):
        """$000001 appears in aircraft.json with --enable-uav."""
        self._feed([
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
        ])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="$000001")
        self.assertIsNotNone(data)
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("$000001", hexes)

    def test_c2_uav_sbs_output(self):
        """UAV hex appears on SBS output port."""
        out_sock = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out_sock.settimeout(2)

        feeder = self.inst.feeder()
        received = b""
        deadline = time.monotonic() + 10

        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("$000001", alt=1500 + batch, lat=51.5, lon=-0.1),
                sbs_msg3("$000001", alt=1500 + batch, lat=51.5, lon=-0.1),
            ])
            batch += 1
            time.sleep(0.5)

            try:
                chunk = out_sock.recv(4096)
                if chunk:
                    received += chunk
                    if b"$000001" in received.upper():
                        break
            except socket.timeout:
                pass
        out_sock.close()
        self.assertIn(b"$000001", received)

    def test_c4_uav_category(self):
        """UAV has category B6 in JSON."""
        self._feed([
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
        ])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="$000001")
        self.assertIsNotNone(data)
        ac = {a["hex"]: a for a in data["aircraft"]}
        self.assertIn("$000001", ac)
        self.assertEqual(ac["$000001"].get("category"), "B6")

    def test_c5_mixed_icao_and_uav(self):
        """Both a regular ICAO aircraft and a UAV appear together."""
        self._feed([
            sbs_msg3("ABCDEF", alt=35000, lat=51.5, lon=-0.1),
            sbs_msg3("ABCDEF", alt=35000, lat=51.5, lon=-0.1),
            sbs_msg3("$000002", alt=500, lat=51.51, lon=-0.11),
            sbs_msg3("$000002", alt=500, lat=51.51, lon=-0.11),
        ])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="$000002")
        self.assertIsNotNone(data)
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("abcdef", hexes, "ICAO aircraft missing")
        self.assertIn("$000002", hexes, "UAV aircraft missing")

    def test_c6_uav_callsign(self):
        """MSG,1 callsign for a UAV address, then MSG,3 position."""
        self._feed([
            sbs_msg1("$000003", "DRN00003"),
            sbs_msg3("$000003", alt=800, lat=51.48, lon=-0.05),
            sbs_msg3("$000003", alt=800, lat=51.48, lon=-0.05),
        ])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="$000003")
        self.assertIsNotNone(data)
        ac = {a["hex"]: a for a in data["aircraft"]}
        self.assertIn("$000003", ac)
        self.assertEqual(ac["$000003"]["flight"].strip(), "DRN00003")


# ===================================================================
# D2/D3: UAV API tests
# ===================================================================

class TestUavApi(unittest.TestCase):
    """Test HTTP API with UAV aircraft."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(extra_args=[
            "--enable-uav", "--lat", "51.5", "--lon", "-0.1",
        ])
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_d2_api_find_hex_uav(self):
        """GET /?find_hex=$000001 returns the UAV aircraft."""
        feed_sbs(self.inst.feeder(), [
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
        ])
        time.sleep(1.5)

        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", "/?find_hex=$000001")
        resp = conn.getresponse()
        self.assertEqual(resp.status, 200)
        data = json.loads(resp.read())
        conn.close()

        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("$000001", hexes)

    def test_d3_api_all_includes_uav(self):
        """GET /?all returns both ICAO and UAV aircraft."""
        feed_sbs(self.inst.feeder(), [
            sbs_msg3("FF0022", alt=20000, lat=52.0, lon=0.0),
            sbs_msg3("FF0022", alt=20000, lat=52.0, lon=0.0),
            sbs_msg3("$000004", alt=600, lat=52.01, lon=0.01),
            sbs_msg3("$000004", alt=600, lat=52.01, lon=0.01),
        ])
        time.sleep(1.5)

        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", "/?all")
        resp = conn.getresponse()
        self.assertEqual(resp.status, 200)
        data = json.loads(resp.read())
        conn.close()

        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("ff0022", hexes, "ICAO aircraft missing from API")
        self.assertIn("$000004", hexes, "UAV aircraft missing from API")


class TestUavRejected(unittest.TestCase):
    """Without --enable-uav, UAV addresses must be rejected."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()  # no --enable-uav
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_c3_uav_rejected(self):
        """$000001 does NOT appear without --enable-uav."""
        feed_sbs(self.inst.feeder(), [
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
            sbs_msg3("$000001", alt=1500, lat=51.5, lon=-0.1),
        ])
        time.sleep(2)
        path = Path(self.inst.tmpdir) / "aircraft.json"
        if path.exists():
            data = json.loads(path.read_text())
            hexes = {a["hex"] for a in data.get("aircraft", [])}
            self.assertNotIn("$000001", hexes)


# ===================================================================
# F: Beast Binary Input
# ===================================================================

class TestBeastInput(unittest.TestCase):
    """Feed Beast binary data on --net-bi-port, verify JSON output."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(beast_in=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _beast_feeder(self):
        """Return a TCP connection to the Beast-in port."""
        return socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

    def test_f1_beast_df17(self):
        """DF17 via Beast binary -> aircraft appears in JSON."""
        # Known-good DF17 message (ICAO 4840D6, identification)
        msg = bytes.fromhex("8D4840D6202CC371C32CE0576098")
        frame = beast_frame(msg, timestamp=0x000102030405)
        sock = self._beast_feeder()
        # Send multiple times for reliable detection
        for _ in range(5):
            sock.sendall(frame)
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="4840d6")
        self.assertIsNotNone(data, "aircraft.json never contained 4840d6")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("4840d6", hexes)

    def test_f2_beast_escape_handling(self):
        """Beast frame with 0x1A in timestamp is handled correctly."""
        # Construct a DF17 with a known ICAO, use timestamp containing 0x1A
        msg = make_df17_msg("1A1A1A", typecode=4, payload_bytes=b'\x20\x30\x40\x50\x60\x70')
        # Timestamp intentionally contains 0x1A bytes
        frame = beast_frame(msg, timestamp=0x001A001A001A)
        sock = self._beast_feeder()
        for _ in range(5):
            sock.sendall(frame)
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="1a1a1a")
        self.assertIsNotNone(data, "aircraft.json never contained 1a1a1a")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("1a1a1a", hexes)

    def test_f3_beast_no_crash_on_garbage(self):
        """Random bytes on Beast-in don't crash readsb."""
        sock = self._beast_feeder()
        # Send garbage data
        garbage = bytes(range(256)) * 4
        sock.sendall(garbage)
        sock.close()
        time.sleep(1)
        # Process should still be running
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed on garbage Beast input")


# ===================================================================
# G: Raw Hex Input
# ===================================================================

class TestRawHexInput(unittest.TestCase):
    """Feed raw hex messages on --net-ri-port, verify JSON output."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(raw_in=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _raw_feeder(self):
        """Return a TCP connection to the raw-in port."""
        return socket.create_connection(
            ("127.0.0.1", self.inst.raw_in_port), timeout=5
        )

    def test_g1_raw_hex_df17(self):
        """DF17 as *HEXHEX...;\n on raw-in -> aircraft in JSON."""
        # Known-good DF17 (ICAO 4840D6)
        hex_msg = "8D4840D6202CC371C32CE0576098"
        raw_line = f"*{hex_msg};\n"
        sock = self._raw_feeder()
        for _ in range(5):
            sock.sendall(raw_line.encode())
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="4840d6")
        self.assertIsNotNone(data, "aircraft.json never contained 4840d6")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("4840d6", hexes)

    def test_g2_raw_hex_at_prefix(self):
        """@-prefixed raw hex (with 12-char timestamp) is accepted."""
        hex_msg = "8D40621D58C382D690C8AC2863A7"
        # @ + 12-char hex timestamp + hex message + ;
        raw_line = f"@000000000001{hex_msg};\n"
        sock = self._raw_feeder()
        for _ in range(5):
            sock.sendall(raw_line.encode())
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="40621d")
        self.assertIsNotNone(data, "aircraft.json never contained 40621d")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("40621d", hexes)

    def test_g3_raw_hex_malformed_no_crash(self):
        """Malformed hex input doesn't crash readsb."""
        sock = self._raw_feeder()
        bad_lines = [
            b"*;\n",               # empty message
            b"*ZZZZZZ;\n",         # non-hex characters
            b"*8D;\n",             # too short
            b"*8D4840D6202C;\n",   # truncated DF17
            b"garbage\n",          # no framing at all
            b"*" + b"FF" * 100 + b";\n",  # way too long
        ]
        for line in bad_lines:
            sock.sendall(line)
            time.sleep(0.1)
        sock.close()
        time.sleep(1)
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed on malformed raw hex input")

    def test_g4_raw_hex_short_message(self):
        """7-byte (56-bit) Mode-S message via raw hex doesn't crash."""
        # Build a DF11 (all-call reply): 5D + ICAO(3 bytes) + PI(3 bytes)
        msg = bytearray(7)
        msg[0] = 0x5D
        msg[1] = 0xAB
        msg[2] = 0xCD
        msg[3] = 0xEF
        # Compute CRC and embed
        crc = modes_checksum(msg)
        msg[4] = (crc >> 16) & 0xFF
        msg[5] = (crc >> 8) & 0xFF
        msg[6] = crc & 0xFF
        hex_msg = msg.hex().upper()
        raw_line = f"*{hex_msg};\n"
        sock = self._raw_feeder()
        for _ in range(3):
            sock.sendall(raw_line.encode())
            time.sleep(0.2)
        sock.close()
        time.sleep(1)
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed on short Mode-S message")


# ===================================================================
# H: Multi-Output Format
# ===================================================================

class TestMultiOutput(unittest.TestCase):
    """Feed Beast input, verify data on Beast-out, raw-out, and SBS-out."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            beast_in=True, beast_out=True, raw_out=True,
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_h1_cross_format(self):
        """Beast input appears on Beast-out, raw-out, and SBS-out."""
        # Connect to all output ports first
        beast_out = socket.create_connection(
            ("127.0.0.1", self.inst.beast_out_port), timeout=5
        )
        beast_out.settimeout(2)
        raw_out = socket.create_connection(
            ("127.0.0.1", self.inst.raw_out_port), timeout=5
        )
        raw_out.settimeout(2)
        sbs_out = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        sbs_out.settimeout(2)

        # Feed a DF17 via Beast
        msg = bytes.fromhex("8D4840D6202CC371C32CE0576098")
        frame = beast_frame(msg, timestamp=0x000102030405)

        beast_in = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        beast_rx = b""
        raw_rx = b""
        sbs_rx = b""

        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            beast_in.sendall(frame)
            batch += 1
            time.sleep(0.5)

            for sock, buf_name in [(beast_out, "beast"), (raw_out, "raw"), (sbs_out, "sbs")]:
                try:
                    chunk = sock.recv(4096)
                    if buf_name == "beast":
                        beast_rx += chunk
                    elif buf_name == "raw":
                        raw_rx += chunk
                    else:
                        sbs_rx += chunk
                except socket.timeout:
                    pass

            # Check if we have data on all three
            if beast_rx and raw_rx and sbs_rx:
                break

        beast_in.close()
        beast_out.close()
        raw_out.close()
        sbs_out.close()

        # Beast output should contain 0x1A framing
        self.assertIn(b'\x1a', beast_rx,
                       "No Beast framing in Beast output")

        # Raw output should contain the hex of our ICAO
        raw_text = raw_rx.decode(errors="replace").upper()
        self.assertIn("4840D6", raw_text,
                       "ICAO not found in raw output")

        # SBS output should contain the ICAO
        sbs_text = sbs_rx.decode(errors="replace").upper()
        self.assertIn("4840D6", sbs_text,
                       "ICAO not found in SBS output")


# ===================================================================
# I: Stats JSON
# ===================================================================

class TestStatsJson(unittest.TestCase):
    """Verify stats.json is written with expected structure."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_i1_stats_json(self):
        """stats.json has expected top-level fields after feeding SBS data."""
        # Feed data over several seconds; stats.json is only written every ~10s
        feeder = self.inst.feeder()
        stats_path = Path(self.inst.tmpdir) / "stats.json"
        deadline = time.monotonic() + 20
        data = None
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("112233", alt=20000, lat=51.5, lon=-0.1),
                sbs_msg3("112233", alt=20000, lat=51.5, lon=-0.1),
            ])
            if stats_path.exists():
                try:
                    data = json.loads(stats_path.read_text())
                    if data.get("total", {}).get("messages", 0) > 0:
                        break
                except (json.JSONDecodeError, KeyError):
                    pass
            time.sleep(1)

        self.assertIsNotNone(data, "stats.json not found or empty")

        # Top-level "now" field
        self.assertIn("now", data)
        self.assertIsInstance(data["now"], (int, float))

        # "total" section with message count
        self.assertIn("total", data)
        total = data["total"]
        self.assertGreater(total.get("messages", 0), 0,
                           "Expected total messages > 0")

        # "total" -> "remote" section (we're in --net-only mode)
        self.assertIn("remote", total)
        remote = total["remote"]

        # SBS = basestation format counter
        self.assertGreater(remote.get("basestation", 0), 0,
                           "Expected basestation messages > 0")

        # Track count
        self.assertIn("tracks", total)
        self.assertGreaterEqual(total["tracks"].get("all", 0), 1,
                                "Expected at least 1 track")


# ===================================================================
# J: Aircraft Staleness
# ===================================================================

class TestAircraftStaleness(unittest.TestCase):
    """Verify the 'seen' field increases after messages stop."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_j1_seen_increases(self):
        """After messages stop, the 'seen' field grows past 2 seconds."""
        feeder = self.inst.feeder()
        # Feed data to establish the aircraft
        for _ in range(4):
            feed_sbs(feeder, [
                sbs_msg3("AABB00", alt=15000, lat=51.5, lon=-0.1),
                sbs_msg3("AABB00", alt=15000, lat=51.5, lon=-0.1),
            ])
            time.sleep(0.3)

        # Confirm aircraft appears
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="aabb00")
        self.assertIsNotNone(data, "aircraft.json never contained aabb00")

        # Stop feeding and wait for staleness to accumulate
        time.sleep(4)

        # Read aircraft.json again
        path = Path(self.inst.tmpdir) / "aircraft.json"
        data = json.loads(path.read_text())
        ac = {a["hex"]: a for a in data.get("aircraft", [])}
        self.assertIn("aabb00", ac, "aabb00 disappeared from aircraft.json")
        seen = ac["aabb00"].get("seen")
        self.assertIsNotNone(seen, "'seen' field missing")
        self.assertGreater(seen, 2.0,
                           f"Expected seen > 2.0s, got {seen}")


# ===================================================================
# K: Callsign API Queries
# ===================================================================

class TestApiFindCallsign(unittest.TestCase):
    """Test /?find_callsign API endpoint."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _feed(self, lines):
        feed_sbs(self.inst.feeder(), lines)
        time.sleep(0.3)

    def _api_get(self, path):
        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", path)
        resp = conn.getresponse()
        data = json.loads(resp.read())
        conn.close()
        return data

    def test_k1_find_single_callsign(self):
        """/?find_callsign=BAW123 returns exactly that aircraft."""
        self._feed([
            sbs_msg1("A10001", "BAW123"),
            sbs_msg3("A10001", alt=35000, lat=51.5, lon=-0.1),
            sbs_msg3("A10001", alt=35000, lat=51.5, lon=-0.1),
            sbs_msg1("A10002", "DLH456"),
            sbs_msg3("A10002", alt=30000, lat=52.0, lon=0.0),
            sbs_msg3("A10002", alt=30000, lat=52.0, lon=0.0),
            sbs_msg1("A10003", "AFR789"),
            sbs_msg3("A10003", alt=25000, lat=53.0, lon=1.0),
            sbs_msg3("A10003", alt=25000, lat=53.0, lon=1.0),
        ])
        time.sleep(1.5)
        data = self._api_get("/?find_callsign=BAW123")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("a10001", hexes)

    def test_k2_find_multiple_callsigns(self):
        """/?find_callsign=BAW123,DLH456 returns both."""
        time.sleep(0.5)
        data = self._api_get("/?find_callsign=BAW123,DLH456")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("a10001", hexes)
        self.assertIn("a10002", hexes)

    def test_k3_find_nonexistent_callsign(self):
        """/?find_callsign=ZZZZZZ returns empty result."""
        data = self._api_get("/?find_callsign=ZZZZZZ")
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 0)

    def test_k4_no_partial_match(self):
        """/?find_callsign=BAW returns no partial match."""
        data = self._api_get("/?find_callsign=BAW")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertNotIn("a10001", hexes)


# ===================================================================
# L: Hex Lookup API Queries
# ===================================================================

class TestApiHexList(unittest.TestCase):
    """Test /?find_hex and /?hexlist API endpoints."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _feed(self, lines):
        feed_sbs(self.inst.feeder(), lines)
        time.sleep(0.3)

    def _api_get(self, path):
        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", path)
        resp = conn.getresponse()
        data = json.loads(resp.read())
        conn.close()
        return data

    def test_l1_find_single_hex(self):
        """/?find_hex=A00001 returns single aircraft."""
        self._feed([
            sbs_msg3("A00001", alt=20000, lat=51.5, lon=-0.1),
            sbs_msg3("A00001", alt=20000, lat=51.5, lon=-0.1),
            sbs_msg3("A00002", alt=25000, lat=52.0, lon=0.0),
            sbs_msg3("A00002", alt=25000, lat=52.0, lon=0.0),
            sbs_msg3("A00003", alt=30000, lat=53.0, lon=1.0),
            sbs_msg3("A00003", alt=30000, lat=53.0, lon=1.0),
        ])
        time.sleep(1.5)
        data = self._api_get("/?find_hex=A00001")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("a00001", hexes)

    def test_l2_find_multiple_hex(self):
        """/?find_hex=A00001,A00002 returns both."""
        data = self._api_get("/?find_hex=A00001,A00002")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("a00001", hexes)
        self.assertIn("a00002", hexes)

    def test_l3_hexlist_alias(self):
        """/?hexlist=A00001 is an alias for find_hex."""
        data = self._api_get("/?hexlist=A00001")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("a00001", hexes)

    def test_l4_find_nonexistent_hex(self):
        """/?find_hex=FFFFFF returns empty result."""
        data = self._api_get("/?find_hex=FFFFFF")
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 0)


# ===================================================================
# M: Connection Resilience
# ===================================================================

class TestConnectionResilience(unittest.TestCase):
    """Test input/output reconnection behaviour."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_m1_sbs_in_reconnect(self):
        """Disconnect and reconnect SBS-in, new aircraft appears."""
        # First connection: feed aircraft
        sock1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        feed_sbs(sock1, [
            sbs_msg3("B00001", alt=20000, lat=51.5, lon=-0.1),
            sbs_msg3("B00001", alt=20000, lat=51.5, lon=-0.1),
        ])
        time.sleep(0.5)
        sock1.close()

        # Second connection: feed different aircraft
        time.sleep(0.5)
        sock2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        feed_sbs(sock2, [
            sbs_msg3("B00002", alt=25000, lat=52.0, lon=0.0),
            sbs_msg3("B00002", alt=25000, lat=52.0, lon=0.0),
        ])
        time.sleep(0.5)
        sock2.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="b00002")
        self.assertIsNotNone(data, "B00002 did not appear after reconnect")

    def test_m2_sbs_out_reconnect(self):
        """Disconnect and reconnect SBS-out, still receiving output."""
        # First output connection
        out1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out1.settimeout(1)
        out1.close()
        time.sleep(0.5)

        # Second output connection
        out2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out2.settimeout(2)

        # Feed data
        feeder = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        received = b""
        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("B00003", alt=30000 + batch, lat=51.5, lon=-0.1),
                sbs_msg3("B00003", alt=30000 + batch, lat=51.5, lon=-0.1),
            ])
            batch += 1
            time.sleep(0.5)
            try:
                chunk = out2.recv(4096)
                if chunk:
                    received += chunk
                    if b"B00003" in received.upper():
                        break
            except socket.timeout:
                pass
        feeder.close()
        out2.close()
        self.assertIn(b"B00003", received.upper(),
                       "ICAO not found on reconnected SBS output")

    def test_m3_process_survives_disconnect(self):
        """After all clients disconnect, process still alive."""
        # Connect and disconnect a client
        sock = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        sock.close()
        time.sleep(1)
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb died after client disconnect")


# ===================================================================
# N: Multiple Simultaneous Clients
# ===================================================================

class TestMultipleClients(unittest.TestCase):
    """Test multiple simultaneous readers on SBS output."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_n1_two_readers(self):
        """Two SBS-out sockets both receive data."""
        out1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out1.settimeout(2)
        out2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out2.settimeout(2)

        feeder = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )

        rx1 = b""
        rx2 = b""
        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("C00001", alt=20000 + batch, lat=51.5, lon=-0.1),
                sbs_msg3("C00001", alt=20000 + batch, lat=51.5, lon=-0.1),
            ])
            batch += 1
            time.sleep(0.5)
            for sock, buf_name in [(out1, "rx1"), (out2, "rx2")]:
                try:
                    chunk = sock.recv(4096)
                    if buf_name == "rx1":
                        rx1 += chunk
                    else:
                        rx2 += chunk
                except socket.timeout:
                    pass
            if b"C00001" in rx1.upper() and b"C00001" in rx2.upper():
                break

        feeder.close()
        out1.close()
        out2.close()

        self.assertIn(b"C00001", rx1.upper(), "Reader 1 missing data")
        self.assertIn(b"C00001", rx2.upper(), "Reader 2 missing data")

    def test_n2_disconnect_one_reader(self):
        """Disconnect one reader, remaining one still works."""
        out1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out1.settimeout(2)
        out2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out2.settimeout(2)

        # Disconnect one
        out1.close()
        time.sleep(0.5)

        feeder = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )

        rx2 = b""
        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("C00002", alt=25000 + batch, lat=52.0, lon=0.0),
                sbs_msg3("C00002", alt=25000 + batch, lat=52.0, lon=0.0),
            ])
            batch += 1
            time.sleep(0.5)
            try:
                chunk = out2.recv(4096)
                if chunk:
                    rx2 += chunk
                    if b"C00002" in rx2.upper():
                        break
            except socket.timeout:
                pass

        feeder.close()
        out2.close()

        self.assertIn(b"C00002", rx2.upper(),
                       "Remaining reader not receiving after other disconnect")
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed after partial disconnect")


# ===================================================================
# O: VRS JSON Output
# ===================================================================

class TestVrsJsonOutput(unittest.TestCase):
    """Test VRS format output on --net-vrs-port."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(vrs_out=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_o1_vrs_format(self):
        """VRS output has acList structure with correct fields."""
        # Connect to VRS output
        vrs_sock = socket.create_connection(
            ("127.0.0.1", self.inst.vrs_out_port), timeout=5
        )
        vrs_sock.settimeout(3)

        feeder = self.inst.feeder()

        received = b""
        deadline = time.monotonic() + 15
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg1("D00001", "TST999"),
                sbs_msg3("D00001", alt=35000, lat=51.5, lon=-0.1, gs=450, track=180),
                sbs_msg3("D00001", alt=35000, lat=51.5, lon=-0.1, gs=450, track=180),
                sbs_msg4("D00001", gs=450, track=180),
                sbs_msg6("D00001", squawk="7000"),
            ])
            batch += 1
            time.sleep(1.5)
            try:
                chunk = vrs_sock.recv(8192)
                if chunk:
                    received += chunk
                    if b"acList" in received:
                        break
            except socket.timeout:
                pass

        vrs_sock.close()
        text = received.decode(errors="replace")

        # VRS sends complete JSON objects; find one with acList
        self.assertIn("acList", text, "VRS output missing acList")

        # Try to parse the JSON
        # VRS output may have multiple concatenated objects, find the first valid one
        vrs_data = None
        for line in text.split("\n"):
            line = line.strip()
            if not line:
                continue
            try:
                parsed = json.loads(line)
                if "acList" in parsed:
                    vrs_data = parsed
                    break
            except json.JSONDecodeError:
                continue

        # If no newline-delimited JSON, try the whole blob
        if vrs_data is None:
            try:
                vrs_data = json.loads(text)
            except json.JSONDecodeError:
                pass

        if vrs_data is not None and vrs_data.get("acList"):
            ac = vrs_data["acList"][0]
            # Check for VRS field names
            self.assertIn("Icao", ac)
            self.assertIn("Lat", ac)
            self.assertIn("Long", ac)


# ===================================================================
# P: Net Connector (outbound client mode)
# ===================================================================

class TestNetConnector(unittest.TestCase):
    """Test --net-connector outbound connection mode."""

    def test_p1_net_connector_sbs_out(self):
        """Readsb connects to a test server with sbs_out protocol."""
        # Start a TCP server
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server_port = server.getsockname()[1]
        server.listen(1)
        server.settimeout(15)

        # Start readsb with --net-connector pointing to our server
        inst = ReadsbInstance(
            extra_args=[
                "--net-connector",
                f"127.0.0.1,{server_port},sbs_out",
            ]
        )
        inst.__enter__()
        try:
            # Accept the incoming connection from readsb
            try:
                client, _ = server.accept()
                client.settimeout(3)
            except socket.timeout:
                self.fail("readsb did not connect to test server")

            # Feed data via SBS input
            feeder = inst.feeder()
            received = b""
            deadline = time.monotonic() + 10
            batch = 0
            while time.monotonic() < deadline:
                feed_sbs(feeder, [
                    sbs_msg3("E00001", alt=20000 + batch, lat=51.5, lon=-0.1),
                    sbs_msg3("E00001", alt=20000 + batch, lat=51.5, lon=-0.1),
                ])
                batch += 1
                time.sleep(0.5)
                try:
                    chunk = client.recv(4096)
                    if chunk:
                        received += chunk
                        if b"E00001" in received.upper():
                            break
                except socket.timeout:
                    pass

            client.close()
            self.assertIn(b"E00001", received.upper(),
                           "ICAO not found in net-connector SBS output")
        finally:
            inst.__exit__(None, None, None)
            server.close()


# ===================================================================
# Q: JSON Network Output
# ===================================================================

class TestJsonNetworkOutput(unittest.TestCase):
    """Test --net-json-port TCP JSON output.

    JSON port output only triggers for non-SBS-input messages (Beast/raw),
    so this test uses Beast input with DF17 airborne position messages.
    CPR decoding requires both even and odd frames to determine position.
    """

    @classmethod
    def setUpClass(cls):
        # json_reliable=1 lowers the position reliability threshold so
        # the first successfully decoded position triggers output.
        cls.inst = ReadsbInstance(
            beast_in=True, json_out=True,
            extra_args=["--json-reliable", "1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_q1_json_tcp_output(self):
        """JSON-over-TCP output has hex, lat, lon fields."""
        json_sock = socket.create_connection(
            ("127.0.0.1", self.inst.json_out_port), timeout=5
        )
        json_sock.settimeout(3)

        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        received = b""
        deadline = time.monotonic() + 15
        ts = 1000  # Beast timestamp counter
        batch = 0
        while time.monotonic() < deadline:
            # Vary position slightly each iteration to avoid duplicate detection
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            alt = 35000

            # Send both even and odd CPR frames (needed for global decode)
            even_msg = make_df17_position("F00001", lat, lon, alt, odd=0)
            odd_msg = make_df17_position("F00001", lat, lon, alt, odd=1)

            beast_sock.sendall(beast_frame(even_msg, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd_msg, timestamp=ts))
            ts += 500

            batch += 1
            time.sleep(0.5)
            try:
                chunk = json_sock.recv(8192)
                if chunk:
                    received += chunk
                    if b"f00001" in received:
                        break
            except socket.timeout:
                pass

        beast_sock.close()
        json_sock.close()
        text = received.decode(errors="replace")

        # Each line should be valid JSON
        found_aircraft = False
        for line in text.strip().split("\n"):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                if obj.get("hex") == "f00001":
                    found_aircraft = True
                    self.assertIn("lat", obj)
                    self.assertIn("lon", obj)
                    break
            except json.JSONDecodeError:
                continue

        self.assertTrue(found_aircraft,
                        "Aircraft f00001 not found in JSON TCP output")


# ===================================================================
# R: Beast Reduce Output
# ===================================================================

class TestBeastReduceOutput(unittest.TestCase):
    """Test Beast Reduce output format and deduplication."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            beast_in=True, beast_reduce=True,
            extra_args=["--json-reliable", "1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_r1_beast_reduce_format(self):
        """Beast reduce output has correct Beast framing."""
        reduce_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_reduce_port), timeout=5
        )
        reduce_sock.settimeout(5)

        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        # Feed multiple DF17 position messages
        received = b""
        ts = 1000
        for batch in range(20):
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            even_msg = make_df17_position("A00001", lat, lon, 35000, odd=0)
            odd_msg = make_df17_position("A00001", lat, lon, 35000, odd=1)
            beast_sock.sendall(beast_frame(even_msg, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd_msg, timestamp=ts))
            ts += 500
            time.sleep(0.15)

            try:
                chunk = reduce_sock.recv(8192)
                if chunk:
                    received += chunk
            except socket.timeout:
                pass

        # Give extra time for output
        time.sleep(1)
        try:
            chunk = reduce_sock.recv(8192)
            if chunk:
                received += chunk
        except socket.timeout:
            pass

        beast_sock.close()
        reduce_sock.close()

        self.assertTrue(len(received) > 0,
                        "No data received from beast_reduce port")

        # Verify Beast framing: data contains 0x1A + type byte
        self.assertIn(b'\x1a', received,
                      "Beast escape byte (0x1a) not found in output")

        # Parse Beast frames: find 0x1A followed by type '3' (long message)
        frames_found = 0
        i = 0
        while i < len(received) - 1:
            if received[i] == 0x1A:
                if i + 1 < len(received) and received[i + 1] == 0x1A:
                    # Escaped 0x1A, skip
                    i += 2
                    continue
                type_byte = received[i + 1:i + 2]
                if type_byte in (b'1', b'2', b'3'):
                    frames_found += 1
                i += 2
            else:
                i += 1

        self.assertGreater(frames_found, 0,
                           "No valid Beast frames found in reduce output")

    def test_r2_beast_reduce_dedup(self):
        """Beast reduce deduplicates repeated messages."""
        reduce_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_reduce_port), timeout=5
        )
        reduce_sock.settimeout(3)

        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        # Feed the SAME message many times rapidly
        msg = make_df17_position("B00001", 52.0, 1.0, 30000, odd=0)
        ts = 10000
        for _ in range(50):
            beast_sock.sendall(beast_frame(msg, timestamp=ts))
            ts += 1  # minimal time increment

        time.sleep(2)

        received = b""
        try:
            while True:
                chunk = reduce_sock.recv(8192)
                if not chunk:
                    break
                received += chunk
        except socket.timeout:
            pass

        beast_sock.close()
        reduce_sock.close()

        # Count output frames (0x1A followed by non-0x1A)
        output_frames = 0
        i = 0
        while i < len(received) - 1:
            if received[i] == 0x1A:
                if i + 1 < len(received) and received[i + 1] == 0x1A:
                    i += 2
                    continue
                if received[i + 1:i + 2] in (b'1', b'2', b'3'):
                    output_frames += 1
                i += 2
            else:
                i += 1

        # The "reduce" behavior should output significantly fewer frames
        # than the 50 we sent. It might output 0 if the message is fully
        # deduplicated, or a small number. In any case, it should be < 50.
        self.assertLess(output_frames, 50,
                        f"Beast reduce did not deduplicate: got {output_frames} frames for 50 inputs")


# ===================================================================
# S: Beast Output Format
# ===================================================================

class TestBeastOutputFormat(unittest.TestCase):
    """Verify Beast output wire format and escaping."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            beast_in=True, beast_out=True,
            extra_args=["--json-reliable", "1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _collect_beast_output(self, input_msgs, timeout=8):
        """Feed Beast input and collect Beast output."""
        out_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_out_port), timeout=5
        )
        out_sock.settimeout(3)

        in_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        for raw_frame in input_msgs:
            in_sock.sendall(raw_frame)
            time.sleep(0.05)

        # Collect output
        received = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = out_sock.recv(8192)
                if chunk:
                    received += chunk
                else:
                    break
            except socket.timeout:
                if received:
                    break

        in_sock.close()
        out_sock.close()
        return received

    @staticmethod
    def _unescape_beast(data):
        """Remove Beast escaping (doubled 0x1A -> single 0x1A)."""
        result = bytearray()
        i = 0
        while i < len(data):
            if data[i] == 0x1A and i + 1 < len(data) and data[i + 1] == 0x1A:
                result.append(0x1A)
                i += 2
            else:
                result.append(data[i])
                i += 1
        return bytes(result)

    @staticmethod
    def _parse_beast_frames(data):
        """Parse Beast frames from raw data. Returns list of (type, payload)."""
        frames = []
        i = 0
        while i < len(data) - 1:
            if data[i] == 0x1A and i + 1 < len(data):
                if data[i + 1] == 0x1A:
                    i += 2
                    continue
                frame_type = data[i + 1]
                # Determine expected payload length (6 ts + 1 signal + msg)
                if frame_type == ord('3'):
                    msg_len = 14
                elif frame_type == ord('2'):
                    msg_len = 7
                elif frame_type == ord('1'):
                    msg_len = 2
                else:
                    i += 2
                    continue

                # Extract payload (may contain escaped bytes)
                payload = bytearray()
                j = i + 2
                while j < len(data) and len(payload) < 6 + 1 + msg_len:
                    if data[j] == 0x1A and j + 1 < len(data):
                        if data[j + 1] == 0x1A:
                            payload.append(0x1A)
                            j += 2
                            continue
                        else:
                            break  # Start of next frame
                    payload.append(data[j])
                    j += 1

                if len(payload) == 6 + 1 + msg_len:
                    msg = bytes(payload[7:])  # Skip timestamp + signal
                    frames.append((chr(frame_type), msg))
                i = j
            else:
                i += 1
        return frames

    def test_s1_beast_passthrough(self):
        """DF17 fed via Beast input appears in Beast output."""
        msg = make_df17_position("C00001", 51.5, -0.1, 35000, odd=0)
        ts = 50000

        # Send both even and odd to help readsb track
        msg_even = make_df17_position("C00001", 51.5, -0.1, 35000, odd=0)
        msg_odd = make_df17_position("C00001", 51.5, -0.1, 35000, odd=1)

        input_frames = []
        for batch in range(10):
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            even = make_df17_position("C00001", lat, lon, 35000, odd=0)
            odd = make_df17_position("C00001", lat, lon, 35000, odd=1)
            input_frames.append(beast_frame(even, timestamp=ts))
            ts += 500
            input_frames.append(beast_frame(odd, timestamp=ts))
            ts += 500

        received = self._collect_beast_output(input_frames)

        self.assertTrue(len(received) > 0,
                        "No data from beast output port")

        # Parse frames and check for ICAO C00001
        frames = self._parse_beast_frames(received)
        icao_found = False
        for ftype, msg_bytes in frames:
            if ftype == '3' and len(msg_bytes) == 14:
                # ICAO is bytes 1-3 of DF17 message
                icao = (msg_bytes[1] << 16) | (msg_bytes[2] << 8) | msg_bytes[3]
                if icao == 0xC00001:
                    icao_found = True
                    break

        self.assertTrue(icao_found,
                        "ICAO C00001 not found in Beast output frames")

    def test_s2_beast_escape_passthrough(self):
        """Beast output correctly escapes 0x1A bytes in data."""
        # Create a DF17 message that contains 0x1A bytes by choosing
        # an ICAO with 0x1A in it: e.g., 0x001A00
        # The ICAO 001A00 will have byte[3] = 0x1A byte[2] = 0x00 byte[1] = 0x00
        # Actually DF17 format: byte 0 = 0x8D, bytes 1-3 = ICAO
        # So ICAO 0x1A1A1A would give bytes 1-3 all 0x1A
        msg_even = make_df17_position("1A1A1A", 51.5, -0.1, 35000, odd=0)
        msg_odd = make_df17_position("1A1A1A", 51.5, -0.1, 35000, odd=1)

        input_frames = []
        ts = 70000
        for batch in range(10):
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            even = make_df17_position("1A1A1A", lat, lon, 35000, odd=0)
            odd = make_df17_position("1A1A1A", lat, lon, 35000, odd=1)
            input_frames.append(beast_frame(even, timestamp=ts))
            ts += 500
            input_frames.append(beast_frame(odd, timestamp=ts))
            ts += 500

        received = self._collect_beast_output(input_frames)

        self.assertTrue(len(received) > 0,
                        "No data from beast output port for 0x1A ICAO")

        # Verify that 0x1A bytes in message data are properly escaped
        # (doubled). Every raw 0x1A in the body should appear as 0x1A 0x1A.
        # Parse and verify at least one frame decodes the ICAO correctly.
        frames = self._parse_beast_frames(received)
        icao_found = False
        for ftype, msg_bytes in frames:
            if ftype == '3' and len(msg_bytes) == 14:
                icao = (msg_bytes[1] << 16) | (msg_bytes[2] << 8) | msg_bytes[3]
                if icao == 0x1A1A1A:
                    icao_found = True
                    break

        self.assertTrue(icao_found,
                        "ICAO 1A1A1A not found after un-escaping Beast output")


# ===================================================================
# T: Net-Connector Beast Out
# ===================================================================

class TestNetConnectorBeastOut(unittest.TestCase):
    """Test --net-connector with beast_out protocol."""

    def test_t1_net_connector_beast_out(self):
        """Net-connector beast_out sends Beast data to remote server."""
        # Start a TCP server to receive the net-connector connection
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server_port = server.getsockname()[1]
        server.listen(1)
        server.settimeout(15)

        try:
            # Launch readsb with net-connector pointing to our server
            inst = ReadsbInstance(
                beast_in=True,
                extra_args=[
                    "--json-reliable", "1",
                    "--net-connector",
                    f"127.0.0.1,{server_port},beast_out",
                ],
            )
            inst.__enter__()

            try:
                # Accept the incoming connection from readsb
                client, _ = server.accept()
                client.settimeout(10)

                # Feed DF17 position messages via Beast input
                beast_sock = socket.create_connection(
                    ("127.0.0.1", inst.beast_in_port), timeout=5
                )

                ts = 90000
                for batch in range(15):
                    lat = 51.5 + batch * 0.002
                    lon = -0.1 + batch * 0.002
                    even = make_df17_position("D00001", lat, lon, 35000, odd=0)
                    odd = make_df17_position("D00001", lat, lon, 35000, odd=1)
                    beast_sock.sendall(beast_frame(even, timestamp=ts))
                    ts += 500
                    beast_sock.sendall(beast_frame(odd, timestamp=ts))
                    ts += 500
                    time.sleep(0.15)

                # Collect data from the net-connector
                received = b""
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    try:
                        chunk = client.recv(8192)
                        if chunk:
                            received += chunk
                            # Check if we have what we need
                            if len(received) > 20:
                                break
                        else:
                            break
                    except socket.timeout:
                        if received:
                            break

                beast_sock.close()
                client.close()

                self.assertTrue(len(received) > 0,
                                "No data received from net-connector beast_out")

                # Verify Beast framing present
                self.assertIn(b'\x1a', received,
                              "Beast escape byte not found in net-connector output")

                # Parse frames and look for ICAO D00001
                frames_found = 0
                icao_found = False
                i = 0
                while i < len(received) - 1:
                    if received[i] == 0x1A:
                        if i + 1 < len(received) and received[i + 1] == 0x1A:
                            i += 2
                            continue
                        type_byte = received[i + 1:i + 2]
                        if type_byte in (b'1', b'2', b'3'):
                            frames_found += 1
                            # For type '3', try to extract ICAO
                            if type_byte == b'3':
                                # Unescape and extract: 6 ts + 1 sig + 14 msg
                                payload = bytearray()
                                j = i + 2
                                while j < len(received) and len(payload) < 21:
                                    if (received[j] == 0x1A and j + 1 < len(received)):
                                        if received[j + 1] == 0x1A:
                                            payload.append(0x1A)
                                            j += 2
                                            continue
                                        else:
                                            break
                                    payload.append(received[j])
                                    j += 1
                                if len(payload) >= 21:
                                    # ICAO at positions 7+1, 7+2, 7+3 (after ts+sig)
                                    icao = ((payload[8] << 16) |
                                            (payload[9] << 8) |
                                            payload[10])
                                    if icao == 0xD00001:
                                        icao_found = True
                        i += 2
                    else:
                        i += 1

                self.assertGreater(frames_found, 0,
                                   "No Beast frames in net-connector output")
                self.assertTrue(icao_found,
                                "ICAO D00001 not in net-connector beast_out")

            finally:
                inst.__exit__(None, None, None)
        finally:
            server.close()


# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if not READSB_BIN.exists():
        print(f"ERROR: readsb binary not found at {READSB_BIN}",
              file=sys.stderr)
        print("Build it first with: make -j$(nproc)", file=sys.stderr)
        sys.exit(1)
    unittest.main(verbosity=2)
