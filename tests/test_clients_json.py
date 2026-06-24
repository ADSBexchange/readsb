"""
clients.json format integration test (--net-ingest).

An ingest node (the prod `beast` role) writes clients.json listing connected
feeders. The leaderboard parses it by position, so the `format[]` header and the
arity of each positional client row are a hard contract — a reordered or added
column silently corrupts feeder stats. This pins both against the 3.14.1631
baseline.

Root: {now, format[], clients[]}. Each client row (positional, len == format):
  [0] receiverId (uuid)      [5] positions/s
  [1] host:port              [6] reduce_signal
  [2] avg. kbit/s            [7] recent_rtt(ms)  (-1 = none)
  [3] conn time(s)           [8] positions
  [4] messages/s
"""

import json
import socket
import time
import unittest
from pathlib import Path

from conftest import ReadsbInstance, beast_frame, make_df17_position, wait_for_port

# The clients.json column header for readsb 3.14.1631 (leaderboard ingest
# contract). A change here breaks leaderboard feeder stats — treat as a gate.
EXPECTED_FORMAT = [
    "receiverId", "host:port", "avg. kbit/s", "conn time(s)", "messages/s",
    "positions/s", "reduce_signal", "recent_rtt(ms)", "positions",
]


def poll_clients_json(json_dir, timeout=10, min_clients=1):
    """Poll until clients.json exists with at least *min_clients* entries."""
    path = Path(json_dir) / "clients.json"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            try:
                data = json.loads(path.read_text())
                if len(data.get("clients", [])) >= min_clients:
                    return data
            except (json.JSONDecodeError, OSError):
                pass
        time.sleep(0.3)
    return None


class TestClientsJson(unittest.TestCase):
    """clients.json header + positional row contract with a connected feeder."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(beast_in=True, extra_args=["--net-ingest"])
        cls.inst.__enter__()
        wait_for_port(cls.inst.beast_in_port)
        # Persistent beast feeder — clients.json only lists *active* connections,
        # so the socket stays open until tearDownClass.
        cls.client = socket.create_connection(
            ("127.0.0.1", cls.inst.beast_in_port), timeout=5)
        for _ in range(20):
            cls.client.sendall(
                beast_frame(make_df17_position("ABC123", 51.5, -0.1, 35000, 0))
                + beast_frame(make_df17_position("ABC123", 51.5, -0.1, 35000, 1)))
            time.sleep(0.3)
        cls.data = poll_clients_json(cls.inst.tmpdir, timeout=10, min_clients=1)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.client.close()
        except Exception:
            pass
        cls.inst.__exit__(None, None, None)

    def test_c1_clients_json_structure(self):
        """clients.json is written with {now, format, clients}."""
        self.assertIsNotNone(self.data, "clients.json never had a client entry")
        self.assertIn("now", self.data)
        self.assertIsInstance(self.data["now"], (int, float))
        self.assertIn("format", self.data)
        self.assertIn("clients", self.data)
        self.assertIsInstance(self.data["clients"], list)

    def test_c2_format_header_contract(self):
        """The format[] column header matches the leaderboard-parsed contract."""
        self.assertEqual(self.data["format"], EXPECTED_FORMAT,
                         "clients.json column header changed — leaderboard parses "
                         "by position, so this breaks feeder stats")

    def test_c3_client_row_is_positional_and_matches_format(self):
        """Each client row has the same arity as format; key fields typed right."""
        self.assertGreater(len(self.data["clients"]), 0, "no connected clients")
        row = self.data["clients"][0]
        self.assertIsInstance(row, list)
        self.assertEqual(len(row), len(self.data["format"]),
                         "client row arity must match the format[] header")
        self.assertIsInstance(row[0], str)   # [0] receiverId (uuid)
        self.assertIsInstance(row[1], str)   # [1] host:port
        self.assertIn("port", row[1])


if __name__ == "__main__":
    unittest.main(verbosity=2)
