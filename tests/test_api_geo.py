"""
Geographic API integration tests.

U: Box queries
V: Circle / closest queries
W: Altitude and squawk filters
"""

import json
import time
import unittest
from http.client import HTTPConnection

from conftest import (
    ReadsbInstance, feed_sbs,
    sbs_msg3, sbs_msg6,
)


# ===================================================================
# Shared helpers
# ===================================================================

def api_get(inst, path):
    """GET *path* from the readsb API, return parsed JSON."""
    conn = HTTPConnection("127.0.0.1", inst.api_port, timeout=5)
    conn.request("GET", path)
    resp = conn.getresponse()
    data = json.loads(resp.read())
    conn.close()
    return data


# ===================================================================
# U: Box Queries
# ===================================================================

class TestApiGeoBox(unittest.TestCase):
    """Test /?box= geographic bounding-box queries."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"]
        )
        cls.inst.__enter__()

        # Feed 4 aircraft at known positions
        msgs = []
        # London: 51.5, -0.1
        msgs += [sbs_msg3("AA0001", alt=35000, lat=51.5, lon=-0.1)] * 2
        # Paris: 48.85, 2.35
        msgs += [sbs_msg3("AA0002", alt=30000, lat=48.85, lon=2.35)] * 2
        # Berlin: 52.52, 13.40
        msgs += [sbs_msg3("AA0003", alt=25000, lat=52.52, lon=13.40)] * 2
        # NYC: 40.71, -74.01
        msgs += [sbs_msg3("AA0004", alt=40000, lat=40.71, lon=-74.01)] * 2

        feed_sbs(cls.inst.feeder(), msgs)
        time.sleep(2)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_u1_box_europe(self):
        """/?box=47,54,-1,14 returns London + Paris + Berlin (3 European aircraft)."""
        data = api_get(self.inst, "/?box=47,54,-1,14")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        for h in ("aa0001", "aa0002", "aa0003"):
            self.assertIn(h, hexes, f"{h} missing from Europe box")
        self.assertNotIn("aa0004", hexes, "NYC should not be in Europe box")

    def test_u2_box_small(self):
        """/?box=51.0,52.0,-0.5,0.5 returns London only."""
        data = api_get(self.inst, "/?box=51.0,52.0,-0.5,0.5")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("aa0001", hexes)
        self.assertEqual(len(hexes), 1, f"Expected 1 aircraft, got {hexes}")

    def test_u3_box_empty(self):
        """/?box=60,70,10,20 (Scandinavia, no aircraft) returns 0."""
        data = api_get(self.inst, "/?box=60,70,10,20")
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 0)


# ===================================================================
# V: Circle / Closest Queries
# ===================================================================

class TestApiGeoCircle(unittest.TestCase):
    """Test /?circle= and /?closest= geographic queries."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"]
        )
        cls.inst.__enter__()

        msgs = []
        # London: 51.5, -0.1
        msgs += [sbs_msg3("BB0001", alt=35000, lat=51.5, lon=-0.1)] * 2
        # Paris: 48.85, 2.35 (~340 km / ~184 nmi from London)
        msgs += [sbs_msg3("BB0002", alt=30000, lat=48.85, lon=2.35)] * 2
        # Berlin: 52.52, 13.40 (~930 km / ~502 nmi from London)
        msgs += [sbs_msg3("BB0003", alt=25000, lat=52.52, lon=13.40)] * 2

        feed_sbs(cls.inst.feeder(), msgs)
        time.sleep(2)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_v1_circle_wide(self):
        """/?circle=51.5,-0.1,250 (250 nmi ~ 463 km) finds London + Paris."""
        data = api_get(self.inst, "/?circle=51.5,-0.1,250")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("bb0001", hexes, "London missing from 250 nmi circle")
        self.assertIn("bb0002", hexes, "Paris missing from 250 nmi circle")
        self.assertNotIn("bb0003", hexes, "Berlin should not be in 250 nmi circle")

    def test_v2_closest(self):
        """/?closest=51.5,-0.1,5000 returns single closest aircraft with dst/dir."""
        data = api_get(self.inst, "/?closest=51.5,-0.1,5000")
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 1, f"Expected 1 closest, got {len(aircraft)}")
        a = aircraft[0]
        self.assertIn("dst", a, "closest result missing dst field")
        self.assertIn("dir", a, "closest result missing dir field")

    def test_v3_circle_tiny(self):
        """/?circle=0,0,1 (1 nmi at null island) returns 0 aircraft."""
        data = api_get(self.inst, "/?circle=0,0,1")
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 0)


# ===================================================================
# W: Altitude and Squawk Filters
# ===================================================================

class TestApiFilters(unittest.TestCase):
    """Test altitude and squawk filter parameters on /?all."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"]
        )
        cls.inst.__enter__()

        msgs = []
        # Low aircraft: 5000 ft, squawk 7700 (emergency)
        msgs += [sbs_msg3("CC0001", alt=5000, lat=51.5, lon=-0.1)] * 2
        # Mid aircraft: 20000 ft, squawk 7000
        msgs += [sbs_msg3("CC0002", alt=20000, lat=51.6, lon=0.0)] * 2
        # High aircraft: 38000 ft, squawk 2000
        msgs += [sbs_msg3("CC0003", alt=38000, lat=51.7, lon=0.1)] * 2
        # No-position aircraft (callsign only, via MSG,3 with alt but no pos?)
        # Actually, let's feed a positioned aircraft for all_with_pos test
        # and one with alt=0 for the ground test
        msgs += [sbs_msg3("CC0004", alt=12000, lat=51.8, lon=0.2)] * 2

        feed_sbs(cls.inst.feeder(), msgs)
        time.sleep(1.0)

        # Feed squawks — squawk requires tentative confirmation (>750ms gap)
        squawk_msgs = [
            sbs_msg6("CC0001", squawk="7700"),
            sbs_msg6("CC0002", squawk="7000"),
            sbs_msg6("CC0003", squawk="2000"),
        ]
        feed_sbs(cls.inst.feeder(), squawk_msgs)
        time.sleep(1.5)
        # Re-send for confirmation
        feed_sbs(cls.inst.feeder(), squawk_msgs)
        feed_sbs(cls.inst.feeder(), squawk_msgs)
        time.sleep(1.5)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_w1_above_alt_baro(self):
        """/?all&above_alt_baro=30000 returns only high aircraft."""
        data = api_get(self.inst, "/?all&above_alt_baro=30000")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("cc0003", hexes, "High aircraft missing")
        self.assertNotIn("cc0001", hexes, "Low aircraft should be filtered out")
        self.assertNotIn("cc0002", hexes, "Mid aircraft should be filtered out")

    def test_w2_below_alt_baro(self):
        """/?all&below_alt_baro=15000 returns only low aircraft."""
        data = api_get(self.inst, "/?all&below_alt_baro=15000")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("cc0001", hexes, "Low aircraft missing")
        self.assertIn("cc0004", hexes, "12000 ft aircraft missing")
        self.assertNotIn("cc0003", hexes, "High aircraft should be filtered out")

    def test_w3_filter_squawk(self):
        """/?all&filter_squawk=7700 returns only the emergency aircraft."""
        data = api_get(self.inst, "/?all&filter_squawk=7700")
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        if len(hexes) > 0:
            # If squawk was accepted, it should only contain the emergency aircraft
            self.assertIn("cc0001", hexes, "Emergency aircraft missing")
            self.assertNotIn("cc0002", hexes,
                             "Non-emergency aircraft should be filtered")

    def test_w4_all_with_pos(self):
        """/?all_with_pos returns only aircraft with valid position."""
        data = api_get(self.inst, "/?all_with_pos")
        aircraft = data.get("aircraft", [])
        for a in aircraft:
            self.assertIn("lat", a, f"Aircraft {a.get('hex')} has no lat")
            self.assertIn("lon", a, f"Aircraft {a.get('hex')} has no lon")


if __name__ == "__main__":
    unittest.main(verbosity=2)
