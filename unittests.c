// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// unittests.c - unit tests for core pure functions
//
// Tests for: squawk conversions, Mode A indexing, coordinate validation,
// bit extraction, Mode A/C altitude decoding, fasthash

#include "readsb.h"
#include <assert.h>

// Linker stub: many headers reference this extern but our tests don't use it
struct _Modes Modes;

// ---- helpers ----

static int failures = 0;

#define ASSERT_EQ_UINT(tag, got, expected) do { \
    unsigned _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got 0x%X (%u), expected 0x%X (%u)\n", \
                tag, _g, _g, _e, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_INT(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_U64(tag, got, expected) do { \
    uint64_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got 0x%"PRIx64", expected 0x%"PRIx64"\n", \
                tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_U32(tag, got, expected) do { \
    uint32_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got 0x%"PRIx32", expected 0x%"PRIx32"\n", \
                tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

// ---- testSquawkConversions ----

static void testSquawkConversions(void) {
    fprintf(stderr, "=== testSquawkConversions ===\n");

    // Known squawk codes: hex-encoded BCD -> decimal
    // 0x7700 means digits 7,7,0,0 -> decimal 7700
    ASSERT_EQ_UINT("squawkHex2Dec(0x7700)", squawkHex2Dec(0x7700), 7700);
    ASSERT_EQ_UINT("squawkHex2Dec(0x7600)", squawkHex2Dec(0x7600), 7600);
    ASSERT_EQ_UINT("squawkHex2Dec(0x7500)", squawkHex2Dec(0x7500), 7500);
    ASSERT_EQ_UINT("squawkHex2Dec(0x1200)", squawkHex2Dec(0x1200), 1200);
    ASSERT_EQ_UINT("squawkHex2Dec(0x0000)", squawkHex2Dec(0x0000), 0);
    ASSERT_EQ_UINT("squawkHex2Dec(0x7777)", squawkHex2Dec(0x7777), 7777);
    ASSERT_EQ_UINT("squawkHex2Dec(0x0001)", squawkHex2Dec(0x0001), 1);
    ASSERT_EQ_UINT("squawkHex2Dec(0x0010)", squawkHex2Dec(0x0010), 10);
    ASSERT_EQ_UINT("squawkHex2Dec(0x0100)", squawkHex2Dec(0x0100), 100);
    ASSERT_EQ_UINT("squawkHex2Dec(0x1000)", squawkHex2Dec(0x1000), 1000);

    // Reverse: decimal -> hex-encoded BCD
    ASSERT_EQ_UINT("squawkDec2Hex(7700)", squawkDec2Hex(7700), 0x7700);
    ASSERT_EQ_UINT("squawkDec2Hex(7600)", squawkDec2Hex(7600), 0x7600);
    ASSERT_EQ_UINT("squawkDec2Hex(7500)", squawkDec2Hex(7500), 0x7500);
    ASSERT_EQ_UINT("squawkDec2Hex(1200)", squawkDec2Hex(1200), 0x1200);
    ASSERT_EQ_UINT("squawkDec2Hex(0)", squawkDec2Hex(0), 0x0000);
    ASSERT_EQ_UINT("squawkDec2Hex(7777)", squawkDec2Hex(7777), 0x7777);

    // Round-trip: dec -> hex -> dec
    unsigned testvals[] = {0, 1, 10, 100, 1000, 1200, 7500, 7600, 7700, 7777, 42, 333, 4567};
    for (unsigned i = 0; i < sizeof(testvals)/sizeof(testvals[0]); i++) {
        char tag[64];
        snprintf(tag, sizeof(tag), "roundtrip dec %u", testvals[i]);
        ASSERT_EQ_UINT(tag, squawkHex2Dec(squawkDec2Hex(testvals[i])), testvals[i]);
    }

    fprintf(stderr, "testSquawkConversions: done\n\n");
}

// ---- testModeAIndex ----

static void testModeAIndex(void) {
    fprintf(stderr, "=== testModeAIndex ===\n");

    // Spot checks
    ASSERT_EQ_UINT("modeAToIndex(0x0000)", modeAToIndex(0x0000), 0);
    ASSERT_EQ_UINT("modeAToIndex(0x7777)", modeAToIndex(0x7777), 4095);
    ASSERT_EQ_UINT("indexToModeA(0)", indexToModeA(0), 0x0000);
    ASSERT_EQ_UINT("indexToModeA(4095)", indexToModeA(4095), 0x7777);

    // Exhaustive round-trip: all 4096 indices
    int roundtrip_ok = 1;
    for (unsigned i = 0; i < 4096; i++) {
        unsigned modeA = indexToModeA(i);
        unsigned back = modeAToIndex(modeA);
        if (back != i) {
            fprintf(stderr, "modeAIndex roundtrip FAIL: index %u -> modeA 0x%04X -> index %u\n",
                    i, modeA, back);
            roundtrip_ok = 0;
            failures++;
            break;
        }
    }
    if (roundtrip_ok) {
        fprintf(stderr, "modeAIndex exhaustive roundtrip (4096 values): PASS\n");
    }

    fprintf(stderr, "testModeAIndex: done\n\n");
}

// ---- testBogusLatLon ----

static void testBogusLatLon(void) {
    fprintf(stderr, "=== testBogusLatLon ===\n");

    // Valid airports
    ASSERT_EQ_INT("Heathrow (51.47, -0.46)", bogus_lat_lon(51.47, -0.46), 0);
    ASSERT_EQ_INT("LAX (33.94, -118.41)", bogus_lat_lon(33.94, -118.41), 0);
    ASSERT_EQ_INT("Sydney (-33.86, 151.21)", bogus_lat_lon(-33.86, 151.21), 0);
    ASSERT_EQ_INT("Tokyo (35.68, 139.77)", bogus_lat_lon(35.68, 139.77), 0);
    ASSERT_EQ_INT("SFO (37.62, -122.38)", bogus_lat_lon(37.62, -122.38), 0);

    // Out of range: >= 90 lat or >= 180 lon
    ASSERT_EQ_INT("lat=90 (boundary)", bogus_lat_lon(90.0, 0.5), 1);
    ASSERT_EQ_INT("lat=-90 (boundary)", bogus_lat_lon(-90.0, 0.5), 1);
    ASSERT_EQ_INT("lon=180 (boundary)", bogus_lat_lon(0.5, 180.0), 1);
    ASSERT_EQ_INT("lon=-180 (boundary)", bogus_lat_lon(0.5, -180.0), 1);
    ASSERT_EQ_INT("lat=91", bogus_lat_lon(91.0, 0.0), 1);
    ASSERT_EQ_INT("lon=200", bogus_lat_lon(50.0, 200.0), 1);

    // Known bogus: null island and equator patterns
    ASSERT_EQ_INT("null island (0,0)", bogus_lat_lon(0.0, 0.0), 1);
    ASSERT_EQ_INT("equator+90 (0,90)", bogus_lat_lon(0.0, 90.0), 1);
    ASSERT_EQ_INT("equator-90 (0,-90)", bogus_lat_lon(0.0, -90.0), 1);

    // Near-origin exclusion zone (fabs < 0.01)
    ASSERT_EQ_INT("near origin (0.005, 0.005)", bogus_lat_lon(0.005, 0.005), 1);
    ASSERT_EQ_INT("near origin (-0.009, 0.009)", bogus_lat_lon(-0.009, 0.009), 1);

    // Just outside the 0.01 exclusion zone
    ASSERT_EQ_INT("just outside (0.02, 0.02)", bogus_lat_lon(0.02, 0.02), 0);
    ASSERT_EQ_INT("just outside (0.01, 5.0)", bogus_lat_lon(0.01, 5.0), 0);

    fprintf(stderr, "testBogusLatLon: done\n\n");
}

// ---- testGetbit ----

static void testGetbit(void) {
    fprintf(stderr, "=== testGetbit ===\n");

    // 0xA5 = 1010 0101, 0x3C = 0011 1100
    // bits 1-16 (1-indexed):
    // 1:1 2:0 3:1 4:0 5:0 6:1 7:0 8:1 | 9:0 10:0 11:1 12:1 13:1 14:1 15:0 16:0
    unsigned char data[2] = {0xA5, 0x3C};
    unsigned expected[16] = {1,0,1,0, 0,1,0,1, 0,0,1,1, 1,1,0,0};

    for (unsigned i = 0; i < 16; i++) {
        char tag[64];
        snprintf(tag, sizeof(tag), "getbit(data, %u)", i + 1);
        ASSERT_EQ_UINT(tag, getbit(data, i + 1), expected[i]);
    }

    fprintf(stderr, "testGetbit: done\n\n");
}

// ---- testGetbits ----

static void testGetbits(void) {
    fprintf(stderr, "=== testGetbits ===\n");

    // 0xDE 0xAD 0xBE 0xEF 0x12
    // binary: 11011110 10101101 10111110 11101111 00010010
    unsigned char data[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12};

    // nby=1: single bit extraction
    ASSERT_EQ_UINT("getbits(1,1) = bit 1 of 0xDE", getbits(data, 1, 1), 1);
    ASSERT_EQ_UINT("getbits(2,2) = bit 2 of 0xDE", getbits(data, 2, 2), 1);
    ASSERT_EQ_UINT("getbits(4,4) = bit 4 of 0xDE", getbits(data, 4, 4), 1);
    ASSERT_EQ_UINT("getbits(5,5) = bit 5 of 0xDE", getbits(data, 5, 5), 1);

    // nby=1: full byte aligned
    ASSERT_EQ_UINT("getbits(1,8) = 0xDE", getbits(data, 1, 8), 0xDE);
    ASSERT_EQ_UINT("getbits(9,16) = 0xAD", getbits(data, 9, 16), 0xAD);
    ASSERT_EQ_UINT("getbits(17,24) = 0xBE", getbits(data, 17, 24), 0xBE);
    ASSERT_EQ_UINT("getbits(25,32) = 0xEF", getbits(data, 25, 32), 0xEF);
    ASSERT_EQ_UINT("getbits(33,40) = 0x12", getbits(data, 33, 40), 0x12);

    // nby=1: partial byte within one byte
    // bits 2-7 of 0xDE (1101111_0) => 101111 = 0x2F = 47
    ASSERT_EQ_UINT("getbits(2,7)", getbits(data, 2, 7), 0x2F);

    // nby=2: cross-byte boundary
    // bits 5-12: last 4 of 0xDE (1110) + first 4 of 0xAD (1010) = 11101010 = 0xEA
    ASSERT_EQ_UINT("getbits(5,12) cross-byte", getbits(data, 5, 12), 0xEA);

    // bits 7-10: last 2 of 0xDE (10) + first 2 of 0xAD (10) = 1010 = 0x0A
    ASSERT_EQ_UINT("getbits(7,10) cross-byte", getbits(data, 7, 10), 0x0A);

    // nby=2: 16 bits spanning 2 bytes, unaligned
    // bits 4-19: 4 bits from byte0 + 8 bits byte1 + 4 bits byte2
    // Wait, that's nby=3. Let me pick bits 5-16: 4 bits from byte0 + 8 bits from byte1
    // = last 4 of 0xDE (1110) + 0xAD (10101101) = 1110 10101101 = 0xEAD
    ASSERT_EQ_UINT("getbits(5,16) 12bits nby=2", getbits(data, 5, 16), 0xEAD);

    // nby=3: spanning 3 bytes
    // bits 1-24: 0xDE 0xAD 0xBE = 0xDEADBE
    ASSERT_EQ_UINT("getbits(1,24) 3 bytes", getbits(data, 1, 24), 0xDEADBE);

    // bits 5-20: last 4 of DE + full AD + first 4 of BE
    // = 1110 10101101 1011 = 0xEADB
    ASSERT_EQ_UINT("getbits(5,20) nby=3", getbits(data, 5, 20), 0xEADB);

    // nby=4: spanning 4 bytes
    // bits 1-32: 0xDEADBEEF
    ASSERT_EQ_UINT("getbits(1,32) 4 bytes", getbits(data, 1, 32), 0xDEADBEEF);

    // bits 5-28: last 4 of DE + AD + BE + first 4 of EF
    // = 1110 10101101 10111110 1110 = 0xEADBEE
    ASSERT_EQ_UINT("getbits(5,28) nby=4", getbits(data, 5, 28), 0xEADBEE);

    // nby=5: spanning 5 bytes
    // bits 4-36: 5 bits from byte0 + 3 full bytes + 1 bit from byte4
    // bits 4-40 would be 37 bits (>32), so let's pick bits 5-36
    // last 4 of byte0 (1110) + byte1 (10101101) + byte2 (10111110) + byte3 (11101111) + first 4 of byte4 (0001)
    // = 1110 10101101 10111110 11101111 0001 = 0xEADBEEF1
    // That's 32 bits across 5 bytes.
    ASSERT_EQ_UINT("getbits(5,36) nby=5", getbits(data, 5, 36), 0xEADBEEF1);

    fprintf(stderr, "testGetbits: done\n\n");
}

// ---- testModeAToModeC ----

static void testModeAToModeC(void) {
    fprintf(stderr, "=== testModeAToModeC ===\n");

    modeACInit();

    // Invalid: D1 bit set (bit 0x0001 in the A field)
    ASSERT_EQ_INT("D1 bit set (0x0001)", modeAToModeC(0x0001), INVALID_ALTITUDE);
    ASSERT_EQ_INT("D1 bit set (0x7771)", modeAToModeC(0x7771), INVALID_ALTITUDE);

    // Invalid: C-field all zeros (bits 0x00F0 = 0)
    ASSERT_EQ_INT("C-field zeros (0x7700)", modeAToModeC(0x7700), INVALID_ALTITUDE);
    ASSERT_EQ_INT("C-field zeros (0x0200)", modeAToModeC(0x0200), INVALID_ALTITUDE);

    // Known Gillham altitude conversions
    // Mode A 0x0050 (only C1 set) should give -12 (hundred ft), i.e. -1200 ft offset relative
    // Let's test some known values:
    // 0x0010 -> only C1 -> OneHundreds: 0^7=7, (7&5)==5 so ^2 -> 5
    // FiveHundreds=0, alt = (0*500 + 5*100) - 1300 = -800 -> -8 (in 100s ft)
    ASSERT_EQ_INT("only C1 (0x0010)", modeAToModeC(0x0010), -8);

    // 0x0020 -> only C2 -> OneHundreds = 3, FiveHundreds=0 -> alt = (0*500 + 3*100) - 1300 = -1000
    ASSERT_EQ_INT("only C2 (0x0020)", modeAToModeC(0x0020), -10);

    // 0x0030 -> C1+C2 -> OneHundreds starts 0, ^7 -> 7, ^3 -> 4, (4&5)==4 not 5, stays 4
    // alt = (0*500 + 4*100) - 1300 = -900
    ASSERT_EQ_INT("C1+C2 (0x0030)", modeAToModeC(0x0030), -9);

    // Round-trip through modeCToModeA for all valid codes
    int valid_count = 0;
    int roundtrip_ok = 1;
    for (unsigned i = 0; i < 4096; i++) {
        unsigned modeA = indexToModeA(i);
        int modeC = modeAToModeC(modeA);
        if (modeC != INVALID_ALTITUDE) {
            valid_count++;
            unsigned back = modeCToModeA(modeC);
            if (back != modeA) {
                fprintf(stderr, "modeAC roundtrip FAIL: modeA 0x%04X -> modeC %d -> modeA 0x%04X\n",
                        modeA, modeC, back);
                roundtrip_ok = 0;
                failures++;
            }
        }
    }
    if (roundtrip_ok) {
        fprintf(stderr, "modeAC roundtrip (%d valid codes): PASS\n", valid_count);
    }

    // Sanity: Gillham code encodes ~1280 valid altitudes (-1000ft to 126700ft)
    if (valid_count < 1000) {
        fprintf(stderr, "modeAC valid count FAIL: only %d valid (expected >1000)\n", valid_count);
        failures++;
    } else {
        fprintf(stderr, "modeAC valid count (%d > 1000): PASS\n", valid_count);
    }

    fprintf(stderr, "testModeAToModeC: done\n\n");
}

// ---- testFasthash ----

static void testFasthash(void) {
    fprintf(stderr, "=== testFasthash ===\n");

    const char *input = "Hello, World!";
    size_t len = strlen(input);

    // Determinism: same input + seed -> same hash
    uint64_t h1 = fasthash64(input, len, 42);
    uint64_t h2 = fasthash64(input, len, 42);
    ASSERT_EQ_U64("fasthash64 determinism", h1, h2);

    uint32_t h3 = fasthash32(input, len, 42);
    uint32_t h4 = fasthash32(input, len, 42);
    ASSERT_EQ_U32("fasthash32 determinism", h3, h4);

    // Seed sensitivity: different seeds -> different hashes
    uint64_t h5 = fasthash64(input, len, 0);
    uint64_t h6 = fasthash64(input, len, 1);
    ASSERT_TRUE("fasthash64 seed sensitivity", h5 != h6);

    uint32_t h7 = fasthash32(input, len, 0);
    uint32_t h8 = fasthash32(input, len, 1);
    ASSERT_TRUE("fasthash32 seed sensitivity", h7 != h8);

    // fasthash32 is derived from fasthash64: h32 = h64 - (h64 >> 32)
    uint64_t h64 = fasthash64(input, len, 99);
    uint32_t expected32 = (uint32_t)(h64 - (h64 >> 32));
    uint32_t actual32 = fasthash32(input, len, 99);
    ASSERT_EQ_U32("fasthash32 = fasthash64 folded", actual32, expected32);

    // Input sensitivity: different inputs -> different hashes
    uint64_t ha = fasthash64("abc", 3, 0);
    uint64_t hb = fasthash64("abd", 3, 0);
    ASSERT_TRUE("fasthash64 input sensitivity", ha != hb);

    // Empty input
    uint64_t he1 = fasthash64("", 0, 0);
    uint64_t he2 = fasthash64("", 0, 0);
    ASSERT_EQ_U64("fasthash64 empty determinism", he1, he2);

    // Different lengths with same prefix
    uint64_t hl1 = fasthash64("test", 4, 0);
    uint64_t hl2 = fasthash64("test1", 5, 0);
    ASSERT_TRUE("fasthash64 length sensitivity", hl1 != hl2);

    // Test all switch-case tail paths (len & 7 = 1..7)
    const char *data = "abcdefghij";
    for (int tail = 1; tail <= 7; tail++) {
        uint64_t ht1 = fasthash64(data, 8 + tail, 0);
        uint64_t ht2 = fasthash64(data, 8 + tail, 0);
        char tag[64];
        snprintf(tag, sizeof(tag), "fasthash64 tail=%d determinism", tail);
        ASSERT_EQ_U64(tag, ht1, ht2);
    }

    fprintf(stderr, "testFasthash: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testSquawkConversions();
    testModeAIndex();
    testBogusLatLon();
    testGetbit();
    testGetbits();
    testModeAToModeC();
    testFasthash();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
