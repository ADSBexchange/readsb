// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// demod_tests.c - unit tests for demodulator functions
//
// Uses #include "demod_2400.c" to access static functions directly.
// Provides linker stubs for external symbols that demod_2400.c references
// but that we don't exercise in the tests.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
struct _Threads Threads;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];
void setExit(int __attribute__((unused)) arg) { }
int64_t mstime(void) { return 1700000000000LL; }

int scoreModesMessage(unsigned char __attribute__((unused)) *msg,
                      int __attribute__((unused)) validbits) { return -2; }
int decodeModesMessage(struct modesMessage __attribute__((unused)) *mm) { return -1; }
void netUseMessage(struct modesMessage __attribute__((unused)) *mm) { }
static struct modesMessage dummy_mm;
struct modesMessage *netGetMM(struct messageBuffer __attribute__((unused)) *buf) { return &dummy_mm; }
void netDrainMessageBuffers(void) { }
int64_t receiveclock_ms_elapsed(int64_t __attribute__((unused)) t1,
                                 int64_t __attribute__((unused)) t2) { return 0; }
void decodeModeAMessage(struct modesMessage __attribute__((unused)) *mm,
                         int __attribute__((unused)) ModeA) { }

// ---- Include source under test ----

#include "demod_2400.c"

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

#define ASSERT_U32_EQ(tag, got, expected) do { \
    uint32_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got 0x%08x, expected 0x%08x\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

// Helper: count set bits in uint32_t
static int popcount32(uint32_t x) {
    int c = 0;
    while (x) {
        c += (x & 1);
        x >>= 1;
    }
    return c;
}

// ---- testSlicePhaseFormulas ----

static void testSlicePhaseFormulas(void) {
    fprintf(stderr, "=== testSlicePhaseFormulas ===\n");

    // All-zero input returns 0 for all phases
    uint16_t zeros[4] = {0, 0, 0, 0};
    ASSERT_INT_EQ("phase0 zero", slice_phase0(zeros), 0);
    ASSERT_INT_EQ("phase1 zero", slice_phase1(zeros), 0);
    ASSERT_INT_EQ("phase2 zero", slice_phase2(zeros), 0);
    ASSERT_INT_EQ("phase3 zero", slice_phase3(zeros), 0);
    ASSERT_INT_EQ("phase4 zero", slice_phase4(zeros), 0);

    // Known values with specific input
    uint16_t v1[4] = {100, 200, 300};
    // phase0: 18*100 - 15*200 - 3*300 = 1800 - 3000 - 900 = -2100
    ASSERT_INT_EQ("phase0 known", slice_phase0(v1), -2100);
    // phase1: 14*100 - 5*200 - 9*300 = 1400 - 1000 - 2700 = -2300
    ASSERT_INT_EQ("phase1 known", slice_phase1(v1), -2300);
    // phase2: 16*100 + 5*200 - 20*300 = 1600 + 1000 - 6000 = -3400
    ASSERT_INT_EQ("phase2 known", slice_phase2(v1), -3400);
    // phase3: 7*100 + 11*200 - 18*300 = 700 + 2200 - 5400 = -2500
    ASSERT_INT_EQ("phase3 known", slice_phase3(v1), -2500);

    // phase4 needs m[3]
    uint16_t v2[4] = {100, 200, 300, 400};
    // phase4: 4*100 + 15*200 - 20*300 + 1*400 = 400 + 3000 - 6000 + 400 = -2200
    ASSERT_INT_EQ("phase4 known", slice_phase4(v2), -2200);

    // DC balance: constant input yields near-zero
    uint16_t dc[4] = {1000, 1000, 1000, 1000};
    ASSERT_INT_EQ("phase0 dc", slice_phase0(dc), 0);
    ASSERT_INT_EQ("phase1 dc", slice_phase1(dc), 0);
    // phase2 has coeff sum = 1, so dc input gives 1*1000 = 1000
    ASSERT_INT_EQ("phase2 dc", slice_phase2(dc), 1000);
    ASSERT_INT_EQ("phase3 dc", slice_phase3(dc), 0);
    ASSERT_INT_EQ("phase4 dc", slice_phase4(dc), 0);

    fprintf(stderr, "testSlicePhaseFormulas: done\n\n");
}

// ---- testSlicePhaseSignDetection ----

static void testSlicePhaseSignDetection(void) {
    fprintf(stderr, "=== testSlicePhaseSignDetection ===\n");

    // "High-low" pattern: strong first sample, weak later
    uint16_t high_low[4] = {60000, 1000, 1000, 1000};
    ASSERT_TRUE("p0 high>0", slice_phase0(high_low) > 0);
    ASSERT_TRUE("p1 high>0", slice_phase1(high_low) > 0);
    ASSERT_TRUE("p2 high>0", slice_phase2(high_low) > 0);
    ASSERT_TRUE("p3 high>0", slice_phase3(high_low) > 0);
    ASSERT_TRUE("p4 high>0", slice_phase4(high_low) > 0);

    // "Low-high" pattern: weak first sample, strong later
    uint16_t low_high[4] = {1000, 1000, 60000, 60000};
    ASSERT_TRUE("p0 low<0", slice_phase0(low_high) < 0);
    ASSERT_TRUE("p1 low<0", slice_phase1(low_high) < 0);
    ASSERT_TRUE("p2 low<0", slice_phase2(low_high) < 0);
    ASSERT_TRUE("p3 low<0", slice_phase3(low_high) < 0);
    ASSERT_TRUE("p4 low<0", slice_phase4(low_high) < 0);

    fprintf(stderr, "testSlicePhaseSignDetection: done\n\n");
}

// ---- testGenerateDamageSet ----

static void testGenerateDamageSet(void) {
    fprintf(stderr, "=== testGenerateDamageSet ===\n");

    // damage_bits=0: only the DF itself
    uint32_t set17_0 = generate_damage_set(17, 0);
    ASSERT_TRUE("df17 d0 has 17", (set17_0 & (1u << 17)) != 0);
    ASSERT_INT_EQ("df17 d0 popcount", popcount32(set17_0), 1);

    // damage_bits=0 for DF 0
    uint32_t set0_0 = generate_damage_set(0, 0);
    ASSERT_TRUE("df0 d0 has 0", (set0_0 & (1u << 0)) != 0);
    ASSERT_INT_EQ("df0 d0 popcount", popcount32(set0_0), 1);

    // damage_bits=1: DF 17 plus single-bit-flip neighbors
    uint32_t set17_1 = generate_damage_set(17, 1);
    ASSERT_TRUE("df17 d1 has 17", (set17_1 & (1u << 17)) != 0);
    ASSERT_TRUE("df17 d1 has 16", (set17_1 & (1u << 16)) != 0);
    ASSERT_TRUE("df17 d1 has 19", (set17_1 & (1u << 19)) != 0);
    ASSERT_TRUE("df17 d1 has 21", (set17_1 & (1u << 21)) != 0);
    ASSERT_TRUE("df17 d1 has 25", (set17_1 & (1u << 25)) != 0);
    ASSERT_TRUE("df17 d1 has 1",  (set17_1 & (1u << 1)) != 0);
    ASSERT_INT_EQ("df17 d1 popcount", popcount32(set17_1), 6);

    // damage_bits=2 should have more entries than damage_bits=1
    uint32_t set17_2 = generate_damage_set(17, 2);
    ASSERT_TRUE("df17 d2 larger", popcount32(set17_2) > popcount32(set17_1));

    fprintf(stderr, "testGenerateDamageSet: done\n\n");
}

// ---- testGenerateDamageSetBounds ----

static void testGenerateDamageSetBounds(void) {
    fprintf(stderr, "=== testGenerateDamageSetBounds ===\n");

    // All valid DFs 0-31 with damage_bits=0 produce single-element sets
    for (uint8_t df = 0; df < 32; df++) {
        uint32_t set = generate_damage_set(df, 0);
        char tag[64];
        snprintf(tag, sizeof(tag), "df%d d0 single", df);
        ASSERT_INT_EQ(tag, popcount32(set), 1);
        snprintf(tag, sizeof(tag), "df%d d0 has self", df);
        ASSERT_TRUE(tag, (set & (1u << df)) != 0);
    }

    // damage_bits=2: reasonable cardinality
    uint32_t set = generate_damage_set(17, 2);
    int pc = popcount32(set);
    ASSERT_TRUE("df17 d2 reasonable", pc > 6 && pc <= 32);

    fprintf(stderr, "testGenerateDamageSetBounds: done\n\n");
}

// ---- testInitBitsets ----

static void testInitBitsets(void) {
    fprintf(stderr, "=== testInitBitsets ===\n");

    // Reset Modes fields relevant to init_bitsets
    Modes.fixDF = 0;
    Modes.nfix_crc = 0;

    init_bitsets();

    // Short bitset: DFs 0, 4, 5, 11
    ASSERT_TRUE("short has DF0", (valid_df_short_bitset & (1u << 0)) != 0);
    ASSERT_TRUE("short has DF4", (valid_df_short_bitset & (1u << 4)) != 0);
    ASSERT_TRUE("short has DF5", (valid_df_short_bitset & (1u << 5)) != 0);
    ASSERT_TRUE("short has DF11", (valid_df_short_bitset & (1u << 11)) != 0);

    // Long bitset: DFs 16, 17, 18, 20, 21
    ASSERT_TRUE("long has DF16", (valid_df_long_bitset & (1u << 16)) != 0);
    ASSERT_TRUE("long has DF17", (valid_df_long_bitset & (1u << 17)) != 0);
    ASSERT_TRUE("long has DF18", (valid_df_long_bitset & (1u << 18)) != 0);
    ASSERT_TRUE("long has DF20", (valid_df_long_bitset & (1u << 20)) != 0);
    ASSERT_TRUE("long has DF21", (valid_df_long_bitset & (1u << 21)) != 0);

    // Without fixDF, DF17 damage neighbors should NOT be in long bitset
    // DF17 neighbors: 16, 19, 21, 25, 1
    // DF16 and DF21 are already present, but DF19 and DF25 should not be
    // (unless ENABLE_DF24 is defined, in which case 25 would be present)
#ifndef ENABLE_DF24
    ASSERT_TRUE("no DF19 without fix", (valid_df_long_bitset & (1u << 19)) == 0);
#endif

    fprintf(stderr, "testInitBitsets: done\n\n");
}

// ---- testInitBitsetsWithDamage ----

static void testInitBitsetsWithDamage(void) {
    fprintf(stderr, "=== testInitBitsetsWithDamage ===\n");

    // Enable fixDF with nfix_crc=1
    Modes.fixDF = 1;
    Modes.nfix_crc = 1;

    init_bitsets();

    // Long bitset should now include DF17 damage neighbors from generate_damage_set(17, 1)
    uint32_t damage17 = generate_damage_set(17, 1);
    // All bits from damage17 should be in valid_df_long_bitset
    ASSERT_TRUE("damage set included", (valid_df_long_bitset & damage17) == damage17);

    // Specifically check DF 1 (a DF17 neighbor via bit 4 flip: 10001 -> 00001)
    ASSERT_TRUE("long has DF1 (damage)", (valid_df_long_bitset & (1u << 1)) != 0);

    // Reset
    Modes.fixDF = 0;
    Modes.nfix_crc = 0;

    fprintf(stderr, "testInitBitsetsWithDamage: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testSlicePhaseFormulas();
    testSlicePhaseSignDetection();
    testGenerateDamageSet();
    testGenerateDamageSetBounds();
    testInitBitsets();
    testInitBitsetsWithDamage();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
