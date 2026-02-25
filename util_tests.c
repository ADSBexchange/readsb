// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// util_tests.c - unit tests for pure utility functions in util.c
//
// We link util.o directly since the functions under test are public.
// This requires linking the same libraries util.c uses (zstd, z, pthread, rt).

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

// ---- test helpers ----

static int failures = 0;

#define ASSERT_EQ_INT(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_I64(tag, got, expected) do { \
    int64_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %ld, expected %ld\n", tag, (long)_g, (long)_e); \
        failures++; \
    } \
} while(0)

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

// ---- testGreatcircle ----

static void testGreatcircle(void) {
    fprintf(stderr, "=== testGreatcircle ===\n");

    // Same point -> 0 meters
    ASSERT_FLOAT_NEAR("gc same point", greatcircle(51.47, -0.46, 51.47, -0.46, 0), 0.0, 0.001);

    // London Heathrow to JFK: ~5,540 km
    {
        double dist = greatcircle(51.47, -0.46, 40.64, -73.78, 0);
        ASSERT_TRUE("gc LHR-JFK > 5400km", dist > 5400e3);
        ASSERT_TRUE("gc LHR-JFK < 5700km", dist < 5700e3);
    }

    // Equator: 0,0 to 0,1 -> ~111,195 m (1 degree of longitude at equator)
    {
        double dist = greatcircle(0.001, 0.0, 0.001, 1.0, 0);
        ASSERT_FLOAT_NEAR("gc equator 1deg", dist, 111195.0, 111195.0 * 0.01);
    }

    // Test approx=1 mode gives similar results
    {
        double exact = greatcircle(51.47, -0.46, 51.50, -0.50, 0);
        double approx = greatcircle(51.47, -0.46, 51.50, -0.50, 1);
        double pct = fabs(exact - approx) / exact * 100;
        ASSERT_TRUE("gc approx vs exact < 1%", pct < 1.0);
    }

    // Near north pole, opposite longitudes: ~2.2km apart (across the pole)
    {
        double dist = greatcircle(89.99, 0, 89.99, 180, 0);
        ASSERT_TRUE("gc near pole > 0", dist > 0);
        // 2 * (90 - 89.99) degrees ≈ 0.02 degrees ≈ 2224 meters
        ASSERT_FLOAT_NEAR("gc near pole", dist, 2224.0, 200.0);
    }

    fprintf(stderr, "testGreatcircle: done\n\n");
}

// ---- testBearing ----

static void testBearing(void) {
    fprintf(stderr, "=== testBearing ===\n");

    // Due north: (0,0) to (1,0) -> ~0° (or 360°)
    {
        double b = bearing(0.01, 0.0, 1.0, 0.0);
        // Should be near 0 or 360
        ASSERT_TRUE("bearing north", b < 1.0 || b > 359.0);
    }

    // Due east: (0,0) to (0,1) -> ~90°
    {
        double b = bearing(0.01, 0.0, 0.01, 1.0);
        ASSERT_FLOAT_NEAR("bearing east", b, 90.0, 1.0);
    }

    // Due south: (1,0) to (0,0) -> ~180°
    {
        double b = bearing(1.0, 0.01, 0.01, 0.01);
        ASSERT_FLOAT_NEAR("bearing south", b, 180.0, 1.0);
    }

    // Due west: (0,1) to (0,0) -> ~270°
    {
        double b = bearing(0.01, 1.0, 0.01, 0.0);
        ASSERT_FLOAT_NEAR("bearing west", b, 270.0, 1.0);
    }

    // Bearing is always in [0, 360)
    {
        double b = bearing(51.47, -0.46, 40.64, -73.78);
        ASSERT_TRUE("bearing range low", b >= 0.0);
        ASSERT_TRUE("bearing range high", b <= 360.0);
    }

    fprintf(stderr, "testBearing: done\n\n");
}

// ---- testSprintUUID ----

static void testSprintUUID(void) {
    fprintf(stderr, "=== testSprintUUID ===\n");

    // sprint_uuid1(0x0123456789ABCDEF) -> "01234567-89ab-cdef"
    {
        char buf[64] = {0};
        sprint_uuid1(0x0123456789ABCDEFULL, buf);
        ASSERT_STR_EQ("uuid1 basic", buf, "01234567-89ab-cdef");
    }

    // sprint_uuid1(0) -> "00000000-0000-0000"
    {
        char buf[64] = {0};
        sprint_uuid1(0, buf);
        ASSERT_STR_EQ("uuid1 zero", buf, "00000000-0000-0000");
    }

    // sprint_uuid1_partial(0xDEADBEEF) -> "deadbeef"
    {
        char buf[64] = {0};
        sprint_uuid1_partial(0xDEADBEEFULL, buf);
        ASSERT_STR_EQ("uuid1 partial", buf, "deadbeef");
    }

    // sprint_uuid1_partial(0) -> "00000000"
    {
        char buf[64] = {0};
        sprint_uuid1_partial(0, buf);
        ASSERT_STR_EQ("uuid1 partial zero", buf, "00000000");
    }

    // sprint_uuid1(0xFFFFFFFFFFFFFFFF) -> "ffffffff-ffff-ffff"
    {
        char buf[64] = {0};
        sprint_uuid1(0xFFFFFFFFFFFFFFFFULL, buf);
        ASSERT_STR_EQ("uuid1 all-F", buf, "ffffffff-ffff-ffff");
    }

    fprintf(stderr, "testSprintUUID: done\n\n");
}

// ---- testReceiveclockElapsed ----

static void testReceiveclockElapsed(void) {
    fprintf(stderr, "=== testReceiveclockElapsed ===\n");

    // receiveclock_ns_elapsed(0, 12000) -> 1,000,000 ns (1ms at 12MHz)
    // (12000 - 0) * 1000 / 12 = 1,000,000
    ASSERT_EQ_I64("ns 1ms", receiveclock_ns_elapsed(0, 12000), 1000000);

    // receiveclock_ms_elapsed(0, 12000000) -> 1000 ms
    // (12000000 - 0) / 12000 = 1000
    ASSERT_EQ_I64("ms 1s", receiveclock_ms_elapsed(0, 12000000), 1000);

    // Same timestamps -> 0
    ASSERT_EQ_I64("ns zero", receiveclock_ns_elapsed(100, 100), 0);

    // receiveclock_ms_elapsed(0, 6000) -> 0 (integer truncation: 6000/12000 = 0)
    ASSERT_EQ_I64("ms truncation", receiveclock_ms_elapsed(0, 6000), 0);

    // Larger interval
    // receiveclock_ns_elapsed(0, 120000) -> 10,000,000 ns (10ms)
    ASSERT_EQ_I64("ns 10ms", receiveclock_ns_elapsed(0, 120000), 10000000);

    // receiveclock_ms_elapsed with non-zero start
    // (24000 - 12000) / 12000 = 1
    ASSERT_EQ_I64("ms offset", receiveclock_ms_elapsed(12000, 24000), 1);

    fprintf(stderr, "testReceiveclockElapsed: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testGreatcircle();
    testBearing();
    testSprintUUID();
    testReceiveclockElapsed();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
