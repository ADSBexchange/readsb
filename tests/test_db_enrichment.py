"""
Aircraft-DB enrichment + military classification integration test (--db-file).

readsb enriches aircraft from a tar1090-db CSV (--db-file): registration (r),
type code (t), and dbFlags (bit 0 = military). tar1090 shows reg/type;
the leaderboard + globeMil tiles use the military bit. With --db-file-lt the
long description (desc), ownOp, and year are also emitted.

Two things to know about the format (learned the hard way):
  * the decompressed CSV must be > 1000 bytes or readsb rejects it
    (aircraft.c: "database file very small, bailing out");
  * every field is ';'-delimited INCLUDING the last, so each row needs a
    TRAILING ';' or the row is silently skipped before being indexed.
Rows: addr;registration;typeCode;dbFlagBits;typeLong;year;ownOp;
dbFlagBits is a '0'/'1' string, bit 0 (first char) = military.

readsb also classifies known military address ranges with no db entry (e.g.
German mil 0x3EA000-0x3EBFFF), but that only surfaces in aircraft.json when a
db-file is loaded (the reg/dbFlags JSON block is gated on Modes.db).

Fed via beast DF17 so the aircraft carry real ICAO addresses that match the db.
"""

import gzip
import json
import os
import socket
import tempfile
import time
import unittest

from conftest import ReadsbInstance, beast_frame, make_df17_position, wait_for_port

MIL_HEX = "abc123"      # in db, dbFlags bit0 = 1 (military)
CIVIL_HEX = "def456"    # in db, dbFlags = 0
RANGE_MIL_HEX = "3ea123"  # NOT in db; German military address range


def make_db_gz():
    """Write a gzipped tar1090-db-format CSV; return its path. Padded > 1000 bytes."""
    rows = [
        f"{MIL_HEX};N737AX;B738;1;Boeing 737-800;2015;TestAir;",
        f"{CIVIL_HEX};G-CIVL;A320;0;Airbus A320;2018;CivilAir;",
    ]
    for i in range(40):
        rows.append(f"{0x200000 + i:06x};N{i:05d};C172;0;Cessna 172 Skyhawk;2001;PadAir;")
    csv = ("\n".join(rows) + "\n").encode()
    fd, path = tempfile.mkstemp(suffix=".csv.gz")
    os.close(fd)
    with gzip.open(path, "wb") as f:
        f.write(csv)
    return path


def feed_positions(beast_port, hexes, rounds=15):
    sock = socket.create_connection(("127.0.0.1", beast_port), timeout=5)
    for _ in range(rounds):
        for hx in hexes:
            sock.sendall(beast_frame(make_df17_position(hx, 51.5, -0.1, 35000, 0)))
            sock.sendall(beast_frame(make_df17_position(hx, 51.5, -0.1, 35000, 1)))
        time.sleep(0.3)
    return sock


def poll_for_enriched(json_dir, want_hex, timeout=10):
    """Poll aircraft.json until *want_hex* appears carrying a registration."""
    from pathlib import Path
    path = Path(json_dir) / "aircraft.json"
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        if path.exists():
            try:
                data = json.loads(path.read_text())
                last = data
                ac = next((a for a in data["aircraft"] if a.get("hex") == want_hex), None)
                if ac and "r" in ac:
                    return data
            except (json.JSONDecodeError, OSError, KeyError):
                pass
        time.sleep(0.3)
    return last


class TestDbEnrichment(unittest.TestCase):
    """--db-file registration/type/dbFlags enrichment + military classification."""

    @classmethod
    def setUpClass(cls):
        cls.db_path = make_db_gz()
        cls.inst = ReadsbInstance(
            beast_in=True, extra_args=["--db-file", cls.db_path, "--db-file-lt"])
        cls.inst.__enter__()
        wait_for_port(cls.inst.beast_in_port)
        time.sleep(2.0)  # let the initial db load complete (sets Modes.db)
        cls.feeder = feed_positions(
            cls.inst.beast_in_port, [MIL_HEX, CIVIL_HEX, RANGE_MIL_HEX])
        cls.data = poll_for_enriched(cls.inst.tmpdir, MIL_HEX, timeout=10)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.feeder.close()
        except Exception:
            pass
        cls.inst.__exit__(None, None, None)
        os.unlink(cls.db_path)

    def _ac(self, hx):
        self.assertIsNotNone(self.data, "aircraft.json never appeared")
        ac = next((a for a in self.data["aircraft"] if a.get("hex") == hx), None)
        self.assertIsNotNone(ac, f"{hx} not in aircraft.json")
        return ac

    def test_d1_registration_and_type_enrichment(self):
        """db-file populates registration (r) and type code (t)."""
        ac = self._ac(MIL_HEX)
        self.assertEqual(ac.get("r"), "N737AX")
        self.assertEqual(ac.get("t"), "B738")

    def test_d2_military_flag_from_db(self):
        """dbFlags military bit (bit 0) is set for the military db entry."""
        ac = self._ac(MIL_HEX)
        self.assertIsInstance(ac.get("dbFlags"), int)
        self.assertEqual(ac["dbFlags"] & 1, 1, "military bit not set from db")

    def test_d3_civil_entry_not_military(self):
        """A civil db entry is enriched but not flagged military."""
        ac = self._ac(CIVIL_HEX)
        self.assertEqual(ac.get("r"), "G-CIVL")
        self.assertEqual(ac.get("t"), "A320")
        self.assertEqual(ac.get("dbFlags", 0) & 1, 0, "civil aircraft flagged military")

    def test_d4_hardcoded_military_range(self):
        """An address in a known military range is flagged even with no db entry."""
        ac = self._ac(RANGE_MIL_HEX)
        self.assertEqual(ac.get("dbFlags", 0) & 1, 1,
                         "hardcoded military-range address not flagged military")

    def test_d5_long_type_description(self):
        """--db-file-lt adds the long type description (desc)."""
        ac = self._ac(MIL_HEX)
        self.assertEqual(ac.get("desc"), "Boeing 737-800")


if __name__ == "__main__":
    unittest.main(verbosity=2)
