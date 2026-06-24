"""
json_out streaming record integration test (--net-json-port).

The EKS data-ingestion readsb runs --net-only and forwards json_out over TCP to
Vector, which feeds the MSK streaming pipeline. --net-json-port "sends one line
with a json object containing aircraft details for every position received".
This pins the streaming record schema so a field change is caught before it
reaches the pipeline. Driven with beast input (the real apex -> streaming path).
"""

import json
import socket
import time
import unittest

from conftest import (
    ReadsbInstance, beast_frame, make_df17_position, wait_for_port,
)


def read_json_line(sock, timeout=10):
    """Read one newline-terminated JSON object from a json_out connection."""
    sock.settimeout(timeout)
    buf = b""
    try:
        while b"\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    line = buf.split(b"\n")[0].strip()
    return json.loads(line) if line else None


class TestJsonOutStream(unittest.TestCase):
    """One JSON object per position on the --net-json-port stream."""

    HEX = "abc123"
    LAT = 51.5
    LON = -0.1
    ALT = 35000

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(beast_in=True, json_out=True)
        cls.inst.__enter__()
        wait_for_port(cls.inst.beast_in_port)
        cls.client = socket.create_connection(
            ("127.0.0.1", cls.inst.json_out_port), timeout=5)
        feeder = socket.create_connection(
            ("127.0.0.1", cls.inst.beast_in_port), timeout=5)
        # Even/odd CPR pairs so a position resolves and json_out emits.
        for _ in range(15):
            feeder.sendall(beast_frame(make_df17_position(cls.HEX, cls.LAT, cls.LON, cls.ALT, 0)))
            feeder.sendall(beast_frame(make_df17_position(cls.HEX, cls.LAT, cls.LON, cls.ALT, 1)))
            time.sleep(0.3)
        cls.record = read_json_line(cls.client, timeout=10)
        feeder.close()

    @classmethod
    def tearDownClass(cls):
        try:
            cls.client.close()
        except Exception:
            pass
        cls.inst.__exit__(None, None, None)

    def test_j1_emits_json_object_per_position(self):
        """The stream emits a newline-terminated JSON object for a position."""
        self.assertIsNotNone(self.record, "no json_out record received")
        self.assertIsInstance(self.record, dict)

    def test_j2_record_identifies_aircraft_and_position(self):
        """The record carries hex + the decoded position the pipeline consumes."""
        self.assertEqual(self.record.get("hex"), self.HEX)
        self.assertIsInstance(self.record.get("lat"), (int, float))
        self.assertIsInstance(self.record.get("lon"), (int, float))
        # CPR-decoded; within a fraction of a degree of what we encoded
        self.assertAlmostEqual(self.record["lat"], self.LAT, places=1)
        self.assertAlmostEqual(self.record["lon"], self.LON, places=1)
        self.assertEqual(self.record.get("alt_baro"), self.ALT)

    def test_j3_record_has_streaming_fields(self):
        """Record carries the fields the MSK pipeline relies on."""
        for key in ("now", "seen", "seen_pos", "messages", "type"):
            self.assertIn(key, self.record, f"json_out record missing '{key}'")
        self.assertIsInstance(self.record["now"], (int, float))


if __name__ == "__main__":
    unittest.main(verbosity=2)
