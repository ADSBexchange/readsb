"""
Trace-file format integration tests (--write-json-globe-index traces).

readsb writes per-aircraft history traces to <json-dir>/traces/<last2hex>/
as trace_recent_<icao>.json and trace_full_<icao>.json (gzip-compressed
despite the .json suffix). Each file is {icao, timestamp, trace[]} where every
trace point is a fixed-length array:

  [0] time offset from `timestamp` (s)   [7] vertical rate
  [1] latitude                           [8] per-point detail object {type,...}
  [2] longitude                          [9] type string
  [3] baro altitude                      [10] geom altitude
  [4] ground speed                       [11] geom rate
  [5] track                              [12] ias
  [6] flags bitfield                     [13] roll

This is the contract tar1090 trace replay and push-scripts/export-traces parse;
pinning the point-array length catches any upstream layout change at the exact
commit that introduces it.
"""

import gzip
import json
import time
import unittest
from pathlib import Path

from conftest import ReadsbInstance, feed_sbs, sbs_msg3, poll_aircraft_json

# Observed trace-point array length for readsb 3.14.1631 (the reconciliation
# baseline). A change here breaks downstream trace parsers — treat as a gate.
TRACE_POINT_LEN = 14


def read_trace(path):
    """Read a trace_*.json file (gzip-compressed despite the .json extension)."""
    raw = Path(path).read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return json.loads(raw)


def find_trace_files(json_dir, icao):
    """Return (recent_path, full_path) for *icao*, or (None, None) if absent."""
    traces = Path(json_dir) / "traces"
    recent = next(traces.rglob(f"trace_recent_{icao}.json"), None) if traces.exists() else None
    full = next(traces.rglob(f"trace_full_{icao}.json"), None) if traces.exists() else None
    return recent, full


def poll_trace_files(json_dir, icao, timeout=20):
    """Poll until both trace files exist and parse with >=1 point."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        recent, full = find_trace_files(json_dir, icao)
        if recent and full:
            try:
                if read_trace(recent).get("trace") and read_trace(full).get("trace"):
                    return recent, full
            except (OSError, ValueError):
                pass
        time.sleep(0.5)
    return find_trace_files(json_dir, icao)


class TestTraceFiles(unittest.TestCase):
    """trace_recent / trace_full files + point-array contract."""

    HEX = "abc123"
    LAT = 51.5
    LON = -0.1
    ALT = 35000

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            extra_args=["--write-json-globe-index", "--json-trace-interval", "1"],
        )
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        # Static position (accepted by the speed-plausibility gate); a trace point
        # is recorded ~every --json-trace-interval, so points accumulate over time.
        for _ in range(24):
            feed_sbs(sock, [sbs_msg3(cls.HEX, alt=cls.ALT, lat=cls.LAT, lon=cls.LON,
                                     gs=450, track=90)])
            time.sleep(0.5)
        poll_aircraft_json(cls.inst.tmpdir, timeout=8, want_hex=cls.HEX)
        cls.recent, cls.full = poll_trace_files(cls.inst.tmpdir, cls.HEX, timeout=20)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_t1_trace_files_written(self):
        """trace_recent and trace_full files are written under traces/<last2hex>/."""
        self.assertIsNotNone(self.recent, "trace_recent file not written")
        self.assertIsNotNone(self.full, "trace_full file not written")
        # sharded by the last byte of the ICAO hex
        self.assertEqual(self.recent.parent.name, self.HEX[-2:])

    def test_t2_trace_files_gzipped(self):
        """Trace files are gzip-compressed (tar1090 serves them so)."""
        self.assertEqual(self.recent.read_bytes()[:2], b"\x1f\x8b")
        self.assertEqual(self.full.read_bytes()[:2], b"\x1f\x8b")

    def test_t3_trace_root_structure(self):
        """Trace root is {icao, timestamp, trace[]} with our aircraft + a base time."""
        data = read_trace(self.full)
        self.assertEqual(data.get("icao"), self.HEX)
        self.assertIn("timestamp", data)
        self.assertIsInstance(data["timestamp"], (int, float))
        self.assertGreater(data["timestamp"], 1577836800)  # after 2020
        self.assertIsInstance(data.get("trace"), list)
        self.assertGreater(len(data["trace"]), 0, "trace has no points")

    def test_t4_trace_point_array_contract(self):
        """Each trace point is the fixed-length array; pos fields match what we fed."""
        data = read_trace(self.recent)
        point = data["trace"][0]
        self.assertIsInstance(point, list)
        self.assertEqual(len(point), TRACE_POINT_LEN,
                         f"trace point array length changed (downstream parsers "
                         f"depend on it): got {len(point)}, expected {TRACE_POINT_LEN}")
        self.assertIsInstance(point[0], (int, float))          # [0] time offset
        self.assertAlmostEqual(point[1], self.LAT, places=2)   # [1] latitude
        self.assertAlmostEqual(point[2], self.LON, places=2)   # [2] longitude
        self.assertEqual(point[3], self.ALT)                   # [3] baro altitude
        self.assertIsInstance(point[8], dict)                  # [8] per-point detail
        self.assertIsInstance(point[9], str)                   # [9] type string


if __name__ == "__main__":
    unittest.main(verbosity=2)
