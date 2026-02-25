// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// net_io_tests.c - unit tests for pure functions from net_io.c
//
// Self-contained: copies ieee754_binary32_le_to_float and
// airground_enum_string to avoid linking net_io.o.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "readsb.h"

// Linker stubs
struct _Modes Modes;
void setExit(int __attribute__((unused)) arg) { }
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

// ---- Copied pure functions from net_io.c ----

static float test_ieee754_binary32_le_to_float(uint8_t *data) {
    double sign = (data[3] & 0x80) ? -1.0 : 1.0;
    int16_t raw_exponent = ((data[3] & 0x7f) << 1) | ((data[2] & 0x80) >> 7);
    uint32_t raw_significand = ((data[2] & 0x7f) << 16) | (data[1] << 8) | data[0];

    if (raw_exponent == 0) {
        if (raw_significand == 0) {
            return 0;
        } else {
            return ldexp(sign * raw_significand, -126 - 23);
        }
    }

    if (raw_exponent == 255) {
        if (raw_significand == 0) {
            return sign < 0 ? -INFINITY : INFINITY;
        } else {
#ifdef NAN
            return NAN;
#else
            return 0.0f;
#endif
        }
    }

    return ldexp(sign * ((1 << 23) | raw_significand), raw_exponent - 127 - 23);
}

static const char *test_airground_enum_string(airground_t ag) {
    switch (ag) {
        case AG_AIRBORNE:
            return "A+";
        case AG_GROUND:
            return "G+";
        default:
            return "?";
    }
}

// ---- test helpers ----

static int failures = 0;

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
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

#define ASSERT_STR_EQ(tag, got, expected) do { \
    if (strcmp((got), (expected)) != 0) { \
        fprintf(stderr, "%s: FAIL: got \"%s\", expected \"%s\"\n", tag, (got), (expected)); \
        failures++; \
    } \
} while(0)

// ---- testIeee754Binary32LeToFloat ----

static void testIeee754Binary32LeToFloat(void) {
    fprintf(stderr, "=== testIeee754Binary32LeToFloat ===\n");

    // +0.0: {00,00,00,00}
    {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee +0.0", result, 0.0, 0.0001);
    }

    // +1.0: {00,00,80,3F}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0x3F};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee +1.0", result, 1.0, 0.0001);
    }

    // -1.0: {00,00,80,BF}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0xBF};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee -1.0", result, -1.0, 0.0001);
    }

    // +inf: {00,00,80,7F}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0x7F};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee +inf", isinf(result) && result > 0);
    }

    // -inf: {00,00,80,FF}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0xFF};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee -inf", isinf(result) && result < 0);
    }

    // NaN: {01,00,80,7F}
    {
        uint8_t data[] = {0x01, 0x00, 0x80, 0x7F};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee NaN", isnan(result));
    }

    // 3.14159: {DB,0F,49,40}
    {
        uint8_t data[] = {0xDB, 0x0F, 0x49, 0x40};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee pi", result, 3.14159, 0.001);
    }

    // Denormal: {01,00,00,00} - smallest positive denormal
    {
        uint8_t data[] = {0x01, 0x00, 0x00, 0x00};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee denormal > 0", result > 0.0);
        ASSERT_TRUE("ieee denormal tiny", result < 1e-38);
    }

    // -0.0: {00,00,00,80} - treated as +0.0
    {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x80};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee -0.0", result, 0.0, 0.0001);
    }

    // 100.0: {00,00,C8,42}
    {
        uint8_t data[] = {0x00, 0x00, 0xC8, 0x42};
        float result = test_ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee 100.0", result, 100.0, 0.001);
    }

    fprintf(stderr, "testIeee754Binary32LeToFloat: done\n\n");
}

// ---- testAirgroundEnumString ----

static void testAirgroundEnumString(void) {
    fprintf(stderr, "=== testAirgroundEnumString ===\n");

    ASSERT_STR_EQ("ag airborne", test_airground_enum_string(AG_AIRBORNE), "A+");
    ASSERT_STR_EQ("ag ground", test_airground_enum_string(AG_GROUND), "G+");
    ASSERT_STR_EQ("ag invalid", test_airground_enum_string(AG_INVALID), "?");
    ASSERT_STR_EQ("ag uncertain", test_airground_enum_string(AG_UNCERTAIN), "?");

    fprintf(stderr, "testAirgroundEnumString: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testIeee754Binary32LeToFloat();
    testAirgroundEnumString();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
