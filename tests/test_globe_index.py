"""
Globe-index output integration tests (--write-json-globe-index).

This is the tar1090 globe-view contract: with --write-json-globe-index, readsb
shards positioned aircraft into globe_<index>.json tiles by location and
advertises the grid + special tiles in receiver.json. Tiles are gzip-compressed
JSON (served by tar1090 with Content-Encoding: gzip) despite the .json suffix.

G1-G4: output present + correct with the flag.
G5-G6: negative control — no globe output without the flag.
"""

import gzip
import json
import time
import unittest
from pathlib import Path

from conftest import ReadsbInstance, feed_sbs, sbs_msg3, poll_aircraft_json


TILE_BBOX_KEYS = {"north", "south", "east", "west"}


def read_tile(path):
    """Read a globe_*.json tile (gzip-compressed despite the .json extension)."""
    raw = Path(path).read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return json.loads(raw)


def globe_json_tiles(json_dir):
    """All globe_<index>.json tile paths in *json_dir* (excludes binCraft/Mil)."""
    return sorted(
        p for p in Path(json_dir).iterdir()
        if p.name.startswith("globe_") and p.name.endswith(".json")
    )


def poll_globe_tile_for_hex(json_dir, want_hex, timeout=10):
    """Poll globe tiles until *want_hex* appears; return the containing tile dict."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for p in globe_json_tiles(json_dir):
            try:
                tile = read_tile(p)
            except (OSError, ValueError):
                continue
            if any(a.get("hex") == want_hex for a in tile.get("aircraft", [])):
                return tile
        time.sleep(0.3)
    return None


class TestGlobeIndex(unittest.TestCase):
    """globe_*.json tiles + receiver.json globe-index fields with the flag set."""

    HEX = "abc123"
    LAT = 51.5
    LON = -0.1

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(extra_args=["--write-json-globe-index"])
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        # Feed repeatedly so the position stays fresh across a tile-write cycle.
        for _ in range(12):
            feed_sbs(sock, [sbs_msg3(cls.HEX, alt=35000, lat=cls.LAT, lon=cls.LON)])
            time.sleep(0.2)
        # Confirm the aircraft is established with a position before asserting.
        poll_aircraft_json(cls.inst.tmpdir, timeout=8, want_hex=cls.HEX)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_g1_globe_tiles_written(self):
        """globe_*.json tiles are written when --write-json-globe-index is set."""
        tiles = globe_json_tiles(self.inst.tmpdir)
        self.assertGreater(len(tiles), 0, "no globe_*.json tiles written")

    def test_g2_tile_is_gzipped_json_with_schema(self):
        """A globe tile is gzip-compressed JSON with the expected tile schema."""
        tiles = globe_json_tiles(self.inst.tmpdir)
        self.assertTrue(tiles, "no globe tiles to inspect")
        self.assertEqual(tiles[0].read_bytes()[:2], b"\x1f\x8b",
                         "globe tile should be gzip-compressed")
        tile = read_tile(tiles[0])
        for key in ("aircraft", "globeIndex", "now", "messages",
                    "north", "south", "east", "west"):
            self.assertIn(key, tile, f"globe tile missing '{key}'")
        self.assertIsInstance(tile["aircraft"], list)

    def test_g3_receiver_advertises_globe_index(self):
        """receiver.json exposes globeIndexGrid + globeIndexSpecialTiles."""
        rec = json.loads((Path(self.inst.tmpdir) / "receiver.json").read_text())
        self.assertIn("globeIndexGrid", rec)
        self.assertIsInstance(rec["globeIndexGrid"], int)
        self.assertIn("globeIndexSpecialTiles", rec)
        special = rec["globeIndexSpecialTiles"]
        self.assertIsInstance(special, list)
        self.assertGreater(len(special), 0, "globeIndexSpecialTiles is empty")
        self.assertTrue(TILE_BBOX_KEYS.issubset(special[0].keys()),
                        f"special tile missing bbox keys: {special[0]}")

    def test_g4_aircraft_lands_in_covering_tile(self):
        """A positioned aircraft appears in the globe tile whose bbox contains it."""
        tile = poll_globe_tile_for_hex(self.inst.tmpdir, self.HEX, timeout=10)
        self.assertIsNotNone(tile, f"{self.HEX} not found in any globe tile")
        self.assertLessEqual(tile["south"], self.LAT, "tile south edge above aircraft")
        self.assertGreaterEqual(tile["north"], self.LAT, "tile north edge below aircraft")
        self.assertLessEqual(tile["west"], self.LON, "tile west edge east of aircraft")
        self.assertGreaterEqual(tile["east"], self.LON, "tile east edge west of aircraft")


class TestGlobeIndexDisabled(unittest.TestCase):
    """Negative control: without the flag, no globe output at all."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()  # no --write-json-globe-index
        cls.inst.__enter__()
        sock = cls.inst.feeder()
        for _ in range(4):
            feed_sbs(sock, [sbs_msg3("DEF456", alt=30000, lat=48.0, lon=2.3)])
            time.sleep(0.2)
        time.sleep(1.5)

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_g5_no_globe_tiles_without_flag(self):
        """No globe_*.json tiles are written without --write-json-globe-index."""
        tiles = globe_json_tiles(self.inst.tmpdir)
        self.assertEqual(tiles, [],
                         f"unexpected globe tiles: {[p.name for p in tiles]}")

    def test_g6_no_globe_index_fields_without_flag(self):
        """receiver.json omits globeIndexGrid without the flag."""
        rec = json.loads((Path(self.inst.tmpdir) / "receiver.json").read_text())
        self.assertNotIn("globeIndexGrid", rec)


if __name__ == "__main__":
    unittest.main(verbosity=2)
