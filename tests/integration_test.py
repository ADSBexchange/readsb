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
# ReadsbInstance — context manager
# ---------------------------------------------------------------------------

class ReadsbInstance:
    """Start / stop a readsb process with unique ports and a temp directory."""

    def __init__(self, extra_args=None, beast_in=False, beast_out=False,
                 raw_in=False, raw_out=False):
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


# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if not READSB_BIN.exists():
        print(f"ERROR: readsb binary not found at {READSB_BIN}",
              file=sys.stderr)
        print("Build it first with: make -j$(nproc)", file=sys.stderr)
        sys.exit(1)
    unittest.main(verbosity=2)
