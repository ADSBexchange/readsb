// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// mode_s_tests.c - unit tests for static decode functions in mode_s.c
//
// Tests for: decodeID13Field, decodeAC13Field, decodeAC12Field,
//            decodeMovementFieldV0, decodeMovementFieldV2

// We #include mode_s.c directly to access its static functions.
// This means we must provide stubs for all external symbols that
// mode_s.c (and its transitive includes) reference.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Provide the Modes global before including readsb.h (via mode_s.c)
#include "readsb.h"
#include "ais_charset.h"

// Linker stubs: mode_s.c calls these but we don't need them for decode tests
struct _Modes Modes;
void setExit(int __attribute__((unused)) arg) { }

// Stubs for functions called from the non-static parts of mode_s.c
int icaoFilterTest(uint32_t __attribute__((unused)) addr) { return 0; }
void icaoFilterAdd(uint32_t __attribute__((unused)) addr) { }
uint32_t icaoFilterTestFuzzy(uint32_t __attribute__((unused)) partial) { return 0; }
void decodeCommB(struct modesMessage __attribute__((unused)) *mm) { }

// ais_charset is defined in ais_charset.o but we link it, so no stub needed.
// modeAC_count / modeAC_match are declared extern in track.h
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

// Stubs for displayModesMessage dependencies (never called in tests)
char *sprint_uuid1(uint64_t __attribute__((unused)) id1, char *p) { return p; }
void printACASInfoShort(uint32_t __attribute__((unused)) addr,
                        unsigned char __attribute__((unused)) *MV,
                        struct aircraft __attribute__((unused)) *a,
                        struct modesMessage __attribute__((unused)) *mm,
                        int64_t __attribute__((unused)) now) { }

// Now pull in mode_s.c to get access to the static functions
// We must guard against double-including readsb.h (already included above)
// but the header guard handles that.
#include "mode_s.c"

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
        fprintf(stderr, "%s: FAIL: got 0x%X (%u), expected 0x%X (%u)\n", \
                tag, _g, _g, _e, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_FLOAT_NEAR(tag, got, expected, tol) do { \
    float _g = (got), _e = (expected), _t = (tol); \
    if (fabsf(_g - _e) > _t) { \
        fprintf(stderr, "%s: FAIL: got %.4f, expected %.4f (tol %.4f)\n", \
                tag, (double)_g, (double)_e, (double)_t); \
        failures++; \
    } \
} while(0)

// ---- testDecodeID13Field ----

static void testDecodeID13Field(void) {
    fprintf(stderr, "=== testDecodeID13Field ===\n");

    // All-zeros input -> output 0
    ASSERT_EQ_INT("ID13(0x0000)", decodeID13Field(0x0000), 0);

    // All 13 bits set (0x1FFF) -> check all output bits are set correctly
    // Input: bits 12..0 all set
    // Output should have: 0x0010|0x1000|0x0020|0x2000|0x0040|0x4000|
    //                     0x0100|0x0001|0x0200|0x0002|0x0400|0x0004
    // (bit 6 = X/M is commented out, so 0x0800 NOT set)
    int expected_all = 0x0010 | 0x1000 | 0x0020 | 0x2000 | 0x0040 | 0x4000 |
                       0x0100 | 0x0001 | 0x0200 | 0x0002 | 0x0400 | 0x0004;
    ASSERT_EQ_INT("ID13(0x1FFF)", decodeID13Field(0x1FFF), expected_all);

    // Individual bit tests: each input bit maps to a specific output bit
    struct { int in_bit; int out_bit; const char *label; } bit_map[] = {
        { 0x1000, 0x0010, "bit12=C1" },
        { 0x0800, 0x1000, "bit11=A1" },
        { 0x0400, 0x0020, "bit10=C2" },
        { 0x0200, 0x2000, "bit9=A2"  },
        { 0x0100, 0x0040, "bit8=C4"  },
        { 0x0080, 0x4000, "bit7=A4"  },
        // bit 6 (0x0040) = X/M is intentionally skipped
        { 0x0020, 0x0100, "bit5=B1"  },
        { 0x0010, 0x0001, "bit4=D1"  },
        { 0x0008, 0x0200, "bit3=B2"  },
        { 0x0004, 0x0002, "bit2=D2"  },
        { 0x0002, 0x0400, "bit1=B4"  },
        { 0x0001, 0x0004, "bit0=D4"  },
    };
    for (unsigned i = 0; i < sizeof(bit_map)/sizeof(bit_map[0]); i++) {
        char tag[64];
        snprintf(tag, sizeof(tag), "ID13 single %s", bit_map[i].label);
        ASSERT_EQ_INT(tag, decodeID13Field(bit_map[i].in_bit), bit_map[i].out_bit);
    }

    // Bit 6 (X/M = 0x0040) should be ignored (commented out in source)
    ASSERT_EQ_INT("ID13(0x0040) X/M ignored", decodeID13Field(0x0040), 0);

    fprintf(stderr, "testDecodeID13Field: done\n\n");
}

// ---- testDecodeAC13Field ----

static void testDecodeAC13Field(void) {
    fprintf(stderr, "=== testDecodeAC13Field ===\n");

    altitude_unit_t unit;
    unsigned q_bit;

    // Q-bit set (0x0010): altitude = n*25 - 1000
    // When Q-bit is set, M-bit (0x0040) clear:
    //   n = ((field & 0x1F80) >> 2) | ((field & 0x0020) >> 1) | (field & 0x000F)

    // Test: n=0 -> altitude = -1000
    // n=0 means all the extracted bits are 0; Q-bit set, rest zero
    // field = 0x0010 (just Q-bit)
    unit = UNIT_METERS; q_bit = 0;
    ASSERT_EQ_INT("AC13 Q=1 n=0", decodeAC13Field(0x0010, &unit, &q_bit), -1000);
    ASSERT_EQ_UINT("AC13 Q=1 n=0 unit", unit, UNIT_FEET);
    ASSERT_EQ_UINT("AC13 Q=1 n=0 q_bit", q_bit, 1);

    // Test: field = 0x1FBF (all bits except M and Q positions... but Q must be set)
    // Let's compute: field with Q-bit set and all other altitude bits set
    // Bits contributing to n: 0x1F80 (bits 12-7), 0x0020 (bit 5), 0x000F (bits 3-0)
    // All those set + Q-bit: 0x1F80 | 0x0020 | 0x000F | 0x0010 = 0x1FBF
    // n = (0x1F80 >> 2) | (0x0020 >> 1) | 0x000F = 0x07E0 | 0x0010 | 0x000F = 0x07FF = 2047
    // altitude = 2047*25 - 1000 = 51175 - 1000 = 50175
    unit = UNIT_METERS; q_bit = 0;
    ASSERT_EQ_INT("AC13 Q=1 max n", decodeAC13Field(0x1FBF, &unit, &q_bit), 50175);

    // Test: FL350 = 35000 ft -> n = (35000 + 1000) / 25 = 1440
    // n = 1440 = 0x5A0
    // Reverse: bits 10..5 of n -> field bits 12..7, bit 4 of n -> field bit 5, bits 3..0 of n -> field bits 3..0
    // n = 0x5A0 = 0101 1010 0000
    // field[12:7] = n[10:5] = 0x5A0 >> 5 = 0x2D -> field = 0x2D << 2 = 0x00B4 << shift...
    // Let me just construct it properly:
    // n = 1440 = 0b10110100000
    // n bits [10:5] = 101101 = 0x2D -> these go into field[12:7]: 0x2D << 7 = 0x1680
    // Wait, field[12:7] = bits 0x1F80, and n[10:5] are extracted via (field & 0x1F80) >> 2
    // So field_bits = n_bits << 2 for the upper part
    // n = ((F & 0x1F80) >> 2) | ((F & 0x0020) >> 1) | (F & 0x000F)
    // n[10:5] from (F & 0x1F80) >> 2, n[4] from (F & 0x0020) >> 1, n[3:0] from F & 0x000F
    // n = 1440 = 0x5A0 = binary 10110100000
    // n[10:5] = 10110 1 = 0x2D (bits 10 down to 5) = 45
    // n[4] = 0
    // n[3:0] = 0000
    // F = (45 << 7) | 0x0010 | (0 << 5) | 0x0000 = 0x1680 | 0x0010 = 0x1690
    // Wait: (F & 0x1F80) >> 2 should give n[10:5] shifted into position
    // Actually n = upper_part | mid_part | lower_part where:
    //   upper_part = (F & 0x1F80) >> 2  (6 bits from positions 12-7, shifted right by 2 -> positions 10-5)
    //   mid_part = (F & 0x0020) >> 1    (1 bit from position 5, shifted right by 1 -> position 4)
    //   lower_part = F & 0x000F         (4 bits from positions 3-0 -> positions 3-0)
    // So n is 11 bits: [10:5][4][3:0]
    // n=1440 = 0b10110100000
    // [10:5] = 101101 = 45
    // [4] = 0
    // [3:0] = 0000
    // F[12:7] = 45, F[5] = 0, F[3:0] = 0, F[4] = Q = 1
    // F = (45 << 7) | (0 << 5) | 0x0010 | 0 = 0x16A0...
    // 45 << 7 = 45 * 128 = 5760 = 0x1680
    // F = 0x1680 | 0x0010 = 0x1690
    unit = UNIT_METERS; q_bit = 0;
    int alt = decodeAC13Field(0x1690, &unit, &q_bit);
    ASSERT_EQ_INT("AC13 Q=1 FL350", alt, 35000);

    // M-bit set (0x0040): meters mode -> INVALID_ALTITUDE
    unit = UNIT_FEET; q_bit = 0;
    ASSERT_EQ_INT("AC13 M=1", decodeAC13Field(0x0040, &unit, &q_bit), INVALID_ALTITUDE);
    ASSERT_EQ_UINT("AC13 M=1 unit", unit, UNIT_METERS);

    // M-bit and Q-bit both set: M-bit takes precedence (checked first? No, M-bit is checked outer)
    // Actually looking at the code: if (!m_bit) is the outer check, so if m_bit is set we go to meters
    unit = UNIT_FEET; q_bit = 0;
    ASSERT_EQ_INT("AC13 M=1 Q=1", decodeAC13Field(0x0050, &unit, &q_bit), INVALID_ALTITUDE);
    ASSERT_EQ_UINT("AC13 M=1 Q=1 unit", unit, UNIT_METERS);

    // Q-bit clear, M-bit clear: Gillham mode through modeAToModeC(decodeID13Field(field))
    // Use a known good Gillham value. From unittests.c we know modeAToModeC(0x0010) = -8
    // decodeID13Field(?) = 0x0010 means we need input bit 12 set (0x1000 -> 0x0010)
    // So field = 0x1000, Q=0, M=0 -> Gillham path
    // decodeID13Field(0x1000) = 0x0010 (just C1)
    // modeAToModeC(0x0010) = -8
    // altitude = 100 * (-8) = -800
    unit = UNIT_METERS; q_bit = 0;
    ASSERT_EQ_INT("AC13 Gillham C1", decodeAC13Field(0x1000, &unit, &q_bit), -800);
    ASSERT_EQ_UINT("AC13 Gillham unit", unit, UNIT_FEET);
    ASSERT_EQ_UINT("AC13 Gillham q_bit=0", q_bit, 0);

    fprintf(stderr, "testDecodeAC13Field: done\n\n");
}

// ---- testDecodeAC12Field ----

static void testDecodeAC12Field(void) {
    fprintf(stderr, "=== testDecodeAC12Field ===\n");

    altitude_unit_t unit;
    unsigned q_bit;

    // Q-bit set (0x10): altitude = n*25 - 1000
    // n = ((field & 0x0FE0) >> 1) | (field & 0x000F)

    // Test: n=0 -> altitude = -1000; field = 0x0010 (just Q-bit)
    unit = UNIT_METERS; q_bit = 0;
    ASSERT_EQ_INT("AC12 Q=1 n=0", decodeAC12Field(0x0010, &unit, &q_bit), -1000);
    ASSERT_EQ_UINT("AC12 Q=1 n=0 unit", unit, UNIT_FEET);
    ASSERT_EQ_UINT("AC12 Q=1 n=0 q_bit", q_bit, 1);

    // Test: FL350 = 35000 ft -> n = (35000 + 1000) / 25 = 1440
    // n = 1440 = 0x5A0 = 0b10110100000
    // n[10:4] from (F & 0x0FE0) >> 1: need n[10:4] = 1011010 = 90
    // n[3:0] from F & 0x000F: need n[3:0] = 0000
    // F[11:5] = 90 -> F = 90 << 5 = 2880 = 0x0B40, plus Q-bit: 0x0B40 | 0x0010 = 0x0B50
    // Verify: n = (0x0B50 & 0x0FE0) >> 1 | (0x0B50 & 0x000F)
    //       = (0x0B40) >> 1 | 0 = 0x05A0 | 0 = 0x05A0 = 1440.
    unit = UNIT_METERS; q_bit = 0;
    ASSERT_EQ_INT("AC12 Q=1 FL350", decodeAC12Field(0x0B50, &unit, &q_bit), 35000);

    // Test: all altitude bits set + Q-bit
    // F & 0x0FE0 = 0x0FE0, F & 0x000F = 0x000F, + Q-bit: F = 0x0FFF
    // n = (0x0FE0 >> 1) | 0x000F = 0x07F0 | 0x000F = 0x07FF = 2047
    // altitude = 2047*25 - 1000 = 50175
    unit = UNIT_METERS; q_bit = 0;
    ASSERT_EQ_INT("AC12 Q=1 max n", decodeAC12Field(0x0FFF, &unit, &q_bit), 50175);

    // Q-bit clear: Gillham mode
    // n = ((field & 0x0FC0) << 1) | (field & 0x003F) with M=0 inserted at bit 6
    // Then decodeID13Field(n) -> modeAToModeC() -> *100
    // This is hard to test directly, but we can verify a known invalid case
    // All zeros (no Q, no data): decodeID13Field(0) = 0, modeAToModeC(0) should be INVALID
    // because C-field is all zeros
    unit = UNIT_METERS; q_bit = 0;
    ASSERT_EQ_INT("AC12 Gillham zeros", decodeAC12Field(0x0000, &unit, &q_bit), INVALID_ALTITUDE);

    fprintf(stderr, "testDecodeAC12Field: done\n\n");
}

// ---- testDecodeMovementFieldV0 ----

static void testDecodeMovementFieldV0(void) {
    fprintf(stderr, "=== testDecodeMovementFieldV0 ===\n");

    // movement=0: no data -> 0
    ASSERT_FLOAT_NEAR("V0 mv=0", decodeMovementFieldV0(0), 0.0f, 0.001f);

    // movement=1: stopped -> 0
    ASSERT_FLOAT_NEAR("V0 mv=1", decodeMovementFieldV0(1), 0.0f, 0.001f);

    // movement=2..8: 0.125 + (mv-2+0.5)*0.125 kt steps
    // mv=2: 0.125 + 0.5*0.125 = 0.125 + 0.0625 = 0.1875
    ASSERT_FLOAT_NEAR("V0 mv=2", decodeMovementFieldV0(2), 0.1875f, 0.001f);
    // mv=8: 0.125 + (8-2+0.5)*0.125 = 0.125 + 6.5*0.125 = 0.125 + 0.8125 = 0.9375
    ASSERT_FLOAT_NEAR("V0 mv=8", decodeMovementFieldV0(8), 0.9375f, 0.001f);

    // movement=9..12: 1 + (mv-9+0.5)*0.25 kt steps
    // mv=9: 1 + 0.5*0.25 = 1.125
    ASSERT_FLOAT_NEAR("V0 mv=9", decodeMovementFieldV0(9), 1.125f, 0.001f);
    // mv=12: 1 + (12-9+0.5)*0.25 = 1 + 3.5*0.25 = 1 + 0.875 = 1.875
    ASSERT_FLOAT_NEAR("V0 mv=12", decodeMovementFieldV0(12), 1.875f, 0.001f);

    // movement=13..38: 2 + (mv-13+0.5)*0.50 kt steps
    // mv=13: 2 + 0.5*0.50 = 2.25
    ASSERT_FLOAT_NEAR("V0 mv=13", decodeMovementFieldV0(13), 2.25f, 0.001f);
    // mv=38: 2 + (38-13+0.5)*0.50 = 2 + 25.5*0.50 = 2 + 12.75 = 14.75
    ASSERT_FLOAT_NEAR("V0 mv=38", decodeMovementFieldV0(38), 14.75f, 0.001f);

    // movement=39..93: 15 + (mv-39+0.5)*1 kt steps
    // mv=39: 15 + 0.5 = 15.5
    ASSERT_FLOAT_NEAR("V0 mv=39", decodeMovementFieldV0(39), 15.5f, 0.001f);
    // mv=93: 15 + (93-39+0.5)*1 = 15 + 54.5 = 69.5
    ASSERT_FLOAT_NEAR("V0 mv=93", decodeMovementFieldV0(93), 69.5f, 0.001f);

    // movement=94..108: 70 + (mv-94+0.5)*2 kt steps
    // mv=94: 70 + 0.5*2 = 71
    ASSERT_FLOAT_NEAR("V0 mv=94", decodeMovementFieldV0(94), 71.0f, 0.001f);
    // mv=108: 70 + (108-94+0.5)*2 = 70 + 14.5*2 = 70 + 29 = 99
    ASSERT_FLOAT_NEAR("V0 mv=108", decodeMovementFieldV0(108), 99.0f, 0.001f);

    // movement=109..123: 100 + (mv-109+0.5)*5 kt steps
    // mv=109: 100 + 0.5*5 = 102.5
    ASSERT_FLOAT_NEAR("V0 mv=109", decodeMovementFieldV0(109), 102.5f, 0.001f);
    // mv=123: 100 + (123-109+0.5)*5 = 100 + 14.5*5 = 100 + 72.5 = 172.5
    ASSERT_FLOAT_NEAR("V0 mv=123", decodeMovementFieldV0(123), 172.5f, 0.001f);

    // movement=124: >= 175kt -> 180
    ASSERT_FLOAT_NEAR("V0 mv=124", decodeMovementFieldV0(124), 180.0f, 0.001f);

    // movement=125..127: invalid -> 0
    ASSERT_FLOAT_NEAR("V0 mv=125", decodeMovementFieldV0(125), 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR("V0 mv=126", decodeMovementFieldV0(126), 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR("V0 mv=127", decodeMovementFieldV0(127), 0.0f, 0.001f);

    fprintf(stderr, "testDecodeMovementFieldV0: done\n\n");
}

// ---- testDecodeMovementFieldV2 ----

static void testDecodeMovementFieldV2(void) {
    fprintf(stderr, "=== testDecodeMovementFieldV2 ===\n");

    // movement=0: no data -> 0
    ASSERT_FLOAT_NEAR("V2 mv=0", decodeMovementFieldV2(0), 0.0f, 0.001f);

    // movement=1: stopped -> 0
    ASSERT_FLOAT_NEAR("V2 mv=1", decodeMovementFieldV2(1), 0.0f, 0.001f);

    // movement=2: 0.125/2 = 0.0625
    ASSERT_FLOAT_NEAR("V2 mv=2", decodeMovementFieldV2(2), 0.0625f, 0.001f);

    // movement=3..8: 0.125 + (mv-3+0.5)*0.875/6 kt steps
    // mv=3: 0.125 + 0.5 * 0.875/6 = 0.125 + 0.0729167 = 0.1979167
    ASSERT_FLOAT_NEAR("V2 mv=3", decodeMovementFieldV2(3), 0.125f + 0.5f * 0.875f / 6.0f, 0.001f);
    // mv=8: 0.125 + (8-3+0.5)*0.875/6 = 0.125 + 5.5*0.145833 = 0.125 + 0.802083 = 0.927083
    ASSERT_FLOAT_NEAR("V2 mv=8", decodeMovementFieldV2(8), 0.125f + 5.5f * 0.875f / 6.0f, 0.001f);

    // movement=9..12: same as V0: 1 + (mv-9+0.5)*0.25
    ASSERT_FLOAT_NEAR("V2 mv=9", decodeMovementFieldV2(9), 1.125f, 0.001f);
    ASSERT_FLOAT_NEAR("V2 mv=12", decodeMovementFieldV2(12), 1.875f, 0.001f);

    // movement=13..38: 2 + (mv-13+0.5)*0.50
    ASSERT_FLOAT_NEAR("V2 mv=13", decodeMovementFieldV2(13), 2.25f, 0.001f);
    ASSERT_FLOAT_NEAR("V2 mv=38", decodeMovementFieldV2(38), 14.75f, 0.001f);

    // movement=39..93: 15 + (mv-39+0.5)*1
    ASSERT_FLOAT_NEAR("V2 mv=39", decodeMovementFieldV2(39), 15.5f, 0.001f);
    ASSERT_FLOAT_NEAR("V2 mv=93", decodeMovementFieldV2(93), 69.5f, 0.001f);

    // movement=94..108: 70 + (mv-94+0.5)*2
    ASSERT_FLOAT_NEAR("V2 mv=94", decodeMovementFieldV2(94), 71.0f, 0.001f);
    ASSERT_FLOAT_NEAR("V2 mv=108", decodeMovementFieldV2(108), 99.0f, 0.001f);

    // movement=109..123: 100 + (mv-109+0.5)*5
    ASSERT_FLOAT_NEAR("V2 mv=109", decodeMovementFieldV2(109), 102.5f, 0.001f);
    ASSERT_FLOAT_NEAR("V2 mv=123", decodeMovementFieldV2(123), 172.5f, 0.001f);

    // movement=124: >= 175kt -> 180
    ASSERT_FLOAT_NEAR("V2 mv=124", decodeMovementFieldV2(124), 180.0f, 0.001f);

    // movement=125..127: invalid -> 0
    ASSERT_FLOAT_NEAR("V2 mv=125", decodeMovementFieldV2(125), 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR("V2 mv=126", decodeMovementFieldV2(126), 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR("V2 mv=127", decodeMovementFieldV2(127), 0.0f, 0.001f);

    fprintf(stderr, "testDecodeMovementFieldV2: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    modeACInit();

    testDecodeID13Field();
    testDecodeAC13Field();
    testDecodeAC12Field();
    testDecodeMovementFieldV0();
    testDecodeMovementFieldV2();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
