"""
Shared test infrastructure for readsb integration tests.

Provides ReadsbInstance (context manager), SBS/Beast/Raw protocol helpers,
CPR encoding, and Mode-S CRC computation.

Requires the readsb binary to be built first (make -j$(nproc)).
Uses only Python 3 stdlib — no external dependencies.
"""

import json
import math
import shutil
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

READSB_BIN = Path(__file__).resolve().parent.parent / "readsb"


# ---------------------------------------------------------------------------
# General helpers
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


# ---------------------------------------------------------------------------
# SBS message builders
# ---------------------------------------------------------------------------

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
