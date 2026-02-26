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

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
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

// ---- setbits_me helper ----
// Set bits [first..last] (1-indexed, MSB) in a 7-byte ME field to `value`.
// Mirror of getbits/getbit bit numbering from mode_s.h.

static void setbits_me(unsigned char *me, unsigned first, unsigned last, unsigned value) {
    for (unsigned i = first; i <= last; i++) {
        unsigned bi = i - 1;
        unsigned by = bi >> 3;
        unsigned mask = 1u << (7 - (bi & 7));
        unsigned bit_pos = last - i; // bit position in value (0 = LSB)
        if (value & (1u << bit_pos))
            me[by] |= mask;
        else
            me[by] &= ~mask;
    }
}

// ---- testDecodeESIdentAndCategory ----

static void testDecodeESIdentAndCategory(void) {
    fprintf(stderr, "=== testDecodeESIdentAndCategory ===\n");

    // Test: encode "BAW256  " and verify decode
    // AIS charset: '@'=0, 'A'=1, 'B'=2, ..., 'Z'=26, ' '=32, '0'=48, ..., '9'=57
    // 'B'=2, 'A'=1, 'W'=23, '2'=50, '5'=53, '6'=54, ' '=32, ' '=32
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 4; // Aircraft ident, category set A

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        // ME bits 6-8: mesub
        setbits_me(me, 6, 8, 1); // mesub = 1
        // ME bits 9-14: char 0 = 'B' = 2
        setbits_me(me, 9, 14, 2);
        // ME bits 15-20: char 1 = 'A' = 1
        setbits_me(me, 15, 20, 1);
        // ME bits 21-26: char 2 = 'W' = 23
        setbits_me(me, 21, 26, 23);
        // ME bits 27-32: char 3 = '2' = 50
        setbits_me(me, 27, 32, 50);
        // ME bits 33-38: char 4 = '5' = 53
        setbits_me(me, 33, 38, 53);
        // ME bits 39-44: char 5 = '6' = 54
        setbits_me(me, 39, 44, 54);
        // ME bits 45-50: char 6 = ' ' = 32
        setbits_me(me, 45, 50, 32);
        // ME bits 51-56: char 7 = ' ' = 32
        setbits_me(me, 51, 56, 32);
        memcpy(mm.ME, me, sizeof(me));

        decodeESIdentAndCategory(&mm);

        ASSERT_EQ_INT("ident callsign_valid", mm.callsign_valid, 1);
        ASSERT_TRUE("ident callsign BAW256",
                     strncmp(mm.callsign, "BAW256  ", 8) == 0);
        ASSERT_EQ_INT("ident mesub", mm.mesub, 1);
        // category = ((0x0E - metype) << 4) | mesub = ((14-4)<<4)|1 = 0xA1
        ASSERT_EQ_UINT("ident category", mm.category, 0xA1);
        ASSERT_EQ_INT("ident category_valid", mm.category_valid, 1);
    }

    // Test: all spaces is valid
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 1;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 0); // mesub = 0
        for (int i = 0; i < 8; i++)
            setbits_me(me, 9 + i * 6, 14 + i * 6, 32); // ' '
        memcpy(mm.ME, me, sizeof(me));

        decodeESIdentAndCategory(&mm);
        ASSERT_EQ_INT("ident allspace valid", mm.callsign_valid, 1);
    }

    // Test: invalid char (index 0 = '@') is still technically valid per the function
    // '@' is in the valid char list: callsign[i] == '@'
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 2;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 1);
        // All chars set to '@' (index 0)
        for (int i = 0; i < 8; i++)
            setbits_me(me, 9 + i * 6, 14 + i * 6, 0);
        memcpy(mm.ME, me, sizeof(me));

        decodeESIdentAndCategory(&mm);
        // '@' is in the valid char list
        ASSERT_EQ_INT("ident at_sign valid", mm.callsign_valid, 1);
    }

    fprintf(stderr, "testDecodeESIdentAndCategory: done\n\n");
}

// ---- testDecodeESAirborneVelocity_ground ----

static void testDecodeESAirborneVelocity_ground(void) {
    fprintf(stderr, "=== testDecodeESAirborneVelocity_ground ===\n");

    // Subtype 1: E/W = 100kt east, N/S = 100kt north
    // gs = sqrt(100^2 + 100^2) ~ 141.4 kt, track ~ 45 deg
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        // ME bits 6-8: mesub = 1 (subsonic ground speed)
        setbits_me(me, 6, 8, 1);
        // ME bits 11-13: NACv = 2
        setbits_me(me, 11, 13, 2);
        // ME bit 14: E/W direction = 0 (east)
        setbits_me(me, 14, 14, 0);
        // ME bits 15-24: E/W speed = 101 (raw: speed+1 = 100kt -> 101)
        setbits_me(me, 15, 24, 101);
        // ME bit 25: N/S direction = 0 (north)
        setbits_me(me, 25, 25, 0);
        // ME bits 26-35: N/S speed = 101 (raw: speed+1 = 100kt -> 101)
        setbits_me(me, 26, 35, 101);
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("vel gs_valid", mm.gs_valid, 1);
        ASSERT_FLOAT_NEAR("vel gs ~141", mm.gs.selected, 141.4f, 1.5f);
        ASSERT_EQ_INT("vel heading_valid", mm.heading_valid, 1);
        ASSERT_FLOAT_NEAR("vel track ~45", mm.heading, 45.0f, 1.0f);
    }

    // Subtype 1: pure south (E/W=0, N/S=200 south) -> track=180, gs=200
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 1); // mesub = 1
        // E/W speed = 0 (raw 0 means no info, but we need raw=1 for 0kt)
        // Actually raw=0 means "not available" and the function checks ew_raw && ns_raw
        // So we need raw=1 for 0kt: (1-1)*dir = 0
        setbits_me(me, 14, 14, 0); // E/W direction
        setbits_me(me, 15, 24, 1); // E/W speed = 1 (0 kt)
        setbits_me(me, 25, 25, 1); // N/S direction = 1 (south)
        setbits_me(me, 26, 35, 201); // N/S speed = 201 (200 kt south)
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("vel south gs_valid", mm.gs_valid, 1);
        ASSERT_FLOAT_NEAR("vel south gs", mm.gs.selected, 200.0f, 1.5f);
        ASSERT_EQ_INT("vel south heading_valid", mm.heading_valid, 1);
        ASSERT_FLOAT_NEAR("vel south track", mm.heading, 180.0f, 1.0f);
    }

    // Subtype 2: supersonic (4x multiplier)
    // E/W = 400kt east (raw 101), N/S = 0kt (raw 1)
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 2); // mesub = 2 (supersonic)
        setbits_me(me, 14, 14, 0); // E/W direction = east
        setbits_me(me, 15, 24, 101); // raw 101 -> (101-1)*4 = 400kt
        setbits_me(me, 25, 25, 0); // N/S direction = north
        setbits_me(me, 26, 35, 1); // raw 1 -> (1-1)*4 = 0kt
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("vel supersonic gs_valid", mm.gs_valid, 1);
        ASSERT_FLOAT_NEAR("vel supersonic gs", mm.gs.selected, 400.0f, 1.5f);
        ASSERT_FLOAT_NEAR("vel supersonic track", mm.heading, 90.0f, 1.0f);
    }

    fprintf(stderr, "testDecodeESAirborneVelocity_ground: done\n\n");
}

// ---- testDecodeESAirborneVelocity_airspeed ----

static void testDecodeESAirborneVelocity_airspeed(void) {
    fprintf(stderr, "=== testDecodeESAirborneVelocity_airspeed ===\n");

    // Subtype 3: heading = 180 deg, IAS = 300
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 3); // mesub = 3 (airspeed)
        // ME bit 14: heading status = 1 (valid)
        setbits_me(me, 14, 14, 1);
        // ME bits 15-24: heading = 512 (180 deg = 512/1024 * 360)
        setbits_me(me, 15, 24, 512);
        // ME bit 25: airspeed type = 0 (IAS)
        setbits_me(me, 25, 25, 0);
        // ME bits 26-35: airspeed = 301 (300 kt: raw-1 = 300)
        setbits_me(me, 26, 35, 301);
        // ME bit 36: vert rate source = 1 (baro)
        setbits_me(me, 36, 36, 1);
        // ME bit 37: vert rate sign = 0 (up)
        setbits_me(me, 37, 37, 0);
        // ME bits 38-46: vert rate = 33 -> (33-1)*64 = 2048 fpm
        setbits_me(me, 38, 46, 33);
        // ME bit 49: geom delta sign = 0 (positive)
        setbits_me(me, 49, 49, 0);
        // ME bits 50-56: geom delta = 11 -> (11-1)*25 = 250 ft
        setbits_me(me, 50, 56, 11);
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("airspeed heading_valid", mm.heading_valid, 1);
        ASSERT_FLOAT_NEAR("airspeed heading 180", mm.heading, 180.0f, 0.5f);
        ASSERT_EQ_INT("airspeed ias_valid", mm.ias_valid, 1);
        ASSERT_EQ_UINT("airspeed ias 300", mm.ias, 300);
        ASSERT_EQ_INT("airspeed baro_rate_valid", mm.baro_rate_valid, 1);
        ASSERT_EQ_INT("airspeed baro_rate 2048", mm.baro_rate, 2048);
        ASSERT_EQ_INT("airspeed geom_delta_valid", mm.geom_delta_valid, 1);
        ASSERT_EQ_INT("airspeed geom_delta 250", mm.geom_delta, 250);
    }

    // Subtype 3: TAS flag (bit 25 = 1)
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 3);
        setbits_me(me, 25, 25, 1); // TAS flag
        setbits_me(me, 26, 35, 251); // 250 kt TAS
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("tas flag valid", mm.tas_valid, 1);
        ASSERT_EQ_UINT("tas value 250", mm.tas, 250);
        ASSERT_EQ_INT("tas ias not valid", mm.ias_valid, 0);
    }

    // Invalid subtype (0) -> no fields set
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 0); // invalid subtype
        setbits_me(me, 15, 24, 101);
        setbits_me(me, 26, 35, 101);
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("invalid sub gs_valid", mm.gs_valid, 0);
        ASSERT_EQ_INT("invalid sub heading_valid", mm.heading_valid, 0);
    }

    // Invalid subtype (5) -> no fields set
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 5); // invalid subtype
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("sub5 gs_valid", mm.gs_valid, 0);
    }

    // Negative vert rate (sign bit set)
    {
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        mm.metype = 19;

        unsigned char me[7];
        memset(me, 0, sizeof(me));
        setbits_me(me, 6, 8, 3);
        setbits_me(me, 36, 36, 0); // geom rate source
        setbits_me(me, 37, 37, 1); // sign = down
        setbits_me(me, 38, 46, 17); // rate = (17-1)*(-64) = -1024
        memcpy(mm.ME, me, sizeof(me));

        decodeESAirborneVelocity(&mm, 0);

        ASSERT_EQ_INT("neg vrate geom_rate_valid", mm.geom_rate_valid, 1);
        ASSERT_EQ_INT("neg vrate -1024", mm.geom_rate, -1024);
    }

    fprintf(stderr, "testDecodeESAirborneVelocity_airspeed: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    modeACInit();

    testDecodeID13Field();
    testDecodeAC13Field();
    testDecodeAC12Field();
    testDecodeMovementFieldV0();
    testDecodeMovementFieldV2();
    testDecodeESIdentAndCategory();
    testDecodeESAirborneVelocity_ground();
    testDecodeESAirborneVelocity_airspeed();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
