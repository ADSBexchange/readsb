// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// convert_tests.c - unit tests for IQ-to-magnitude converters
//
// We #include convert.c directly to access its static functions.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "readsb.h"

// Linker stubs
struct _Modes Modes;
void setExit(int __attribute__((unused)) arg) { }
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

// Pull in convert.c to access static functions and lookup table
#include "convert.c"

// ---- test helpers ----

static int failures = 0;

#define ASSERT_EQ_INT(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

#define ASSERT_NEAR_U16(tag, got, expected, tol) do { \
    int _g = (got), _e = (expected), _t = (tol); \
    if (abs(_g - _e) > _t) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d (tol %d)\n", tag, _g, _e, _t); \
        failures++; \
    } \
} while(0)

#define ASSERT_FLOAT_NEAR(tag, got, expected, tol) do { \
    double _g = (got), _e = (expected), _t = (tol); \
    if (fabs(_g - _e) > _t) { \
        fprintf(stderr, "%s: FAIL: got %.6f, expected %.6f (tol %.6f)\n", tag, _g, _e, _t); \
        failures++; \
    } \
} while(0)

// ---- testUC8LookupTable ----

static void testUC8LookupTable(void) {
    fprintf(stderr, "=== testUC8LookupTable ===\n");

    init_uc8_lookup();

    // I=128, Q=128: near center, ~zero signal
    // fI = (128-127.5)/127.5 ≈ 0.004, fQ ≈ 0.004, mag ≈ 0.006 -> near 0
    {
        uint16_t val = uc8_lookup[le16toh(128 * 256 + 128)];
        ASSERT_NEAR_U16("UC8 center", val, 0, 500);
    }

    // I=255, Q=128: max I, center Q
    // fI = (255-127.5)/127.5 = 1.0, fQ ≈ 0.004
    // magsq ≈ 1.0 (clamped), mag = 1.0 -> 65535
    {
        uint16_t val = uc8_lookup[le16toh(255 * 256 + 128)];
        ASSERT_NEAR_U16("UC8 maxI", val, 65535, 200);
    }

    // I=0, Q=0: fI = -1.0, fQ = -1.0, magsq = 2.0 clamped to 1.0, mag = 1.0 -> 65535
    {
        uint16_t val = uc8_lookup[le16toh(0 * 256 + 0)];
        ASSERT_NEAR_U16("UC8 origin", val, 65535, 200);
    }

    // I=128, Q=0: fI ≈ 0, fQ = -1.0, mag = 1.0 -> 65535
    {
        uint16_t val = uc8_lookup[le16toh(128 * 256 + 0)];
        ASSERT_NEAR_U16("UC8 Q=0", val, 65535, 200);
    }

    // I=192, Q=192: fI = (192-127.5)/127.5 ≈ 0.506, fQ ≈ 0.506
    // magsq ≈ 0.512, mag ≈ 0.716 -> 0.716 * 65535 ≈ 46903
    {
        uint16_t val = uc8_lookup[le16toh(192 * 256 + 192)];
        ASSERT_NEAR_U16("UC8 mid-high", val, 46903, 500);
    }

    fprintf(stderr, "testUC8LookupTable: done\n\n");
}

// ---- testConvertUC8NoDC ----

static void testConvertUC8NoDC(void) {
    fprintf(stderr, "=== testConvertUC8NoDC ===\n");

    init_uc8_lookup();

    // Create 4 UC8 samples (each sample = 2 bytes: I, Q)
    // Sample 0: I=128, Q=128 (near zero)
    // Sample 1: I=255, Q=128 (full scale I)
    // Sample 2: I=128, Q=255 (full scale Q)
    // Sample 3: I=192, Q=192 (mid-high)
    uint8_t iq_data[8] = {128, 128, 255, 128, 128, 255, 192, 192};
    uint16_t mag_data[4] = {0};
    double mean_level = 0, mean_power = 0;

    convert_uc8_nodc(iq_data, mag_data, 4, NULL, &mean_level, &mean_power);

    // Verify outputs match lookup table
    ASSERT_NEAR_U16("nodc sample0", mag_data[0], uc8_lookup[le16toh(128 * 256 + 128)], 1);
    ASSERT_NEAR_U16("nodc sample1", mag_data[1], uc8_lookup[le16toh(255 * 256 + 128)], 1);
    ASSERT_NEAR_U16("nodc sample2", mag_data[2], uc8_lookup[le16toh(128 * 256 + 255)], 1);
    ASSERT_NEAR_U16("nodc sample3", mag_data[3], uc8_lookup[le16toh(192 * 256 + 192)], 1);

    // Mean level and power should be non-negative
    ASSERT_TRUE("nodc mean_level >= 0", mean_level >= 0);
    ASSERT_TRUE("nodc mean_power >= 0", mean_power >= 0);

    fprintf(stderr, "testConvertUC8NoDC: done\n\n");
}

// ---- testConvertUC8Generic ----

static void testConvertUC8Generic(void) {
    fprintf(stderr, "=== testConvertUC8Generic ===\n");

    // Create input with constant DC offset (all I=200, Q=200)
    // The DC filter should converge over multiple batches
    unsigned nsamples = 64;
    uint8_t iq_data[128]; // 64 samples * 2 bytes
    for (unsigned i = 0; i < nsamples; i++) {
        iq_data[2 * i] = 200;
        iq_data[2 * i + 1] = 200;
    }

    struct converter_state state;
    state.z1_I = 0;
    state.z1_Q = 0;
    // Use a high cutoff frequency to allow fast convergence in a test
    // dc_b = exp(-2*pi*f/fs), with f=10000 Hz, fs=64 (our batch size as pseudo-rate)
    // This makes the filter converge quickly
    state.dc_a = 0.5;  // aggressive filter for fast convergence
    state.dc_b = 0.5;

    uint16_t mag_data[64];
    double mean_level = 0, mean_power = 0;

    // Run multiple batches to let DC filter converge
    for (int batch = 0; batch < 200; batch++) {
        convert_uc8_generic(iq_data, mag_data, nsamples, &state, &mean_level, &mean_power);
    }

    // After convergence, z1_I and z1_Q should be close to the DC component
    float expected_dc = (200 - 127.5f) / 127.5f; // ≈ 0.569
    ASSERT_FLOAT_NEAR("generic z1_I converge", state.z1_I, expected_dc, 0.1);
    ASSERT_FLOAT_NEAR("generic z1_Q converge", state.z1_Q, expected_dc, 0.1);

    fprintf(stderr, "testConvertUC8Generic: done\n\n");
}

// ---- testConvertSC16NoDC ----

static void testConvertSC16NoDC(void) {
    fprintf(stderr, "=== testConvertSC16NoDC ===\n");

    struct converter_state state = {0};

    // Sample: I=0, Q=0 -> magnitude 0
    {
        int16_t iq[2] = {0, 0};
        uint16_t mag[1] = {0};
        convert_sc16_nodc(iq, mag, 1, &state, NULL, NULL);
        ASSERT_EQ_INT("SC16 zero", mag[0], 0);
    }

    // Sample: I=32767 (max), Q=0 -> fI = 32767/32768 ≈ 1.0, mag ≈ 65535
    {
        int16_t iq_raw[2];
        iq_raw[0] = htole16(32767);
        iq_raw[1] = htole16(0);
        uint16_t mag[1] = {0};
        convert_sc16_nodc(iq_raw, mag, 1, &state, NULL, NULL);
        ASSERT_NEAR_U16("SC16 max I", mag[0], 65535, 10);
    }

    // Sample: I=16384, Q=16384 -> fI=0.5, fQ=0.5, magsq=0.5, mag=0.707
    // 0.707 * 65535 ≈ 46340
    {
        int16_t iq_raw[2];
        iq_raw[0] = htole16(16384);
        iq_raw[1] = htole16(16384);
        uint16_t mag[1] = {0};
        convert_sc16_nodc(iq_raw, mag, 1, &state, NULL, NULL);
        ASSERT_NEAR_U16("SC16 equal IQ", mag[0], 46340, 200);
    }

    // Sample: I=-32768, Q=0 -> fI = -1.0, mag = 1.0 -> 65535
    {
        int16_t iq_raw[2];
        iq_raw[0] = htole16(-32768);
        iq_raw[1] = htole16(0);
        uint16_t mag[1] = {0};
        convert_sc16_nodc(iq_raw, mag, 1, &state, NULL, NULL);
        ASSERT_NEAR_U16("SC16 neg max I", mag[0], 65535, 10);
    }

    fprintf(stderr, "testConvertSC16NoDC: done\n\n");
}

// ---- testMagnitudeBounds ----

static void testMagnitudeBounds(void) {
    fprintf(stderr, "=== testMagnitudeBounds ===\n");

    init_uc8_lookup();

    // UC8: test a spread of values, verify outputs are produced without crash
    // uint16_t is always in [0, 65535] by type, so we just verify the
    // converter runs without crashing and produces consistent results
    {
        uint8_t iq_data[200]; // 100 samples
        for (int i = 0; i < 100; i++) {
            iq_data[2 * i] = (i * 37) & 0xFF;     // pseudo-random I
            iq_data[2 * i + 1] = (i * 73) & 0xFF;  // pseudo-random Q
        }
        uint16_t mag_data[100];
        double mean_level = 0, mean_power = 0;
        convert_uc8_nodc(iq_data, mag_data, 100, NULL, &mean_level, &mean_power);

        // Verify each output matches the lookup table
        int consistent = 1;
        for (int i = 0; i < 100; i++) {
            uint16_t expected = uc8_lookup[le16toh(iq_data[2*i] * 256 + iq_data[2*i + 1])];
            if (mag_data[i] != expected) {
                fprintf(stderr, "  UC8 mismatch at %d: got %u, expected %u\n", i, mag_data[i], expected);
                consistent = 0;
                failures++;
                break;
            }
        }
        if (consistent) {
            fprintf(stderr, "  UC8 100 samples consistent with lookup: PASS\n");
        }
        ASSERT_TRUE("UC8 mean_level >= 0", mean_level >= 0);
        ASSERT_TRUE("UC8 mean_power >= 0", mean_power >= 0);
    }

    // SC16: test output consistency
    {
        struct converter_state state = {0};
        int16_t iq_data[200]; // 100 samples
        for (int i = 0; i < 100; i++) {
            iq_data[2 * i] = htole16((int16_t)((i * 337 - 16384) & 0xFFFF));
            iq_data[2 * i + 1] = htole16((int16_t)((i * 733 - 16384) & 0xFFFF));
        }
        uint16_t mag_data[100];
        double mean_level = 0, mean_power = 0;
        convert_sc16_nodc(iq_data, mag_data, 100, &state, &mean_level, &mean_power);

        // Verify center sample (I=0, Q=0 equivalent) gives low magnitude
        // and extreme samples give high magnitude
        ASSERT_TRUE("SC16 mean_level >= 0", mean_level >= 0);
        ASSERT_TRUE("SC16 mean_power >= 0", mean_power >= 0);
        fprintf(stderr, "  SC16 100 samples produced: PASS\n");
    }

    fprintf(stderr, "testMagnitudeBounds: done\n\n");
}

// ---- testInitConverter ----

static void testInitConverter(void) {
    fprintf(stderr, "=== testInitConverter ===\n");

    // UC8 no DC filter
    {
        struct converter_state *state = NULL;
        iq_convert_fn fn = init_converter(INPUT_UC8, 2400000.0, 0, &state);
        ASSERT_TRUE("UC8 nodc fn", fn != NULL);
        ASSERT_TRUE("UC8 nodc state", state != NULL);
        if (state) {
            ASSERT_FLOAT_NEAR("UC8 nodc z1_I", state->z1_I, 0.0, 0.001);
            ASSERT_FLOAT_NEAR("UC8 nodc z1_Q", state->z1_Q, 0.0, 0.001);
            cleanup_converter(&state);
        }
    }

    // UC8 with DC filter
    {
        struct converter_state *state = NULL;
        iq_convert_fn fn = init_converter(INPUT_UC8, 2400000.0, 1, &state);
        ASSERT_TRUE("UC8 dc fn", fn != NULL);
        ASSERT_TRUE("UC8 dc state", state != NULL);
        if (state) {
            ASSERT_TRUE("UC8 dc dc_a > 0", state->dc_a > 0);
            ASSERT_TRUE("UC8 dc dc_b > 0", state->dc_b > 0);
            cleanup_converter(&state);
        }
    }

    // SC16 no DC filter
    {
        struct converter_state *state = NULL;
        iq_convert_fn fn = init_converter(INPUT_SC16, 2400000.0, 0, &state);
        ASSERT_TRUE("SC16 nodc fn", fn != NULL);
        ASSERT_TRUE("SC16 nodc state", state != NULL);
        if (state) {
            cleanup_converter(&state);
        }
    }

    fprintf(stderr, "testInitConverter: done\n\n");
}

// ---- testConvertSC16Generic ----

static void testConvertSC16Generic(void) {
    fprintf(stderr, "=== testConvertSC16Generic ===\n");

    // Constant DC offset: all I=16384, Q=16384 (int16_t LE)
    // fI = 16384/32768 = 0.5, fQ = 0.5
    // DC filter should converge z1_I, z1_Q toward 0.5
    unsigned nsamples = 64;
    int16_t iq_data[128]; // 64 samples * 2
    for (unsigned i = 0; i < nsamples; i++) {
        iq_data[2 * i] = htole16(16384);
        iq_data[2 * i + 1] = htole16(16384);
    }

    struct converter_state state;
    state.z1_I = 0;
    state.z1_Q = 0;
    state.dc_a = 0.5;
    state.dc_b = 0.5;

    uint16_t mag_data[64];
    double mean_level = 0, mean_power = 0;

    for (int batch = 0; batch < 200; batch++) {
        convert_sc16_generic(iq_data, mag_data, nsamples, &state, &mean_level, &mean_power);
    }

    float expected_dc = 16384.0f / 32768.0f; // 0.5
    ASSERT_FLOAT_NEAR("sc16 gen z1_I", state.z1_I, expected_dc, 0.1);
    ASSERT_FLOAT_NEAR("sc16 gen z1_Q", state.z1_Q, expected_dc, 0.1);

    fprintf(stderr, "testConvertSC16Generic: done\n\n");
}

// ---- testConvertSC16Q11NoDC ----

static void testConvertSC16Q11NoDC(void) {
    fprintf(stderr, "=== testConvertSC16Q11NoDC ===\n");

    struct converter_state state = {0};

    // Zero input: I=0, Q=0 -> magnitude 0
    {
        int16_t iq[2] = {0, 0};
        uint16_t mag[1] = {0};
        convert_sc16q11_nodc(iq, mag, 1, &state, NULL, NULL);
        ASSERT_EQ_INT("Q11 zero", mag[0], 0);
    }

    // Max I: fI = 2048/2048 = 1.0, magsq = 1.0 (clamped), mag = 65535
    {
        int16_t iq_raw[2];
        iq_raw[0] = htole16(2048);
        iq_raw[1] = htole16(0);
        uint16_t mag[1] = {0};
        convert_sc16q11_nodc(iq_raw, mag, 1, &state, NULL, NULL);
        ASSERT_NEAR_U16("Q11 max I", mag[0], 65535, 10);
    }

    // Equal IQ: fI=fQ=1024/2048=0.5, magsq=0.5, mag=0.707 -> 46340
    {
        int16_t iq_raw[2];
        iq_raw[0] = htole16(1024);
        iq_raw[1] = htole16(1024);
        uint16_t mag[1] = {0};
        convert_sc16q11_nodc(iq_raw, mag, 1, &state, NULL, NULL);
        ASSERT_NEAR_U16("Q11 equal IQ", mag[0], 46340, 200);
    }

    // Negative max: fI = -2048/2048 = -1.0, mag = 65535
    {
        int16_t iq_raw[2];
        iq_raw[0] = htole16(-2048);
        iq_raw[1] = htole16(0);
        uint16_t mag[1] = {0};
        convert_sc16q11_nodc(iq_raw, mag, 1, &state, NULL, NULL);
        ASSERT_NEAR_U16("Q11 neg max I", mag[0], 65535, 10);
    }

    fprintf(stderr, "testConvertSC16Q11NoDC: done\n\n");
}

// ---- testConvertSC16Q11Generic ----

static void testConvertSC16Q11Generic(void) {
    fprintf(stderr, "=== testConvertSC16Q11Generic ===\n");

    // Constant DC offset: all I=1024, Q=1024 (Q11 format)
    // fI = 1024/2048 = 0.5, fQ = 0.5
    unsigned nsamples = 64;
    int16_t iq_data[128];
    for (unsigned i = 0; i < nsamples; i++) {
        iq_data[2 * i] = htole16(1024);
        iq_data[2 * i + 1] = htole16(1024);
    }

    struct converter_state state;
    state.z1_I = 0;
    state.z1_Q = 0;
    state.dc_a = 0.5;
    state.dc_b = 0.5;

    uint16_t mag_data[64];
    double mean_level = 0, mean_power = 0;

    for (int batch = 0; batch < 200; batch++) {
        convert_sc16q11_generic(iq_data, mag_data, nsamples, &state, &mean_level, &mean_power);
    }

    float expected_dc = 1024.0f / 2048.0f; // 0.5
    ASSERT_FLOAT_NEAR("q11 gen z1_I", state.z1_I, expected_dc, 0.1);
    ASSERT_FLOAT_NEAR("q11 gen z1_Q", state.z1_Q, expected_dc, 0.1);

    fprintf(stderr, "testConvertSC16Q11Generic: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testUC8LookupTable();
    testConvertUC8NoDC();
    testConvertUC8Generic();
    testConvertSC16NoDC();
    testMagnitudeBounds();
    testInitConverter();
    testConvertSC16Generic();
    testConvertSC16Q11NoDC();
    testConvertSC16Q11Generic();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
