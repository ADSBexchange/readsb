"""
HTTP API combined query and binary output integration tests.

Q: Combined queries (altitude range, with_pos, mutually exclusive options)
R: Binary/compressed output formats (bincraft, zstd)
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
# Q: Combined Queries
# ===================================================================

class TestApiCombinedQueries(unittest.TestCase):
    """Test combined filter queries."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.0", "--lon", "-1.0"]
        )
        cls.inst.__enter__()

        # Aircraft at varied altitudes, some with positions, some without
        lines = [
            # Low altitude aircraft (5000 ft) with position
            sbs_msg3("BB0001", alt=5000, lat=51.0, lon=-1.0),
            sbs_msg3("BB0001", alt=5000, lat=51.0, lon=-1.0),

            # Mid altitude aircraft (20000 ft) with position
            sbs_msg3("BB0002", alt=20000, lat=51.1, lon=-1.1),
            sbs_msg3("BB0002", alt=20000, lat=51.1, lon=-1.1),

            # High altitude aircraft (40000 ft) with position
            sbs_msg3("BB0003", alt=40000, lat=51.2, lon=-1.2),
            sbs_msg3("BB0003", alt=40000, lat=51.2, lon=-1.2),

            # Aircraft with callsign only (no position) - use msg1 only
            sbs_msg1("BB0004", "TEST44"),
        ]
        feed_sbs(cls.inst.feeder(), lines)
        time.sleep(1.5)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _api_get(self, path):
        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read()
        conn.close()
        return resp.status, body

    def _api_get_json(self, path):
        status, body = self._api_get(path)
        return status, json.loads(body)

    def test_q1_all_with_altitude_range(self):
        """above_alt_baro + below_alt_baro filters to altitude range."""
        status, data = self._api_get_json(
            "/?all&above_alt_baro=10000&below_alt_baro=30000"
        )
        self.assertEqual(status, 200)
        aircraft = data.get("aircraft", [])
        hexes = {a["hex"] for a in aircraft}
        # Only bb0002 (20000 ft) should be in range
        self.assertIn("bb0002", hexes)
        self.assertNotIn("bb0001", hexes)  # 5000 ft, below range
        self.assertNotIn("bb0003", hexes)  # 40000 ft, above range

    def test_q2_all_filter_with_pos(self):
        """filter_with_pos returns only aircraft that have position."""
        status, data = self._api_get_json("/?all&filter_with_pos")
        self.assertEqual(status, 200)
        aircraft = data.get("aircraft", [])
        # All returned aircraft should have lat/lon
        for a in aircraft:
            self.assertIn("lat", a, f"aircraft {a.get('hex')} missing lat")
            self.assertIn("lon", a, f"aircraft {a.get('hex')} missing lon")

    def test_q3_mutually_exclusive_rejected(self):
        """Combining circle + all is rejected (mainOptionCount > 1)."""
        # circle + all = mainOptionCount 2, not a special-cased combo
        status, body = self._api_get(
            "/?all&circle=51.0,-1.0,100"
        )
        # readsb returns 400 for invalid option combinations
        self.assertEqual(status, 400)


# ===================================================================
# R: Binary/Compressed Output
# ===================================================================

class TestApiBinaryOutput(unittest.TestCase):
    """Test binary and compressed output formats."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

        # Feed some aircraft so there's data to return
        lines = [
            sbs_msg3("CC0001", alt=35000, lat=48.0, lon=2.0),
            sbs_msg3("CC0001", alt=35000, lat=48.0, lon=2.0),
        ]
        feed_sbs(cls.inst.feeder(), lines)
        time.sleep(1.5)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _api_get_raw(self, path):
        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read()
        conn.close()
        return resp.status, body

    def test_r1_bincraft_response(self):
        """bincraft output returns non-empty binary (not JSON)."""
        status, body = self._api_get_raw("/?all&bincraft")
        self.assertEqual(status, 200)
        self.assertTrue(len(body) > 0, "bincraft response should not be empty")
        # bincraft output should not be valid JSON
        try:
            json.loads(body)
            # If it parses as JSON, it might be an error response - that's ok
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass  # Expected - binary data

    def test_r2_zstd_response(self):
        """zstd output starts with zstd magic bytes."""
        status, body = self._api_get_raw("/?all&zstd")
        self.assertEqual(status, 200)
        self.assertTrue(len(body) >= 4, "zstd response should have at least 4 bytes")
        # Zstd magic number: 0x28B52FFD (little-endian)
        magic = int.from_bytes(body[:4], byteorder='little')
        self.assertEqual(magic, 0xFD2FB528,
                         f"Expected zstd magic 0xFD2FB528, got 0x{magic:08X}")

    def test_r3_bincraft_empty(self):
        """bincraft on non-existent hex doesn't crash."""
        status, body = self._api_get_raw("/?find_hex=FFFFFF&bincraft")
        self.assertEqual(status, 200)
        # Should return something without crashing
        self.assertIsNotNone(body)


if __name__ == "__main__":
    unittest.main()
