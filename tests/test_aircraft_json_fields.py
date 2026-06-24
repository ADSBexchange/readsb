"""
aircraft.json field-stability integration test.

aircraft.json is parsed by three consumers (tar1090, leaderboard, exporters).
This pins the top-level shape and the high-value per-aircraft object fields +
types that 3.14.1631 emits, so the reconciliation walk flags a field
rename/retype/removal at the commit that introduces it.

(Each aircraft is a JSON object — the positional array layout is binCraft-only,
not the JSON output.)
"""

import json
import time
import unittest
from pathlib import Path

from conftest import (
    ReadsbInstance, feed_sbs, poll_aircraft_json,
    sbs_msg1, sbs_msg3, sbs_msg4, sbs_msg6,
)


class TestAircraftJsonFields(unittest.TestCase):
    """Top-level shape + per-aircraft object field contract."""

    HEX = "abc123"

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        # Rich feed: callsign + position + velocity + squawk, so the high-value
        # fields are populated.
        for _ in range(8):
            feed_sbs(sock, [
                sbs_msg1(cls.HEX, "TEST123 "),
                sbs_msg3(cls.HEX, alt=35000, lat=51.5, lon=-0.1),
                sbs_msg4(cls.HEX, gs=450, track=90, vr=64),
                sbs_msg6(cls.HEX, squawk="1200"),
            ])
            time.sleep(0.3)
        # Poll until the entry actually carries a position — the hex can appear
        # (from the callsign message) a write-cycle before lat/lon land.
        cls.data = None
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            d = poll_aircraft_json(cls.inst.tmpdir, timeout=2, want_hex=cls.HEX)
            ac = next((a for a in d["aircraft"] if a.get("hex") == cls.HEX), None) if d else None
            if ac and "lat" in ac:
                cls.data = d
                break
            time.sleep(0.3)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _aircraft(self):
        self.assertIsNotNone(self.data, "aircraft.json never appeared with our hex")
        ac = next((a for a in self.data["aircraft"] if a.get("hex") == self.HEX), None)
        self.assertIsNotNone(ac, f"{self.HEX} not in aircraft.json")
        return ac

    def test_a1_top_level_shape(self):
        """aircraft.json top-level is {now, messages, aircraft[]}."""
        self.assertIsNotNone(self.data)
        self.assertIsInstance(self.data.get("now"), (int, float))
        self.assertIsInstance(self.data.get("messages"), (int, float))
        self.assertIsInstance(self.data.get("aircraft"), list)

    def test_a2_aircraft_entries_are_objects(self):
        """Each aircraft entry is a JSON object keyed by hex."""
        ac = self._aircraft()
        self.assertIsInstance(ac, dict)
        self.assertIsInstance(ac["hex"], str)

    def test_a3_high_value_fields_and_types(self):
        """The fields the three consumers parse are present and correctly typed."""
        ac = self._aircraft()
        self.assertIsInstance(ac.get("flight"), str)
        self.assertIsInstance(ac.get("lat"), (int, float))
        self.assertIsInstance(ac.get("lon"), (int, float))
        # alt_baro is numeric in flight, or the string "ground"
        self.assertIsInstance(ac.get("alt_baro"), (int, float, str))
        self.assertIsInstance(ac.get("gs"), (int, float))
        self.assertIsInstance(ac.get("track"), (int, float))
        self.assertIsInstance(ac.get("seen"), (int, float))
        self.assertIsInstance(ac.get("seen_pos"), (int, float))
        self.assertIsInstance(ac.get("rssi"), (int, float))
        self.assertIsInstance(ac.get("messages"), (int, float))
        self.assertIsInstance(ac.get("squawk"), str)
        self.assertIsInstance(ac.get("type"), str)

    def test_a4_list_fields_are_arrays(self):
        """mlat / tisb are arrays (tar1090 iterates them)."""
        ac = self._aircraft()
        self.assertIsInstance(ac.get("mlat"), list)
        self.assertIsInstance(ac.get("tisb"), list)


if __name__ == "__main__":
    unittest.main(verbosity=2)
