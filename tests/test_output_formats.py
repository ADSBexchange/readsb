"""
Output format integration tests.

I: Stats JSON
O: VRS JSON output
Q: JSON network output (TCP)
"""

import json
import socket
import time
import unittest
from pathlib import Path

from conftest import (
    ReadsbInstance, beast_frame, feed_sbs,
    make_df17_position, sbs_msg1, sbs_msg3, sbs_msg4, sbs_msg6,
)


# ===================================================================
# I: Stats JSON
# ===================================================================

class TestStatsJson(unittest.TestCase):
    """Verify stats.json is written with expected structure."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_i1_stats_json(self):
        """stats.json has expected top-level fields after feeding SBS data."""
        # Feed data over several seconds; stats.json is only written every ~10s
        feeder = self.inst.feeder()
        stats_path = Path(self.inst.tmpdir) / "stats.json"
        deadline = time.monotonic() + 20
        data = None
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("112233", alt=20000, lat=51.5, lon=-0.1),
                sbs_msg3("112233", alt=20000, lat=51.5, lon=-0.1),
            ])
            if stats_path.exists():
                try:
                    data = json.loads(stats_path.read_text())
                    if data.get("total", {}).get("messages", 0) > 0:
                        break
                except (json.JSONDecodeError, KeyError):
                    pass
            time.sleep(1)

        self.assertIsNotNone(data, "stats.json not found or empty")

        # Top-level "now" field
        self.assertIn("now", data)
        self.assertIsInstance(data["now"], (int, float))

        # "total" section with message count
        self.assertIn("total", data)
        total = data["total"]
        self.assertGreater(total.get("messages", 0), 0,
                           "Expected total messages > 0")

        # "total" -> "remote" section (we're in --net-only mode)
        self.assertIn("remote", total)
        remote = total["remote"]

        # SBS = basestation format counter
        self.assertGreater(remote.get("basestation", 0), 0,
                           "Expected basestation messages > 0")

        # Track count
        self.assertIn("tracks", total)
        self.assertGreaterEqual(total["tracks"].get("all", 0), 1,
                                "Expected at least 1 track")


# ===================================================================
# O: VRS JSON Output
# ===================================================================

class TestVrsJsonOutput(unittest.TestCase):
    """Test VRS format output on --net-vrs-port."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(vrs_out=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_o1_vrs_format(self):
        """VRS output has acList structure with correct fields."""
        # Connect to VRS output
        vrs_sock = socket.create_connection(
            ("127.0.0.1", self.inst.vrs_out_port), timeout=5
        )
        vrs_sock.settimeout(3)

        feeder = self.inst.feeder()

        received = b""
        deadline = time.monotonic() + 15
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg1("D00001", "TST999"),
                sbs_msg3("D00001", alt=35000, lat=51.5, lon=-0.1, gs=450, track=180),
                sbs_msg3("D00001", alt=35000, lat=51.5, lon=-0.1, gs=450, track=180),
                sbs_msg4("D00001", gs=450, track=180),
                sbs_msg6("D00001", squawk="7000"),
            ])
            batch += 1
            time.sleep(1.5)
            try:
                chunk = vrs_sock.recv(8192)
                if chunk:
                    received += chunk
                    if b"acList" in received:
                        break
            except socket.timeout:
                pass

        vrs_sock.close()
        text = received.decode(errors="replace")

        # VRS sends complete JSON objects; find one with acList
        self.assertIn("acList", text, "VRS output missing acList")

        # Try to parse the JSON
        # VRS output may have multiple concatenated objects, find the first valid one
        vrs_data = None
        for line in text.split("\n"):
            line = line.strip()
            if not line:
                continue
            try:
                parsed = json.loads(line)
                if "acList" in parsed:
                    vrs_data = parsed
                    break
            except json.JSONDecodeError:
                continue

        # If no newline-delimited JSON, try the whole blob
        if vrs_data is None:
            try:
                vrs_data = json.loads(text)
            except json.JSONDecodeError:
                pass

        if vrs_data is not None and vrs_data.get("acList"):
            ac = vrs_data["acList"][0]
            # Check for VRS field names
            self.assertIn("Icao", ac)
            self.assertIn("Lat", ac)
            self.assertIn("Long", ac)


# ===================================================================
# Q: JSON Network Output
# ===================================================================

class TestJsonNetworkOutput(unittest.TestCase):
    """Test --net-json-port TCP JSON output.

    JSON port output only triggers for non-SBS-input messages (Beast/raw),
    so this test uses Beast input with DF17 airborne position messages.
    CPR decoding requires both even and odd frames to determine position.
    """

    @classmethod
    def setUpClass(cls):
        # json_reliable=1 lowers the position reliability threshold so
        # the first successfully decoded position triggers output.
        cls.inst = ReadsbInstance(
            beast_in=True, json_out=True,
            extra_args=["--json-reliable", "1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_q1_json_tcp_output(self):
        """JSON-over-TCP output has hex, lat, lon fields."""
        json_sock = socket.create_connection(
            ("127.0.0.1", self.inst.json_out_port), timeout=5
        )
        json_sock.settimeout(3)

        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        received = b""
        deadline = time.monotonic() + 15
        ts = 1000  # Beast timestamp counter
        batch = 0
        while time.monotonic() < deadline:
            # Vary position slightly each iteration to avoid duplicate detection
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            alt = 35000

            # Send both even and odd CPR frames (needed for global decode)
            even_msg = make_df17_position("F00001", lat, lon, alt, odd=0)
            odd_msg = make_df17_position("F00001", lat, lon, alt, odd=1)

            beast_sock.sendall(beast_frame(even_msg, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd_msg, timestamp=ts))
            ts += 500

            batch += 1
            time.sleep(0.5)
            try:
                chunk = json_sock.recv(8192)
                if chunk:
                    received += chunk
                    if b"f00001" in received:
                        break
            except socket.timeout:
                pass

        beast_sock.close()
        json_sock.close()
        text = received.decode(errors="replace")

        # Each line should be valid JSON
        found_aircraft = False
        for line in text.strip().split("\n"):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                if obj.get("hex") == "f00001":
                    found_aircraft = True
                    self.assertIn("lat", obj)
                    self.assertIn("lon", obj)
                    break
            except json.JSONDecodeError:
                continue

        self.assertTrue(found_aircraft,
                        "Aircraft f00001 not found in JSON TCP output")


if __name__ == "__main__":
    unittest.main(verbosity=2)
