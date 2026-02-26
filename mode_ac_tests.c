// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// mode_ac_tests.c - unit tests for functions in mode_ac.c
//
// Uses #include "mode_ac.c" to access static functions directly.
// Provides linker stubs for external symbols that mode_ac.c references.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];
void setExit(int __attribute__((unused)) arg) { }

// ---- Include source under test ----

#include "mode_ac.c"

// ---- test helpers ----

static int failures = 0;

#define ASSERT_EQ_INT(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_UINT(tag, got, expected) do { \
    unsigned _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got 0x%04X (%u), expected 0x%04X (%u)\n", \
                tag, _g, _g, _e, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

#define ASSERT_NEQ_INT(tag, got, not_expected) do { \
    int _g = (got), _ne = (not_expected); \
    if (_g == _ne) { \
        fprintf(stderr, "%s: FAIL: got %d, expected != %d\n", tag, _g, _ne); \
        failures++; \
    } \
} while(0)

// ---- testModeACInit ----

static void testModeACInit(void) {
    fprintf(stderr, "=== testModeACInit ===\n");

    // Init should populate tables
    modeACInit();

    // Squawk 0x0110 (B1+C1 set) should produce a valid altitude
    int mc = modeAToModeC(0x0110);
    ASSERT_NEQ_INT("init 0110 valid", mc, INVALID_ALTITUDE);

    // modeCToModeA should return non-zero for valid mode C values
    unsigned ma = modeCToModeA(0);
    ASSERT_TRUE("init mC2mA(0) nonzero", ma != 0);

    fprintf(stderr, "testModeACInit: done\n\n");
}

// ---- testModeAToModeC ----

static void testModeAToModeC(void) {
    fprintf(stderr, "=== testModeAToModeC ===\n");

    // 0x0000 has D1..D4=0 which means C1..C4=0, which is invalid
    // (C1,C2,C4 cannot all be zero)
    ASSERT_EQ_INT("mA2mC 0x0000", modeAToModeC(0x0000), INVALID_ALTITUDE);

    // Squawk 0x7700 (emergency): should produce some valid altitude
    int mc7700 = modeAToModeC(0x7700);
    // 0x7700 may or may not be valid depending on bit patterns
    // Just verify the function doesn't crash and returns something
    (void)mc7700;

    // Squawk with invalid index (>= 4096) should return INVALID_ALTITUDE
    // modeAToIndex(0xFFFF) should be >= 4096 due to the bit extraction
    ASSERT_EQ_INT("mA2mC 0xFFFF", modeAToModeC(0xFFFF), INVALID_ALTITUDE);

    // Try a valid squawk: 0x0020 (only C2 set)
    int mc0020 = modeAToModeC(0x0020);
    ASSERT_NEQ_INT("mA2mC 0x0020 valid", mc0020, INVALID_ALTITUDE);

    fprintf(stderr, "testModeAToModeC: done\n\n");
}

// ---- testModeCToModeA ----

static void testModeCToModeA(void) {
    fprintf(stderr, "=== testModeCToModeA ===\n");

    // Round-trip test: for valid mode A squawks, converting to mode C
    // and back should yield the original squawk
    int round_trip_ok = 0;
    int round_trip_fail = 0;

    for (unsigned i = 0; i < 4096; ++i) {
        unsigned modeA = indexToModeA(i);
        int modeC = modeAToModeC(modeA);
        if (modeC == INVALID_ALTITUDE)
            continue;

        unsigned back = modeCToModeA(modeC);
        if (back == modeA) {
            round_trip_ok++;
        } else {
            if (round_trip_fail < 5) {
                fprintf(stderr, "  round-trip fail: modeA=0x%04X -> modeC=%d -> back=0x%04X\n",
                        modeA, modeC, back);
            }
            round_trip_fail++;
        }
    }

    ASSERT_TRUE("mC2mA round-trip count", round_trip_ok > 0);
    ASSERT_EQ_INT("mC2mA round-trip fails", round_trip_fail, 0);
    fprintf(stderr, "  round-trip: %d ok, %d fail\n", round_trip_ok, round_trip_fail);

    // Out of range mode C returns 0
    ASSERT_EQ_UINT("mC2mA out of range neg", modeCToModeA(-14), 0);
    ASSERT_EQ_UINT("mC2mA out of range high", modeCToModeA(5000), 0);

    fprintf(stderr, "testModeCToModeA: done\n\n");
}

// ---- testInternalModeAToModeC ----

static void testInternalModeAToModeC(void) {
    fprintf(stderr, "=== testInternalModeAToModeC ===\n");

    // D1 set is illegal (bit 0x0001)
    ASSERT_EQ_INT("internal D1 set", internalModeAToModeC(0x0011), INVALID_ALTITUDE);

    // C1..C4 all zero is illegal (bits 0x00F0 == 0)
    ASSERT_EQ_INT("internal C zero", internalModeAToModeC(0x1000), INVALID_ALTITUDE);

    // Invalid higher bits (0xFFFF8889 mask)
    ASSERT_EQ_INT("internal bad bits", internalModeAToModeC(0x80000000), INVALID_ALTITUDE);

    // C1 only (0x0010): OneHundreds = 7 -> swap to 5
    int mc = internalModeAToModeC(0x0010);
    ASSERT_NEQ_INT("internal C1 only valid", mc, INVALID_ALTITUDE);

    // C1+C2 (0x0030): OneHundreds = 7^3 = 4
    mc = internalModeAToModeC(0x0030);
    ASSERT_NEQ_INT("internal C1C2 valid", mc, INVALID_ALTITUDE);

    fprintf(stderr, "testInternalModeAToModeC: done\n\n");
}

// ---- testDecodeModeAMessage ----

static void testDecodeModeAMessage(void) {
    fprintf(stderr, "=== testDecodeModeAMessage ===\n");

    struct modesMessage mm;
    memset(&mm, 0, sizeof(mm));

    // Squawk 0x7700 (emergency)
    decodeModeAMessage(&mm, 0x7700);

    ASSERT_EQ_INT("mm source", mm.source, SOURCE_MODE_AC);
    ASSERT_EQ_INT("mm addrtype", mm.addrtype, ADDR_MODE_A);
    ASSERT_EQ_INT("mm msgtype", mm.msgtype, DFTYPE_MODEAC);
    ASSERT_EQ_INT("mm msgbits", mm.msgbits, 16);

    // addr should have MODES_NON_ICAO_ADDRESS set and ident bit masked
    ASSERT_TRUE("mm addr non-icao", (mm.addr & MODES_NON_ICAO_ADDRESS) != 0);

    // squawkHex should be ModeA & 0x7777
    ASSERT_EQ_UINT("mm squawkHex", mm.squawkHex, 0x7700 & 0x7777);

    // squawkDec should convert hex squawk to decimal
    ASSERT_EQ_UINT("mm squawkDec", mm.squawkDec, squawkHex2Dec(0x7700 & 0x7777));

    ASSERT_EQ_INT("mm squawk_valid", mm.squawk_valid, 1);

    // SPI flag: bit 0x0080 of ModeA. 0x7700 & 0x0080 = 0
    ASSERT_EQ_INT("mm spi", mm.spi, 0);
    ASSERT_EQ_INT("mm spi_valid", mm.spi_valid, 1);

    // Test with SPI set (bit 0x0080)
    memset(&mm, 0, sizeof(mm));
    decodeModeAMessage(&mm, 0x7780);
    ASSERT_EQ_INT("mm spi set", mm.spi, 1);
    // When SPI is set, altitude should not be decoded
    ASSERT_EQ_INT("mm no alt with spi", mm.baro_alt_valid, 0);

    fprintf(stderr, "testDecodeModeAMessage: done\n\n");
}

// ---- testIndexToModeARoundTrip ----

static void testIndexToModeARoundTrip(void) {
    fprintf(stderr, "=== testIndexToModeARoundTrip ===\n");

    int ok = 0;
    int fail = 0;

    for (unsigned i = 0; i < 4096; ++i) {
        unsigned modeA = indexToModeA(i);
        unsigned back = modeAToIndex(modeA);
        if (back == i) {
            ok++;
        } else {
            if (fail < 5) {
                fprintf(stderr, "  fail: index=%u -> modeA=0x%04X -> back=%u\n",
                        i, modeA, back);
            }
            fail++;
        }
    }

    ASSERT_EQ_INT("i2mA round-trip ok", ok, 4096);
    ASSERT_EQ_INT("i2mA round-trip fail", fail, 0);

    fprintf(stderr, "testIndexToModeARoundTrip: done\n\n");
}

// ---- testSquawkHex2Dec ----

static void testSquawkHex2Dec(void) {
    fprintf(stderr, "=== testSquawkHex2Dec ===\n");

    ASSERT_EQ_UINT("hex2dec 0x1200", squawkHex2Dec(0x1200), 1200);
    ASSERT_EQ_UINT("hex2dec 0x7700", squawkHex2Dec(0x7700), 7700);
    ASSERT_EQ_UINT("hex2dec 0x0000", squawkHex2Dec(0x0000), 0);
    ASSERT_EQ_UINT("hex2dec 0x7777", squawkHex2Dec(0x7777), 7777);
    ASSERT_EQ_UINT("hex2dec 0x1234", squawkHex2Dec(0x1234), 1234);
    ASSERT_EQ_UINT("hex2dec 0x0001", squawkHex2Dec(0x0001), 1);

    fprintf(stderr, "testSquawkHex2Dec: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testModeACInit();
    testModeAToModeC();
    testModeCToModeA();
    testInternalModeAToModeC();
    testDecodeModeAMessage();
    testIndexToModeARoundTrip();
    testSquawkHex2Dec();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
