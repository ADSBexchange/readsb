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
# ReadsbInstance — context manager
# ---------------------------------------------------------------------------

class ReadsbInstance:
    """Start / stop a readsb process with unique ports and a temp directory."""

    def __init__(self, extra_args=None):
        self.extra_args = extra_args or []
        self.proc = None
        self.tmpdir = None
        self.sbs_in_port = get_free_port()
        self.sbs_out_port = get_free_port()
        self.api_port = get_free_port()
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
        ] + self.extra_args
        self.proc = subprocess.Popen(
            cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        if not wait_for_port(self.sbs_in_port):
            self.proc.kill()
            self.proc.wait()
            raise RuntimeError(
                f"readsb failed to listen on SBS-in port {self.sbs_in_port}"
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
        # Squawk requires tentative confirmation: same value seen
        # after 750ms delay before it's accepted
        time.sleep(1.0)
        self._feed([sbs_msg6("A60000", squawk="4521")])
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="a60000")
        self.assertIsNotNone(data, "aircraft.json never contained a60000")
        ac = {a["hex"]: a for a in data["aircraft"]}
        self.assertIn("a60000", ac)
        self.assertEqual(ac["a60000"].get("squawk"), "4521")


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


# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if not READSB_BIN.exists():
        print(f"ERROR: readsb binary not found at {READSB_BIN}",
              file=sys.stderr)
        print("Build it first with: make -j$(nproc)", file=sys.stderr)
        sys.exit(1)
    unittest.main(verbosity=2)
