// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// globe_index_tests.c - unit tests for globe indexing and utility functions
//
// Copies the pure functions under test to avoid heavy linking requirements.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

// ---- Minimal types / constants from globe_index.h ----

#define GLOBE_INDEX_GRID 3
#define GLOBE_SPECIAL_INDEX 70
#define GLOBE_LAT_MULT (360 / GLOBE_INDEX_GRID + 1)
#define GLOBE_MIN_INDEX (1000)
#define GLOBE_MAX_INDEX (180 / GLOBE_INDEX_GRID * GLOBE_LAT_MULT + GLOBE_MIN_INDEX)

struct tile {
    int south;
    int west;
    int north;
    int east;
};

// Simulated global state for the functions under test
static struct tile *test_special_tiles;
static int test_special_tile_count;

// ---- Copied functions from globe_index.c ----

static int test_globe_index(double lat_in, double lon_in) {
    int grid = GLOBE_INDEX_GRID;
    int lat = grid * ((int) ((lat_in + 90) / grid)) - 90;
    int lon = grid * ((int) ((lon_in + 180) / grid)) - 180;

    struct tile *tiles = test_special_tiles;

    for (int i = 0; tiles[i].south != 0 || tiles[i].north != 0; i++) {
        struct tile tile = tiles[i];
        if (lat >= tile.south && lat < tile.north) {
            if (tile.west < tile.east && lon >= tile.west && lon < tile.east) {
                return i;
            }
            if (tile.west > tile.east && (lon >= tile.west || lon < tile.east)) {
                return i;
            }
        }
    }

    int i = (lat + 90) / grid;
    int j = (lon + 180) / grid;

    int res = (i * GLOBE_LAT_MULT + j + GLOBE_MIN_INDEX);
    if (res > GLOBE_MAX_INDEX) {
        return 0;
    }
    return res;
}

static int test_globe_index_index(int index) {
    double lat = ((index - GLOBE_MIN_INDEX) / GLOBE_LAT_MULT) * GLOBE_INDEX_GRID - 90;
    double lon = ((index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * GLOBE_INDEX_GRID - 180;
    return test_globe_index(lat, lon);
}

static int roundUp8(int value) {
    return ((value + 7) / 8) * 8;
}

// ---- Initialize special tiles (same order as init_globe_index) ----

static void init_test_tiles(void) {
    test_special_tiles = calloc(GLOBE_SPECIAL_INDEX, sizeof(struct tile));
    int count = 0;

    // Arctic
    test_special_tiles[count++] = (struct tile){60, -126, 90, 0};
    test_special_tiles[count++] = (struct tile){60, 0, 90, 150};
    // Alaska and Chukotka (wraps dateline)
    test_special_tiles[count++] = (struct tile){51, 150, 90, -126};
    // North Pacific
    test_special_tiles[count++] = (struct tile){9, 150, 51, -126};
    // Northern Canada
    test_special_tiles[count++] = (struct tile){51, -126, 60, -69};
    // Northwest USA
    test_special_tiles[count++] = (struct tile){45, -120, 51, -114};
    test_special_tiles[count++] = (struct tile){45, -114, 51, -102};
    test_special_tiles[count++] = (struct tile){45, -102, 51, -90};
    // Eastern Canada
    test_special_tiles[count++] = (struct tile){45, -90, 51, -75};
    test_special_tiles[count++] = (struct tile){45, -75, 51, -69};
    // Balkan
    test_special_tiles[count++] = (struct tile){42, 12, 48, 18};
    test_special_tiles[count++] = (struct tile){42, 18, 48, 24};
    // Poland
    test_special_tiles[count++] = (struct tile){48, 18, 54, 24};
    // Sweden
    test_special_tiles[count++] = (struct tile){54, 12, 60, 24};
    // Denmark
    test_special_tiles[count++] = (struct tile){54, 3, 60, 12};
    // Northern UK
    test_special_tiles[count++] = (struct tile){54, -9, 60, 3};
    // Golfo de Vizcaya
    test_special_tiles[count++] = (struct tile){42, -9, 48, 0};
    // West Russia
    test_special_tiles[count++] = (struct tile){42, 24, 51, 51};
    test_special_tiles[count++] = (struct tile){51, 24, 60, 51};
    // Central Russia
    test_special_tiles[count++] = (struct tile){30, 51, 60, 90};
    // East Russia
    test_special_tiles[count++] = (struct tile){30, 90, 60, 120};
    // Koreas and Japan
    test_special_tiles[count++] = (struct tile){30, 120, 39, 129};
    test_special_tiles[count++] = (struct tile){30, 129, 39, 138};
    test_special_tiles[count++] = (struct tile){30, 138, 39, 150};
    test_special_tiles[count++] = (struct tile){39, 120, 60, 150};
    // Vietnam
    test_special_tiles[count++] = (struct tile){9, 90, 21, 111};
    // South China
    test_special_tiles[count++] = (struct tile){21, 90, 30, 111};
    // South China and ICAO
    test_special_tiles[count++] = (struct tile){9, 111, 24, 129};
    test_special_tiles[count++] = (struct tile){24, 111, 30, 120};
    test_special_tiles[count++] = (struct tile){24, 120, 30, 129};
    // Pacific south of Japan
    test_special_tiles[count++] = (struct tile){9, 129, 30, 150};
    // Persian Gulf
    test_special_tiles[count++] = (struct tile){9, 51, 30, 69};
    // India
    test_special_tiles[count++] = (struct tile){9, 69, 30, 90};
    // South Atlantic / South Africa
    test_special_tiles[count++] = (struct tile){-90, -30, 9, 51};
    // Indian Ocean
    test_special_tiles[count++] = (struct tile){-90, 51, 9, 111};
    // Australia
    test_special_tiles[count++] = (struct tile){-90, 111, -18, 160};
    test_special_tiles[count++] = (struct tile){-18, 111, 9, 160};
    // South Pacific and NZ
    test_special_tiles[count++] = (struct tile){-90, 160, -42, -90};
    test_special_tiles[count++] = (struct tile){-42, 160, 9, -90};
    // North South America
    test_special_tiles[count++] = (struct tile){-9, -90, 9, -42};
    // South South America
    test_special_tiles[count++] = (struct tile){-90, -90, -9, -63};
    test_special_tiles[count++] = (struct tile){-21, -63, -9, -42};
    test_special_tiles[count++] = (struct tile){-90, -63, -21, -42};
    test_special_tiles[count++] = (struct tile){-90, -42, 9, -30};
    // Guatemala / Mexico
    test_special_tiles[count++] = (struct tile){9, -126, 33, -117};
    test_special_tiles[count++] = (struct tile){9, -117, 30, -102};
    // Western Gulf + east Mexico
    test_special_tiles[count++] = (struct tile){9, -102, 27, -90};
    // Eastern Gulf of Mexico
    test_special_tiles[count++] = (struct tile){24, -90, 30, -84};
    // South of Jamaica
    test_special_tiles[count++] = (struct tile){9, -90, 18, -69};
    // Cuba / Haiti
    test_special_tiles[count++] = (struct tile){18, -90, 24, -69};
    // Mediterranean
    test_special_tiles[count++] = (struct tile){36, 6, 42, 18};
    test_special_tiles[count++] = (struct tile){36, 18, 42, 30};
    // North Africa
    test_special_tiles[count++] = (struct tile){9, -9, 39, 6};
    test_special_tiles[count++] = (struct tile){9, 6, 36, 30};
    // Middle East
    test_special_tiles[count++] = (struct tile){9, 30, 42, 51};
    // West of Bermuda
    test_special_tiles[count++] = (struct tile){24, -75, 39, -69};
    // North Atlantic (duplicate)
    test_special_tiles[count++] = (struct tile){24, -75, 39, -69};
    test_special_tiles[count++] = (struct tile){9, -69, 30, -33};
    test_special_tiles[count++] = (struct tile){30, -69, 60, -33};
    test_special_tiles[count++] = (struct tile){9, -33, 30, -9};
    test_special_tiles[count++] = (struct tile){30, -33, 60, -9};

    test_special_tile_count = count;
}

// ---- test helpers ----

static int failures = 0;

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL: condition false\n", tag); \
        failures++; \
    } \
} while(0)

#define ASSERT_INT_EQ(tag, got, expected) do { \
    if ((got) != (expected)) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, (int)(got), (int)(expected)); \
        failures++; \
    } \
} while(0)

// ---- testRoundUp8 ----

static void testRoundUp8(void) {
    fprintf(stderr, "=== testRoundUp8 ===\n");

    ASSERT_INT_EQ("roundUp8(0)",  roundUp8(0), 0);
    ASSERT_INT_EQ("roundUp8(1)",  roundUp8(1), 8);
    ASSERT_INT_EQ("roundUp8(7)",  roundUp8(7), 8);
    ASSERT_INT_EQ("roundUp8(8)",  roundUp8(8), 8);
    ASSERT_INT_EQ("roundUp8(9)",  roundUp8(9), 16);
    ASSERT_INT_EQ("roundUp8(15)", roundUp8(15), 16);
    ASSERT_INT_EQ("roundUp8(16)", roundUp8(16), 16);
    ASSERT_INT_EQ("roundUp8(100)", roundUp8(100), 104);
    ASSERT_INT_EQ("roundUp8(256)", roundUp8(256), 256);

    fprintf(stderr, "testRoundUp8: done\n\n");
}

// ---- testGlobeIndex ----

static void testGlobeIndex(void) {
    fprintf(stderr, "=== testGlobeIndex ===\n");

    // Arctic point (70°N, -60°E) → should match a special tile (index < GLOBE_SPECIAL_INDEX)
    int idx = test_globe_index(70.0, -60.0);
    ASSERT_TRUE("arctic is special", idx < GLOBE_SPECIAL_INDEX);

    // Equator/prime meridian → regular grid index >= GLOBE_MIN_INDEX
    // (0,0) is in West Africa special tile "9, -9, 39, 6"
    // Use a point outside all special tiles: e.g., (5, -15) should be regular
    // Actually let's check: (0,0) snapped to grid is (0,0), which falls in tile {9, -9, 39, 6}? No: lat=0 < 9 = south.
    // So (0,0) snapped to (-90+grid*floor((0+90)/3)) = grid=3, (0+90)/3=30, lat=30*3-90=0, lon=0
    // lat=0, need south <= 0 < north. Tiles with south <= 0: {-90,...,9,...} tiles. Check {-90,-30,9,51}: south=-90, north=9, west=-30, east=51. lon=0 is in [-30,51). lat=0 is in [-90,9). Yes! So (0,0) matches a special tile.
    // Use point (3, -135) which is outside all special tiles
    // Actually (3,-135) lat=3, lon=-135. Snapped: lat=3, lon=-135. Special tiles with south<=3: many. Let's check dateline areas.
    // Easier: use testGlobeIndexGrid for nearby points and just validate no crash here.

    // Known grid calculation: lat=0, lon=0 falls in special tile
    idx = test_globe_index(0.0, 0.0);
    ASSERT_TRUE("equator origin valid", idx >= 0);

    // Non-special point: (24, -84) is outside all special tiles → regular grid
    // (verified by scanning all 3° cells: special tiles cover most of the globe)
    idx = test_globe_index(24.0, -84.0);
    ASSERT_TRUE("non-special is regular", idx >= GLOBE_MIN_INDEX);
    ASSERT_TRUE("non-special <= max", idx <= GLOBE_MAX_INDEX);

    // Verify no crash at poles and dateline
    idx = test_globe_index(89.9, 0.0);
    ASSERT_TRUE("north pole valid", idx >= 0 && idx <= GLOBE_MAX_INDEX);

    idx = test_globe_index(-89.9, 0.0);
    ASSERT_TRUE("south pole valid", idx >= 0 && idx <= GLOBE_MAX_INDEX);

    idx = test_globe_index(0.0, 179.9);
    ASSERT_TRUE("east dateline valid", idx >= 0 && idx <= GLOBE_MAX_INDEX);

    idx = test_globe_index(0.0, -179.9);
    ASSERT_TRUE("west dateline valid", idx >= 0 && idx <= GLOBE_MAX_INDEX);

    fprintf(stderr, "testGlobeIndex: done\n\n");
}

// ---- testGlobeIndexRoundTrip ----

static void testGlobeIndexRoundTrip(void) {
    fprintf(stderr, "=== testGlobeIndexRoundTrip ===\n");

    // For regular (non-special) indices, globe_index_index(idx) should either
    // return idx (true regular cell) or a special tile index (cell overlaps special region).
    // We scan all indices and verify: if round-trip returns idx, the forward
    // mapping is self-consistent.
    int total_regular = 0;
    int round_trip_ok = 0;
    int maps_to_special = 0;
    for (int idx = GLOBE_MIN_INDEX; idx <= GLOBE_MAX_INDEX; idx++) {
        int rt = test_globe_index_index(idx);
        if (rt == idx) {
            round_trip_ok++;
        } else if (rt < GLOBE_MIN_INDEX) {
            maps_to_special++;
        }
        total_regular++;
    }
    // We should have some regular indices that round-trip cleanly
    char tag[64];
    snprintf(tag, sizeof(tag), "roundtrip %d ok, %d special, %d total", round_trip_ok, maps_to_special, total_regular);
    ASSERT_TRUE(tag, round_trip_ok > 0);
    // Every index should either round-trip or map to special
    ASSERT_INT_EQ("all accounted", round_trip_ok + maps_to_special, total_regular);

    // Specific known non-special: (24, -84) should round-trip
    int idx = test_globe_index(24.0, -84.0);
    ASSERT_TRUE("specific is regular", idx >= GLOBE_MIN_INDEX);
    if (idx >= GLOBE_MIN_INDEX) {
        int rt = test_globe_index_index(idx);
        ASSERT_INT_EQ("specific roundtrip", rt, idx);
    }

    fprintf(stderr, "testGlobeIndexRoundTrip: done\n\n");
}

// ---- testGlobeIndexGrid ----

static void testGlobeIndexGrid(void) {
    fprintf(stderr, "=== testGlobeIndexGrid ===\n");

    // Points within the same 3° cell return the same index
    // Using a non-special region: ~24°N, -84°W (gap near Gulf of Mexico)
    int idx1 = test_globe_index(24.0, -84.0);
    int idx2 = test_globe_index(24.5, -83.5);
    int idx3 = test_globe_index(26.9, -82.1);
    ASSERT_INT_EQ("same cell 1-2", idx1, idx2);
    ASSERT_INT_EQ("same cell 1-3", idx1, idx3);

    // Adjacent cell returns different index
    // (24, -81) is also non-special, different lon cell
    int idx_adj2 = test_globe_index(24.0, -81.0);
    ASSERT_TRUE("adj lon diff", idx_adj2 != idx1);
    // Verify both are regular grid
    ASSERT_TRUE("adj lon regular", idx_adj2 >= GLOBE_MIN_INDEX);

    // Grid size validation: 180/3 = 60 latitude bands, 360/3 = 120 longitude bands
    // GLOBE_LAT_MULT = 360/3 + 1 = 121
    // GLOBE_MAX_INDEX should be 60 * 121 + 1000 = 8260
    ASSERT_INT_EQ("GLOBE_MAX_INDEX", GLOBE_MAX_INDEX, 60 * GLOBE_LAT_MULT + GLOBE_MIN_INDEX);

    fprintf(stderr, "testGlobeIndexGrid: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    init_test_tiles();

    testRoundUp8();
    testGlobeIndex();
    testGlobeIndexRoundTrip();
    testGlobeIndexGrid();

    free(test_special_tiles);

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
