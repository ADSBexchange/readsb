"""
UAV / Drone integration tests (AX-688).

C: UAV addresses with --enable-uav
D2/D3: UAV API queries
C (rejected): UAV addresses without --enable-uav
"""

import json
import socket
import time
import unittest
from http.client import HTTPConnection
from pathlib import Path

from conftest import (
    ReadsbInstance, feed_sbs, poll_aircraft_json,
    sbs_msg1, sbs_msg3,
)


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


# ===================================================================
# UAV Rejected (without --enable-uav)
# ===================================================================

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
