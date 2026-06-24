"""
State persistence round-trip integration test (--write-state).

The globe / re-api roles use --write-state so aircraft + traces survive a
restart. The 2024 upgrade's top operational risk was "state drift / corrupted
hexes" on re-api after upgrading. This test exercises the round-trip directly:
feed an aircraft, write state on exit, restart pointed at the same state dir,
and assert the aircraft (and its position) is restored without any new feed.

Two-phase, so it drives readsb subprocesses directly rather than using the
single-process ReadsbInstance harness.
"""

import shutil
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

from conftest import (
    READSB_BIN, get_free_port, wait_for_port, feed_sbs, sbs_msg3,
    poll_aircraft_json,
)

HEX = "abc123"
LAT = 51.5
LON = -0.1
ALT = 35000


def start_readsb(json_dir, state_dir, only_on_exit):
    """Start a readsb subprocess writing state to *state_dir*; return (proc, sbs_port)."""
    sbs_port = get_free_port()
    api_port = get_free_port()
    cmd = [
        str(READSB_BIN), "--net-only", "--quiet",
        "--write-json", json_dir,
        "--write-state", state_dir,
        "--net-sbs-in-port", str(sbs_port),
        "--net-api-port", str(api_port),
        "--auto-exit", "120",
    ]
    if only_on_exit:
        cmd.append("--write-state-only-on-exit")
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not wait_for_port(sbs_port):
        proc.kill(); proc.wait()
        raise RuntimeError("readsb failed to start")
    return proc, sbs_port


def stop(proc):
    """SIGTERM readsb (triggers the on-exit state write) and wait for it to flush."""
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait()


class TestStatePersistence(unittest.TestCase):
    """write-state -> restart -> aircraft + position restored."""

    @classmethod
    def setUpClass(cls):
        cls.state_dir = tempfile.mkdtemp(prefix="readsb-state-")
        cls.json1 = tempfile.mkdtemp(prefix="readsb-json1-")
        cls.json2 = tempfile.mkdtemp(prefix="readsb-json2-")

        # Phase 1: feed an aircraft with a position, then exit (writes state).
        p1, sbs_port = start_readsb(cls.json1, cls.state_dir, only_on_exit=True)
        sock = socket.create_connection(("127.0.0.1", sbs_port), timeout=5)
        for _ in range(12):
            feed_sbs(sock, [sbs_msg3(HEX, alt=ALT, lat=LAT, lon=LON)])
            time.sleep(0.3)
        cls.before = poll_aircraft_json(cls.json1, timeout=8, want_hex=HEX)
        sock.close()
        stop(p1)
        time.sleep(1.0)  # let the on-exit state write settle
        cls.state_files = [p.name for p in Path(cls.state_dir).rglob("*") if p.is_file()]

        # Phase 2: restart from the same state dir with NO feed.
        cls.p2, _ = start_readsb(cls.json2, cls.state_dir, only_on_exit=False)
        cls.after = poll_aircraft_json(cls.json2, timeout=10, want_hex=HEX)

    @classmethod
    def tearDownClass(cls):
        stop(cls.p2)
        for d in (cls.state_dir, cls.json1, cls.json2):
            shutil.rmtree(d, ignore_errors=True)

    def test_s1_state_written_on_exit(self):
        """State blobs are written to the state dir on exit."""
        blobs = [f for f in self.state_files if f.startswith("blob_")]
        self.assertGreater(len(blobs), 0,
                           f"no state blobs written; got {self.state_files}")

    def test_s2_aircraft_restored_after_restart(self):
        """The fed aircraft reappears after restart with no new feed."""
        self.assertIsNotNone(self.before, "aircraft was not present before restart")
        self.assertIsNotNone(self.after,
                             "aircraft.json never repopulated after restart")
        hexes = {a.get("hex") for a in self.after["aircraft"]}
        self.assertIn(HEX, hexes,
                      "aircraft not restored from state after restart")

    def test_s3_restored_position_intact(self):
        """Restored aircraft keeps its position (guards against state drift)."""
        self.assertIsNotNone(self.after)
        ac = next((a for a in self.after["aircraft"] if a.get("hex") == HEX), None)
        self.assertIsNotNone(ac, f"{HEX} not restored")
        self.assertIn("lat", ac, "restored aircraft lost its position")
        self.assertAlmostEqual(ac["lat"], LAT, places=2)
        self.assertAlmostEqual(ac["lon"], LON, places=2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
