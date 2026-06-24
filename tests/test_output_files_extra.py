"""
Extra output-file integration tests: --write-json-gzip and --write-prom.

Both are used by the production readsb roles (adsbexchange-app configs):
  * --write-json-gzip  -> aircraft.json.gz (tar1090 serves the gzipped variant)
  * --write-prom       -> a Prometheus text-format metrics file scraped by
                          node_exporter (every role writes one; the monitoring
                          contract). Written on the stats cycle (~10s after
                          start), so setUpClass waits for it.
"""

import gzip
import json
import os
import tempfile
import time
import unittest
from pathlib import Path

from conftest import ReadsbInstance, feed_sbs, sbs_msg3, poll_aircraft_json


def poll_file_nonempty(path, timeout=16):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if os.path.getsize(path) > 0:
                return Path(path).read_text()
        except OSError:
            pass
        time.sleep(0.5)
    return ""


class TestExtraOutputFiles(unittest.TestCase):
    """aircraft.json.gz + the Prometheus metrics file."""

    @classmethod
    def setUpClass(cls):
        fd, cls.prom_path = tempfile.mkstemp(suffix=".prom")
        os.close(fd)
        cls.inst = ReadsbInstance(
            extra_args=["--write-json-gzip", "--write-prom", cls.prom_path])
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        for _ in range(6):
            feed_sbs(sock, [sbs_msg3("ABC123", alt=35000, lat=51.5, lon=-0.1)])
            time.sleep(0.3)
        poll_aircraft_json(cls.inst.tmpdir, timeout=8, want_hex="abc123")
        cls.prom_text = poll_file_nonempty(cls.prom_path, timeout=16)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)
        os.unlink(cls.prom_path)

    # ---- --write-json-gzip ----

    def test_e1_aircraft_json_gz_written_and_valid(self):
        """aircraft.json.gz is written, gzip-compressed, and parses."""
        gz = Path(self.inst.tmpdir) / "aircraft.json.gz"
        self.assertTrue(gz.exists(), "aircraft.json.gz not written")
        self.assertEqual(gz.read_bytes()[:2], b"\x1f\x8b", "not gzip-compressed")
        data = json.loads(gzip.decompress(gz.read_bytes()))
        self.assertIn("aircraft", data)
        self.assertIn("now", data)

    def test_e2_gz_matches_plain_json(self):
        """aircraft.json.gz carries the same aircraft set as aircraft.json."""
        d = Path(self.inst.tmpdir)
        plain = json.loads((d / "aircraft.json").read_text())
        gzd = json.loads(gzip.decompress((d / "aircraft.json.gz").read_bytes()))
        self.assertEqual({a["hex"] for a in gzd["aircraft"]},
                         {a["hex"] for a in plain["aircraft"]})

    # ---- --write-prom ----

    def test_e3_prom_file_has_readsb_metrics(self):
        """The Prometheus file is written with readsb_ metrics."""
        self.assertTrue(self.prom_text, "prom file never populated")
        readsb_metrics = [l for l in self.prom_text.splitlines()
                          if l.startswith("readsb_")]
        self.assertGreater(len(readsb_metrics), 0, "no readsb_ metrics in prom file")

    def test_e4_prom_lines_are_valid_format(self):
        """Each metric line is `name[{labels}] <numeric value>` (node_exporter-parseable)."""
        sample = [l for l in self.prom_text.splitlines()
                  if l and not l.startswith("#")][:20]
        self.assertTrue(sample, "no metric lines")
        for line in sample:
            parts = line.rsplit(" ", 1)
            self.assertEqual(len(parts), 2, f"unparseable metric line: {line!r}")
            float(parts[1])  # value must be numeric; raises on failure


if __name__ == "__main__":
    unittest.main(verbosity=2)
