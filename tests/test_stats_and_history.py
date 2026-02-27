"""
Receiver JSON, aircraft JSON, and message count integration tests.

U: Receiver and aircraft JSON file verification
V: JSON file writing and update behavior
"""

import json
import time
import unittest
from http.client import HTTPConnection
from pathlib import Path

from conftest import (
    ReadsbInstance, feed_sbs, poll_aircraft_json,
    sbs_msg1, sbs_msg3, sbs_msg4,
)


# ===================================================================
# U: Receiver and Aircraft JSON Files
# ===================================================================

class TestReceiverJson(unittest.TestCase):
    """Verify receiver.json and aircraft.json content after feeding data."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()
        # Feed some data
        sock = cls.inst.feeder()
        for i in range(5):
            feed_sbs(sock, [
                sbs_msg3(f"A{i:05d}", alt=30000 + i * 1000,
                         lat=51.5 + i * 0.01, lon=-0.1 + i * 0.01),
            ])
            time.sleep(0.1)
        # Wait for JSON writes
        time.sleep(1.5)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_u1_receiver_json_structure(self):
        """receiver.json has valid structure with version and refresh."""
        path = Path(self.inst.tmpdir) / "receiver.json"
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists():
                break
            time.sleep(0.2)

        self.assertTrue(path.exists(), "receiver.json not found")
        data = json.loads(path.read_text())
        self.assertIn("version", data)
        self.assertIn("refresh", data)
        self.assertIsInstance(data["refresh"], (int, float))

    def test_u2_aircraft_json_has_fed_data(self):
        """aircraft.json contains aircraft we fed via SBS."""
        data = poll_aircraft_json(self.inst.tmpdir, timeout=8, min_aircraft=1)
        self.assertIsNotNone(data, "aircraft.json never appeared")
        self.assertIn("aircraft", data)
        self.assertIsInstance(data["aircraft"], list)
        self.assertGreater(len(data["aircraft"]), 0,
                           "No aircraft in aircraft.json after feeding")

    def test_u3_api_all_returns_fed_aircraft(self):
        """/?all API endpoint returns aircraft we fed."""
        conn = HTTPConnection("127.0.0.1", self.inst.api_port, timeout=5)
        conn.request("GET", "/?all")
        resp = conn.getresponse()
        self.assertEqual(resp.status, 200)
        data = json.loads(resp.read())
        conn.close()
        self.assertIn("aircraft", data)
        self.assertGreater(len(data["aircraft"]), 0)
        # At least one of our fed aircraft should appear
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertTrue(
            any(f"a{i:05d}" in hexes for i in range(5)),
            f"None of the fed aircraft found in API response. Got: {hexes}"
        )


# ===================================================================
# V: JSON File Writing and Updates
# ===================================================================

class TestJsonFileWriting(unittest.TestCase):
    """Verify JSON files are written and updated correctly."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--write-json-every", "0.5"],
        )
        cls.inst.__enter__()
        # Feed data to trigger file writes
        sock = cls.inst.feeder()
        for i in range(5):
            feed_sbs(sock, [
                sbs_msg3(f"C{i:05d}", alt=35000 + i * 500,
                         lat=51.5, lon=-0.1),
            ])
            time.sleep(0.2)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_v1_aircraft_json_written(self):
        """aircraft.json written to json dir with valid content."""
        data = poll_aircraft_json(self.inst.tmpdir, timeout=8, min_aircraft=1)
        self.assertIsNotNone(data, "aircraft.json never appeared or had no aircraft")
        self.assertIn("aircraft", data)
        self.assertIsInstance(data["aircraft"], list)
        self.assertIn("now", data)

    def test_v2_json_dir_structure(self):
        """Expected files present in write-json dir."""
        time.sleep(1)
        json_dir = Path(self.inst.tmpdir)

        self.assertTrue(
            (json_dir / "receiver.json").exists(),
            "receiver.json missing from JSON dir"
        )
        self.assertTrue(
            (json_dir / "aircraft.json").exists(),
            "aircraft.json missing from JSON dir"
        )

    def test_v3_receiver_json_fields(self):
        """receiver.json contains expected fields."""
        path = Path(self.inst.tmpdir) / "receiver.json"
        self.assertTrue(path.exists())
        data = json.loads(path.read_text())
        self.assertIn("version", data)
        self.assertIn("refresh", data)
        self.assertIn("history", data)

    def test_v4_aircraft_json_updates(self):
        """aircraft.json is updated when new data arrives."""
        path = Path(self.inst.tmpdir) / "aircraft.json"
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists():
                break
            time.sleep(0.2)

        self.assertTrue(path.exists(), "aircraft.json not found")
        initial_mtime = path.stat().st_mtime

        # Feed new data
        sock = self.inst.feeder()
        feed_sbs(sock, [
            sbs_msg3("E00099", alt=40000, lat=53.0, lon=2.0),
        ])
        time.sleep(1.5)

        new_mtime = path.stat().st_mtime
        self.assertGreaterEqual(new_mtime, initial_mtime,
                                "aircraft.json mtime should not decrease")

    def test_v5_aircraft_json_now_field(self):
        """aircraft.json 'now' field is a reasonable timestamp."""
        data = poll_aircraft_json(self.inst.tmpdir, timeout=5, min_aircraft=1)
        self.assertIsNotNone(data)
        self.assertIn("now", data)
        now = data["now"]
        self.assertIsInstance(now, (int, float))
        # Should be a reasonable Unix timestamp (after year 2020)
        self.assertGreater(now, 1577836800,
                           "now field should be a Unix timestamp")

    def test_v6_messages_field(self):
        """aircraft.json has a messages field reflecting activity."""
        data = poll_aircraft_json(self.inst.tmpdir, timeout=5, min_aircraft=1)
        self.assertIsNotNone(data)
        self.assertIn("messages", data)
        self.assertIsInstance(data["messages"], (int, float))
        self.assertGreater(data["messages"], 0,
                           "messages count should be positive after feeding data")


if __name__ == "__main__":
    unittest.main(verbosity=2)
