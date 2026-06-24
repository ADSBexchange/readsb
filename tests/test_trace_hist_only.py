"""
Trace hist-only mode integration test (--json-trace-hist-only 3).

The hrtraces and globe-history roles run with --json-trace-hist-only 3, which
means "don't write recent(1) or full(2) traces to /run — archive to
write-globe-history only". This is the opposite of the default (test_trace_files
covers traces being written to /run). Here we pin that the flag suppresses the
/run trace files while the aircraft is still tracked normally.
"""

import time
import unittest
from pathlib import Path

from conftest import ReadsbInstance, feed_sbs, sbs_msg3, poll_aircraft_json


class TestTraceHistOnly(unittest.TestCase):
    """--json-trace-hist-only 3 suppresses /run trace files."""

    HEX = "abc123"

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(extra_args=[
            "--write-json-globe-index",
            "--json-trace-interval", "1",
            "--json-trace-hist-only", "3",
        ])
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        for _ in range(20):
            feed_sbs(sock, [sbs_msg3(cls.HEX, alt=35000, lat=51.5, lon=-0.1)])
            time.sleep(0.3)
        cls.data = poll_aircraft_json(cls.inst.tmpdir, timeout=8, want_hex=cls.HEX)
        time.sleep(2)  # give any (suppressed) trace writes a chance to appear

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_t1_no_run_trace_files(self):
        """With hist-only 3, no trace_recent/full files are written under /run."""
        traces = Path(self.inst.tmpdir) / "traces"
        run_traces = list(traces.rglob("trace_*.json")) if traces.exists() else []
        self.assertEqual(run_traces, [],
                         f"hist-only 3 should suppress /run traces; got {[p.name for p in run_traces]}")

    def test_t2_aircraft_still_tracked(self):
        """The aircraft is still tracked/positioned (the feed path is unaffected)."""
        self.assertIsNotNone(self.data, "aircraft.json never appeared")
        ac = next((a for a in self.data["aircraft"] if a.get("hex") == self.HEX), None)
        self.assertIsNotNone(ac, f"{self.HEX} not tracked")
        self.assertIn("lat", ac, "aircraft has no position")


if __name__ == "__main__":
    unittest.main(verbosity=2)
