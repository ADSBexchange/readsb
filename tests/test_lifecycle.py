"""
Process lifecycle integration tests.

E: Clean shutdown
J: Aircraft staleness
"""

import json
import shutil
import signal
import subprocess
import time
import unittest
from pathlib import Path

from conftest import (
    ReadsbInstance, feed_sbs, poll_aircraft_json, sbs_msg3,
)


# ===================================================================
# E: Lifecycle
# ===================================================================

class TestShutdown(unittest.TestCase):
    """Process lifecycle behaviour."""

    def test_e1_clean_sigterm(self):
        """SIGTERM -> exit code 0 within 5 seconds."""
        inst = ReadsbInstance()
        inst.__enter__()
        try:
            self.assertIsNone(inst.proc.poll(), "process not running")
            inst.proc.send_signal(signal.SIGTERM)
            try:
                ret = inst.proc.wait(timeout=5)
                # Mark process as terminated so __exit__ doesn't try again
                inst.proc = None
                self.assertEqual(ret, 0, f"exit code {ret}, expected 0")
            except subprocess.TimeoutExpired:
                inst.proc.kill()
                self.fail("readsb did not exit within 5 s of SIGTERM")
        finally:
            if inst.tmpdir:
                shutil.rmtree(inst.tmpdir, ignore_errors=True)


# ===================================================================
# J: Aircraft Staleness
# ===================================================================

class TestAircraftStaleness(unittest.TestCase):
    """Verify the 'seen' field increases after messages stop."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--lat", "51.5", "--lon", "-0.1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_j1_seen_increases(self):
        """After messages stop, the 'seen' field grows past 2 seconds."""
        feeder = self.inst.feeder()
        # Feed data to establish the aircraft
        for _ in range(4):
            feed_sbs(feeder, [
                sbs_msg3("AABB00", alt=15000, lat=51.5, lon=-0.1),
                sbs_msg3("AABB00", alt=15000, lat=51.5, lon=-0.1),
            ])
            time.sleep(0.3)

        # Confirm aircraft appears
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="aabb00")
        self.assertIsNotNone(data, "aircraft.json never contained aabb00")

        # Stop feeding and wait for staleness to accumulate
        time.sleep(4)

        # Read aircraft.json again
        path = Path(self.inst.tmpdir) / "aircraft.json"
        data = json.loads(path.read_text())
        ac = {a["hex"]: a for a in data.get("aircraft", [])}
        self.assertIn("aabb00", ac, "aabb00 disappeared from aircraft.json")
        seen = ac["aabb00"].get("seen")
        self.assertIsNotNone(seen, "'seen' field missing")
        self.assertGreater(seen, 2.0,
                           f"Expected seen > 2.0s, got {seen}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
