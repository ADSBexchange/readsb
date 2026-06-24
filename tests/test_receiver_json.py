"""
receiver.json capability-field integration test.

receiver.json advertises the capabilities tar1090 branches on (binary/zstd
output, trace interval, the globe-index grid + special tiles). This pins the
full field set + types 3.14.1631 emits with --write-json-globe-index, so the
reconciliation walk flags any change at the commit that introduces it.

Note: `haveTraces` is NOT emitted by 3.14.1631 (even with traces written), so it
is intentionally not asserted here — if a later upstream version adds it, that is
a new field to evaluate, not a regression.
"""

import json
import time
import unittest
from pathlib import Path

from conftest import ReadsbInstance, feed_sbs, sbs_msg3

TILE_BBOX_KEYS = {"north", "south", "east", "west"}


class TestReceiverJsonFields(unittest.TestCase):
    """Full receiver.json capability contract (globe-index config)."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(extra_args=["--write-json-globe-index"])
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        for _ in range(4):
            feed_sbs(sock, [sbs_msg3("ABC123", alt=35000, lat=51.5, lon=-0.1)])
            time.sleep(0.2)
        time.sleep(1.5)
        cls.rec = json.loads((Path(cls.inst.tmpdir) / "receiver.json").read_text())

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_r1_core_fields(self):
        """version / refresh / history present and typed."""
        self.assertIsInstance(self.rec.get("version"), str)
        self.assertTrue(self.rec["version"], "version is empty")
        # version string starts with the numeric version (not asserting the
        # build-identity label, which is reverted during the reconciliation walk)
        self.assertTrue(self.rec["version"][0].isdigit(),
                        f"version should start with a version number: {self.rec['version']!r}")
        self.assertIsInstance(self.rec.get("refresh"), (int, float))
        self.assertGreater(self.rec["refresh"], 0)
        self.assertIn("history", self.rec)

    def test_r2_output_capability_flags(self):
        """binCraft / zstd / json_trace_interval capability flags."""
        self.assertIsInstance(self.rec.get("binCraft"), bool)
        self.assertIsInstance(self.rec.get("zstd"), bool)
        self.assertIsInstance(self.rec.get("json_trace_interval"), (int, float))
        self.assertGreater(self.rec["json_trace_interval"], 0)

    def test_r3_globe_index_capability(self):
        """globeIndexGrid + globeIndexSpecialTiles (the tar1090 globe contract)."""
        self.assertIsInstance(self.rec.get("globeIndexGrid"), int)
        self.assertGreater(self.rec["globeIndexGrid"], 0)
        special = self.rec.get("globeIndexSpecialTiles")
        self.assertIsInstance(special, list)
        self.assertGreater(len(special), 0)
        self.assertTrue(TILE_BBOX_KEYS.issubset(special[0].keys()),
                        f"special tile missing bbox keys: {special[0]}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
