// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// geomag_tests.c - unit tests for geomagnetic calculations in geomag.c
//
// geomag.c is self-contained (no readsb.h dependency), pure math with
// well-defined WMM2020 reference values.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "geomag.h"

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

#define ASSERT_FLOAT_NEAR(tag, got, expected, tol) do { \
    double _g = (got), _e = (expected), _t = (tol); \
    if (fabs(_g - _e) > _t) { \
        fprintf(stderr, "%s: FAIL: got %.6f, expected %.6f (tol %.6f)\n", tag, _g, _e, _t); \
        failures++; \
    } \
} while(0)

// ---- testGeomagInit ----

static void testGeomagInit(void) {
    fprintf(stderr, "=== testGeomagInit ===\n");

    int ret = geomag_init();
    ASSERT_EQ_INT("geomag_init returns 0", ret, 0);

    fprintf(stderr, "testGeomagInit: done\n\n");
}

// ---- testGeomagKnownLocations ----
// WMM2020 reference values at epoch 2020.0, alt=0km.
// Tolerances are generous to allow for implementation differences.

static void testGeomagKnownLocations(void) {
    fprintf(stderr, "=== testGeomagKnownLocations ===\n");

    double dec, dip, ti, gv;

    // Boulder CO (40.0N, 105.25W): dec ~7.7E, dip ~66.7, ti ~51900
    geomag_calc(0, 40.0, -105.25, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_FLOAT_NEAR("boulder dec", dec, 7.7, 1.0);
    ASSERT_FLOAT_NEAR("boulder dip", dip, 66.7, 2.0);
    ASSERT_FLOAT_NEAR("boulder ti", ti, 51900.0, 1000.0);

    // Equator / Prime Meridian (0.0, 0.0): dip moderate (WMM2020 gives ~-30 at 0,0)
    geomag_calc(0, 0.0, 0.0, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_FLOAT_NEAR("equator dip", dip, -30.0, 5.0);

    // Tokyo (35.7N, 139.7E): dec ~ -8.0W
    geomag_calc(0, 35.7, 139.7, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_FLOAT_NEAR("tokyo dec", dec, -8.0, 1.5);

    // Sydney (33.9S, 151.2E): dec ~ 12.6E
    geomag_calc(0, -33.9, 151.2, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_FLOAT_NEAR("sydney dec", dec, 12.6, 1.5);

    fprintf(stderr, "testGeomagKnownLocations: done\n\n");
}

// ---- testGeomagPoles ----
// Poles should not crash and should not return NaN

static void testGeomagPoles(void) {
    fprintf(stderr, "=== testGeomagPoles ===\n");

    double dec, dip, ti, gv;

    // North pole
    int ret = geomag_calc(0, 90.0, 0.0, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_EQ_INT("north pole returns 0", ret, 0);
    ASSERT_TRUE("north pole ti not NaN", !isnan(ti));
    ASSERT_TRUE("north pole dip not NaN", !isnan(dip));

    // South pole
    ret = geomag_calc(0, -90.0, 0.0, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_EQ_INT("south pole returns 0", ret, 0);
    ASSERT_TRUE("south pole ti not NaN", !isnan(ti));
    ASSERT_TRUE("south pole dip not NaN", !isnan(dip));

    fprintf(stderr, "testGeomagPoles: done\n\n");
}

// ---- testGeomagAltitudeEffect ----
// Same location at 0km vs 100km: ti should decrease, dec stays ~constant

static void testGeomagAltitudeEffect(void) {
    fprintf(stderr, "=== testGeomagAltitudeEffect ===\n");

    double dec0, dip0, ti0, gv0;
    double dec100, dip100, ti100, gv100;

    geomag_calc(0, 40.0, -105.0, 2020.0, &dec0, &dip0, &ti0, &gv0);
    geomag_calc(100, 40.0, -105.0, 2020.0, &dec100, &dip100, &ti100, &gv100);

    // Total intensity should decrease with altitude
    ASSERT_TRUE("ti decreases with alt", ti100 < ti0);

    // Declination should stay approximately the same
    ASSERT_FLOAT_NEAR("dec stable with alt", dec100, dec0, 2.0);

    fprintf(stderr, "testGeomagAltitudeEffect: done\n\n");
}

// ---- testGeomagGridVariation ----
// lat < 55: gv = -999.0; Arctic (80N): gv != -999.0

static void testGeomagGridVariation(void) {
    fprintf(stderr, "=== testGeomagGridVariation ===\n");

    double dec, dip, ti, gv;

    // Mid-latitude: gv should be -999.0
    geomag_calc(0, 40.0, -105.0, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_FLOAT_NEAR("mid-lat gv = -999", gv, -999.0, 0.001);

    // Arctic (80N): gv should NOT be -999.0
    geomag_calc(0, 80.0, 0.0, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_TRUE("arctic gv != -999", fabs(gv - (-999.0)) > 0.1);

    // Antarctic (-70S): gv should NOT be -999.0
    geomag_calc(0, -70.0, 0.0, 2020.0, &dec, &dip, &ti, &gv);
    ASSERT_TRUE("antarctic gv != -999", fabs(gv - (-999.0)) > 0.1);

    fprintf(stderr, "testGeomagGridVariation: done\n\n");
}

// ---- testGeomagConsistency ----
// Same input twice -> identical outputs

static void testGeomagConsistency(void) {
    fprintf(stderr, "=== testGeomagConsistency ===\n");

    double dec1, dip1, ti1, gv1;
    double dec2, dip2, ti2, gv2;

    geomag_calc(0, 51.5, -0.1, 2020.0, &dec1, &dip1, &ti1, &gv1);
    geomag_calc(0, 51.5, -0.1, 2020.0, &dec2, &dip2, &ti2, &gv2);

    ASSERT_FLOAT_NEAR("consistency dec", dec1, dec2, 0.0001);
    ASSERT_FLOAT_NEAR("consistency dip", dip1, dip2, 0.0001);
    ASSERT_FLOAT_NEAR("consistency ti", ti1, ti2, 0.0001);
    ASSERT_FLOAT_NEAR("consistency gv", gv1, gv2, 0.0001);

    fprintf(stderr, "testGeomagConsistency: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testGeomagInit();
    testGeomagKnownLocations();
    testGeomagPoles();
    testGeomagAltitudeEffect();
    testGeomagGridVariation();
    testGeomagConsistency();

    geomag_destroy();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
