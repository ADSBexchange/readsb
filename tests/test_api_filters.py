"""
HTTP API filter integration tests.

M: Callsign filter endpoints (prefix, exact) — validates parsing and response
N: Registration and type lookup (without database)

Note: parseFetch() lowercases the entire HTTP request before parsing. The
filter_callsign_prefix and filter_callsign_exact filters compare against
uppercase callsigns stored in binCraft.  As a result, these filters currently
return zero matches.  The find_callsign endpoint (hash-based) uppercases its
input and works correctly — see TestApiFindCallsign in test_api.py.
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
# M: Callsign Filter API
# ===================================================================

class TestApiCallsignFilters(unittest.TestCase):
    """Validate callsign filter endpoint parsing and response format.

    filter_callsign_prefix and filter_callsign_exact are accepted by the
    API and return valid JSON.  Due to a case-sensitivity issue (the HTTP
    request is lowercased but binCraft stores uppercase callsigns), these
    filters currently return zero results.  Tests here verify the endpoints
    parse correctly and don't crash.
    """

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"]
        )
        cls.inst.__enter__()

        # Feed 3 aircraft with callsigns BAW123, BAW456, DLH789
        lines = [
            sbs_msg1("AA0001", "BAW123"),
            sbs_msg3("AA0001", alt=35000, lat=51.5, lon=-0.1),
            sbs_msg3("AA0001", alt=35000, lat=51.5, lon=-0.1),
            sbs_msg1("AA0002", "BAW456"),
            sbs_msg3("AA0002", alt=36000, lat=51.6, lon=-0.2),
            sbs_msg3("AA0002", alt=36000, lat=51.6, lon=-0.2),
            sbs_msg1("AA0003", "DLH789"),
            sbs_msg3("AA0003", alt=37000, lat=51.7, lon=-0.3),
            sbs_msg3("AA0003", alt=37000, lat=51.7, lon=-0.3),
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

    def test_m1_filter_callsign_prefix_accepted(self):
        """filter_callsign_prefix is accepted and returns valid JSON."""
        status, data = self._api_get_json("/?all&filter_callsign_prefix=BAW")
        self.assertEqual(status, 200)
        self.assertIn("aircraft", data)

    def test_m2_filter_callsign_prefix_no_match(self):
        """filter_callsign_prefix=ZZZ returns empty aircraft list."""
        status, data = self._api_get_json("/?all&filter_callsign_prefix=ZZZ")
        self.assertEqual(status, 200)
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 0)

    def test_m3_filter_callsign_exact_accepted(self):
        """filter_callsign_exact is accepted and returns valid JSON."""
        status, data = self._api_get_json("/?all&filter_callsign_exact=BAW123")
        self.assertEqual(status, 200)
        self.assertIn("aircraft", data)

    def test_m4_find_callsign_lowercase(self):
        """find_callsign with lowercase input still finds the aircraft.

        Unlike filter_callsign_prefix, find_callsign uppercases its input
        before hash lookup, so it works regardless of request case.
        """
        status, data = self._api_get_json("/?find_callsign=baw123")
        self.assertEqual(status, 200)
        flights = {a.get("flight", "").strip() for a in data.get("aircraft", [])}
        self.assertIn("BAW123", flights)

    def test_m5_all_includes_callsign_data(self):
        """Verify ?all returns aircraft with flight field from SBS MSG,1."""
        status, data = self._api_get_json("/?all")
        self.assertEqual(status, 200)
        flights = {a.get("flight", "").strip() for a in data.get("aircraft", [])
                   if a.get("flight")}
        self.assertIn("BAW123", flights)
        self.assertIn("BAW456", flights)
        self.assertIn("DLH789", flights)


# ===================================================================
# N: Registration and Type Lookup (no database)
# ===================================================================

class TestApiRegAndType(unittest.TestCase):
    """Test find_reg and find_type endpoints parse gracefully without database."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _api_get(self, path):
        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", path)
        resp = conn.getresponse()
        data = json.loads(resp.read())
        conn.close()
        return resp.status, data

    def test_n1_find_reg_empty(self):
        """find_reg=N12345 returns valid JSON with 0 matching aircraft."""
        status, data = self._api_get("/?find_reg=N12345")
        self.assertEqual(status, 200)
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 0)

    def test_n2_find_type_empty(self):
        """find_type=A320 returns valid JSON with 0 matching aircraft."""
        status, data = self._api_get("/?find_type=A320")
        self.assertEqual(status, 200)
        aircraft = data.get("aircraft", [])
        self.assertEqual(len(aircraft), 0)


if __name__ == "__main__":
    unittest.main()
