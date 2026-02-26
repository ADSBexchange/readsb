"""
Net connector integration tests.

N: Beast binary input with DF17 messages
P: Multi-source input (SBS + Beast simultaneously)
"""

import socket
import time
import unittest

from conftest import (
    ReadsbInstance, beast_frame, feed_sbs,
    make_df17_msg, make_df17_position,
    poll_aircraft_json, sbs_msg1, sbs_msg3,
)


# ===================================================================
# N: Beast Input with DF17 Messages
# ===================================================================

class TestBeastDF17Input(unittest.TestCase):
    """Feed Beast-framed DF17 messages, verify aircraft appear in API."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(beast_in=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _beast_feeder(self):
        return socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

    def test_n1_beast_binary_adsb(self):
        """Feed Beast-format DF17 position message, verify aircraft appears."""
        sock = self._beast_feeder()
        ts = 200000
        for batch in range(10):
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            even = make_df17_position("E00001", lat, lon, 35000, odd=0)
            odd = make_df17_position("E00001", lat, lon, 35000, odd=1)
            sock.sendall(beast_frame(even, timestamp=ts))
            ts += 500
            sock.sendall(beast_frame(odd, timestamp=ts))
            ts += 500
            time.sleep(0.15)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="e00001")
        self.assertIsNotNone(data, "aircraft.json never contained e00001")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("e00001", hexes)

    def test_n2_beast_mode_s_short(self):
        """Feed Beast-format Mode S short message (7 bytes), no crash."""
        sock = self._beast_feeder()
        # 7-byte Mode S short message (DF0 all-call reply)
        short_msg = bytes.fromhex("00A00000000000")
        frame = beast_frame(short_msg, timestamp=300000)
        for _ in range(5):
            sock.sendall(frame)
            time.sleep(0.2)
        sock.close()
        time.sleep(0.5)
        # Process should still be running
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed on short Mode S message")

    def test_n3_beast_timestamp_propagation(self):
        """Beast timestamp propagates to aircraft seen time."""
        sock = self._beast_feeder()
        ts = 400000
        for batch in range(10):
            lat = 52.0 + batch * 0.002
            lon = 1.0 + batch * 0.002
            even = make_df17_position("E00003", lat, lon, 25000, odd=0)
            odd = make_df17_position("E00003", lat, lon, 25000, odd=1)
            sock.sendall(beast_frame(even, timestamp=ts))
            ts += 500
            sock.sendall(beast_frame(odd, timestamp=ts))
            ts += 500
            time.sleep(0.15)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="e00003")
        self.assertIsNotNone(data, "aircraft.json never contained e00003")
        ac = [a for a in data["aircraft"] if a["hex"] == "e00003"]
        self.assertEqual(len(ac), 1)
        # Aircraft should have a "seen" field (seconds since last message)
        self.assertIn("seen", ac[0])
        self.assertIsInstance(ac[0]["seen"], (int, float))


# ===================================================================
# P: Multi-Source Input (SBS + Beast)
# ===================================================================

class TestMultiSource(unittest.TestCase):
    """Feed aircraft via both SBS and Beast inputs simultaneously."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(beast_in=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_p1_sbs_and_beast_combined(self):
        """Different aircraft via SBS and Beast both appear in API."""
        # Feed aircraft F00001 via SBS
        sbs_sock = self.inst.feeder()
        feed_sbs(sbs_sock, [
            sbs_msg3("F00001", alt=30000, lat=51.0, lon=-0.5),
        ])

        # Feed aircraft F00002 via Beast
        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )
        ts = 500000
        for batch in range(10):
            lat = 52.0 + batch * 0.002
            lon = 0.5 + batch * 0.002
            even = make_df17_position("F00002", lat, lon, 35000, odd=0)
            odd = make_df17_position("F00002", lat, lon, 35000, odd=1)
            beast_sock.sendall(beast_frame(even, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd, timestamp=ts))
            ts += 500
            time.sleep(0.15)
        beast_sock.close()

        # Both should appear
        data = poll_aircraft_json(self.inst.tmpdir, min_aircraft=2, timeout=10)
        self.assertIsNotNone(data)
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("f00001", hexes, "SBS aircraft F00001 not found")
        self.assertIn("f00002", hexes, "Beast aircraft F00002 not found")

    def test_p2_same_aircraft_both_sources(self):
        """Same ICAO fed via both SBS and Beast, data merges."""
        icao = "F00003"

        # Feed callsign via SBS
        sbs_sock = self.inst.feeder()
        feed_sbs(sbs_sock, [
            sbs_msg1(icao, "TEST123"),
            sbs_msg3(icao, alt=28000, lat=50.5, lon=-1.0),
        ])

        # Feed position updates via Beast
        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )
        ts = 600000
        for batch in range(10):
            lat = 50.5 + batch * 0.002
            lon = -1.0 + batch * 0.002
            even = make_df17_position(icao, lat, lon, 28000, odd=0)
            odd = make_df17_position(icao, lat, lon, 28000, odd=1)
            beast_sock.sendall(beast_frame(even, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd, timestamp=ts))
            ts += 500
            time.sleep(0.15)
        beast_sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="f00003")
        self.assertIsNotNone(data, "aircraft.json never contained f00003")
        ac = [a for a in data["aircraft"] if a["hex"] == "f00003"]
        self.assertEqual(len(ac), 1, "Should be exactly one entry for f00003")


if __name__ == "__main__":
    unittest.main(verbosity=2)
