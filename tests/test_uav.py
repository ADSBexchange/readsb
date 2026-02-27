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


# ===================================================================
# E: UAV filter_uav API with database file
# ===================================================================

def _make_db_csv(path):
    """Write a minimal aircraft db CSV with UAV-flagged and normal entries.

    Format: hex;registration;typeCode;dbFlags;typeLong;year;owner;
    (trailing semicolon required — nextToken needs ';' after every field)
    dbFlags is a binary string, bit 4 = UAV.
    File must be >= 1000 bytes.
    """
    uav_flags = "00001" + "0" * 27  # bit 4 set (UAV)
    no_flags  = "0" * 32
    lines = [
        # UAV entry: $-prefixed hex with dbFlags bit 4
        f"$000010;DRN010;UAV;{uav_flags};TestDrone;2024;TestOp;",
        # Normal ICAO entry: no UAV flag
        f"AAAAAA;N12345;B738;{no_flags};Boeing 737-800;2015;TestAirline;",
    ]
    content = "\n".join(lines) + "\n"
    # Pad to >= 1000 bytes with dummy entries (7 semicolons = 8 fields, addr=0 → skipped)
    while len(content.encode()) < 1100:
        content += "000000;;;;;;;\n"
    Path(path).write_text(content)


class TestUavFilterApi(unittest.TestCase):
    """Test /?filter_uav API endpoint with a database file."""

    @classmethod
    def setUpClass(cls):
        import tempfile as _tf
        cls._db_dir = _tf.mkdtemp(prefix="readsb-db-")
        cls._db_path = str(Path(cls._db_dir) / "aircraft.csv")
        _make_db_csv(cls._db_path)

        cls.inst = ReadsbInstance(extra_args=[
            "--enable-uav", "--lat", "51.5", "--lon", "-0.1",
            "--db-file", cls._db_path,
        ])
        cls.inst.__enter__()

        # Give db loading time (dbUpdate runs in background thread,
        # dbFinishUpdate swaps in main loop)
        time.sleep(2)

        # Feed a UAV and a normal ICAO aircraft repeatedly to ensure
        # the db has time to load and updateTypeReg propagates dbFlags.
        # dbUpdate runs every 30s from startup (first check at t=0),
        # and dbFinishUpdate re-runs updateTypeReg on all aircraft.
        feeder = cls.inst.feeder()
        for _ in range(8):
            feed_sbs(feeder, [
                sbs_msg3("$000010", alt=500, lat=51.5, lon=-0.1),
                sbs_msg3("AAAAAA", alt=35000, lat=51.5, lon=-0.1),
            ])
            time.sleep(0.5)

        # Wait for aircraft to appear in JSON
        poll_aircraft_json(cls.inst.tmpdir, want_hex="$000010")

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)
        import shutil as _sh
        _sh.rmtree(cls._db_dir, ignore_errors=True)

    def _api_get(self, query):
        """Issue a GET to the API and return (status, data_dict)."""
        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", query)
        resp = conn.getresponse()
        status = resp.status
        body = resp.read()
        conn.close()
        if status == 200:
            return status, json.loads(body)
        return status, {}

    def test_e1_filter_uav_recognized(self):
        """/?all&filter_uav returns 200 (recognized parameter)."""
        status, _ = self._api_get("/?all&filter_uav")
        self.assertEqual(status, 200, "filter_uav should be a recognized parameter")

    def test_e2_filter_uav_returns_uav(self):
        """/?all&filter_uav returns the UAV aircraft (db flag bit 4 match)."""
        # First verify the aircraft exists via /?all
        status, all_data = self._api_get("/?all")
        self.assertEqual(status, 200)
        all_hexes = {a["hex"] for a in all_data.get("aircraft", [])}
        self.assertIn("$000010", all_hexes, "UAV should exist in /?all")

        # Check dbFlags on the aircraft (may need db loading time)
        uav_ac = [a for a in all_data.get("aircraft", []) if a["hex"] == "$000010"]
        if uav_ac:
            db_flags = uav_ac[0].get("dbFlags", 0)
            self.assertTrue(db_flags & 16,
                            f"UAV dbFlags should have bit 4 set, got {db_flags}")

        # Now check filter_uav endpoint
        status, data = self._api_get("/?all&filter_uav")
        self.assertEqual(status, 200)
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertIn("$000010", hexes, "UAV should appear in filter_uav results")

    def test_e3_filter_uav_excludes_icao(self):
        """/?all&filter_uav does NOT return normal ICAO aircraft."""
        status, data = self._api_get("/?all&filter_uav")
        self.assertEqual(status, 200)
        hexes = {a["hex"] for a in data.get("aircraft", [])}
        self.assertNotIn("aaaaaa", hexes,
                         "Normal ICAO aircraft should not appear in filter_uav results")


if __name__ == "__main__":
    unittest.main(verbosity=2)
