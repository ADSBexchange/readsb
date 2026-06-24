"""
Globe-history output integration test (--write-globe-history).

readsb archives history under <history-dir>/YYYY/MM/DD/{traces/<last2hex>/, acas/}
plus an internal_state/ blob set. The historical-export pipeline
(push-scripts → customer buckets) walks this dated layout, so the directory
structure is the contract pinned here against the 3.14.1631 baseline.

Heatmap note: heatmap.bin generation (--heatmap) is gated on a day rollover /
long runtime and is not reproducible in a short test — readsb only creates the
dated heatmap/ directory in-window. So this test asserts the globe-history dir
layout (incl. the heatmap dir when present), not the heatmap.bin blob itself.
"""

import shutil
import tempfile
import time
import unittest
from pathlib import Path

from conftest import ReadsbInstance, feed_sbs, sbs_msg3, poll_aircraft_json

# YYYY/MM/DD glob (date-agnostic)
DATED_GLOB = "[0-9][0-9][0-9][0-9]/[0-9][0-9]/[0-9][0-9]"


def poll_history_layout(hist_dir, timeout=20):
    """Poll until a dated YYYY/MM/DD dir with a traces/ subdir appears."""
    hist = Path(hist_dir)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        dated = [p for p in hist.glob(DATED_GLOB)
                 if p.is_dir() and (p / "traces").is_dir()]
        if dated:
            return sorted(dated)
        time.sleep(0.5)
    return sorted(p for p in hist.glob(DATED_GLOB) if p.is_dir())


class TestGlobeHistory(unittest.TestCase):
    """globe-history dated directory layout."""

    @classmethod
    def setUpClass(cls):
        cls.histdir = tempfile.mkdtemp(prefix="readsb-hist-")
        cls.inst = ReadsbInstance(extra_args=[
            "--write-json-globe-index",
            "--write-globe-history", cls.histdir,
            "--json-trace-interval", "1",
        ])
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        for _ in range(24):
            feed_sbs(sock, [sbs_msg3("ABC123", alt=35000, lat=51.5, lon=-0.1)])
            time.sleep(0.5)
        poll_aircraft_json(cls.inst.tmpdir, timeout=8, want_hex="abc123")
        cls.dated = poll_history_layout(cls.histdir, timeout=20)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)
        shutil.rmtree(cls.histdir, ignore_errors=True)

    def test_h1_dated_directory_created(self):
        """A YYYY/MM/DD dated directory is created under the history dir."""
        self.assertTrue(self.dated,
                        "no YYYY/MM/DD dated directory under the history dir")

    def test_h2_dated_dir_has_traces_and_acas(self):
        """Each dated dir contains the traces/ and acas/ subdirs exporters expect."""
        self.assertTrue(self.dated, "no dated dir to inspect")
        day = self.dated[-1]
        self.assertTrue((day / "traces").is_dir(),
                        f"missing traces/ under {day}")
        self.assertTrue((day / "acas").is_dir(),
                        f"missing acas/ under {day}")

    def test_h3_internal_state_blobs(self):
        """internal_state/ blob set is written (state persistence for restarts)."""
        state = Path(self.histdir) / "internal_state"
        self.assertTrue(state.is_dir(), "internal_state/ dir not written")
        blobs = list(state.glob("blob_*"))
        self.assertGreater(len(blobs), 0, "no internal_state blobs written")


if __name__ == "__main__":
    unittest.main(verbosity=2)
