"""
HTTP API integration tests.

D: Basic API queries
K: Callsign API queries
L: Hex lookup API queries
"""

import json
import time
import unittest
from http.client import HTTPConnection

from conftest import (
    ReadsbInstance, feed_sbs,
    sbs_msg1, sbs_msg3,
)


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


if __name__ == "__main__":
    unittest.main(verbosity=2)
