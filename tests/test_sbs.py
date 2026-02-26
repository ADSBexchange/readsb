"""
SBS protocol integration tests.

A: SBS Input -> JSON File Output
B: SBS Input -> SBS Output
"""

import json
import socket
import time
import unittest
from pathlib import Path

from conftest import (
    ReadsbInstance, feed_sbs, poll_aircraft_json,
    sbs_msg1, sbs_msg3, sbs_msg4, sbs_msg6,
)


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


if __name__ == "__main__":
    unittest.main(verbosity=2)
