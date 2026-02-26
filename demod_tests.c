// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// demod_tests.c - unit tests for demodulator correlation functions
//
// Self-contained: copies slice_phase functions and generate_damage_set
// directly. No external dependencies.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

// ---- Copied pure functions from demod_2400.c ----

static inline int slice_phase0(uint16_t *m) {
    return 18 * m[0] - 15 * m[1] - 3 * m[2];
}

static inline int slice_phase1(uint16_t *m) {
    return 14 * m[0] - 5 * m[1] - 9 * m[2];
}

static inline int slice_phase2(uint16_t *m) {
    return 16 * m[0] + 5 * m[1] - 20 * m[2];
}

static inline int slice_phase3(uint16_t *m) {
    return 7 * m[0] + 11 * m[1] - 18 * m[2];
}

static inline int slice_phase4(uint16_t *m) {
    return 4 * m[0] + 15 * m[1] - 20 * m[2] + 1 * m[3];
}

static uint32_t generate_damage_set(uint8_t df, unsigned damage_bits) {
    uint32_t result = (1 << df);
    if (!damage_bits)
        return result;

    for (unsigned bit = 0; bit < 5; ++bit) {
        unsigned damaged_df = df ^ (1 << bit);
        result |= generate_damage_set(damaged_df, damage_bits - 1);
    }

    return result;
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

    // DC balance: coefficients sum to ~0
    // phase0: 18 - 15 - 3 = 0 ✓
    // phase1: 14 - 5 - 9 = 0 ✓
    // phase2: 16 + 5 - 20 = 1 (close to 0, not exact)
    // phase3: 7 + 11 - 18 = 0 ✓
    // phase4: 4 + 15 - 20 + 1 = 0 ✓
    // Verify DC balance: constant input yields near-zero
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
    // This represents a bit=1 (mark)
    uint16_t high_low[4] = {60000, 1000, 1000, 1000};
    ASSERT_TRUE("p0 high>0", slice_phase0(high_low) > 0);
    ASSERT_TRUE("p1 high>0", slice_phase1(high_low) > 0);
    ASSERT_TRUE("p2 high>0", slice_phase2(high_low) > 0);
    ASSERT_TRUE("p3 high>0", slice_phase3(high_low) > 0);
    ASSERT_TRUE("p4 high>0", slice_phase4(high_low) > 0);

    // "Low-high" pattern: weak first sample, strong later
    // This represents a bit=0 (space)
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
    // 17 = 10001 in binary. Flipping each of the 5 bits:
    // bit 0: 10000 = 16
    // bit 1: 10011 = 19
    // bit 2: 10101 = 21
    // bit 3: 11001 = 25
    // bit 4: 00001 = 1
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

    // damage_bits=2: expected cardinality check
    // For DF 17 with 2 damage bits: 1 (self) + 5 (1-bit) + up to 20 (2-bit)
    // Some 2-bit flips overlap, so exact count depends on specifics.
    // Just verify it's a reasonable number (> 6 from d=1, <= 32 total possible DFs)
    uint32_t set = generate_damage_set(17, 2);
    int pc = popcount32(set);
    ASSERT_TRUE("df17 d2 reasonable", pc > 6 && pc <= 32);

    fprintf(stderr, "testGenerateDamageSetBounds: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testSlicePhaseFormulas();
    testSlicePhaseSignDetection();
    testGenerateDamageSet();
    testGenerateDamageSetBounds();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
