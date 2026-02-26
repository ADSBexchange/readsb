// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// cpr_tests.c - unit tests for static helpers in cpr.c
//
// Uses #include "cpr.c" to access static functions directly.
// cpr.c only depends on <math.h> and <stdio.h> — zero stubs needed.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// ---- Include source under test ----

#include "cpr.c"

// ---- test helpers ----

static int failures = 0;

#define ASSERT_EQ_INT(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_DBL(tag, got, expected, tol) do { \
    double _g = (got), _e = (expected); \
    if (fabs(_g - _e) > (tol)) { \
        fprintf(stderr, "%s: FAIL: got %.10f, expected %.10f\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

// ---- testCprModInt ----

static void testCprModInt(void) {
    fprintf(stderr, "=== testCprModInt ===\n");

    // Positive modulus
    ASSERT_EQ_INT("modInt 7%%3", cprModInt(7, 3), 1);
    ASSERT_EQ_INT("modInt 10%%5", cprModInt(10, 5), 0);

    // Negative wrap-around: C '%' can return negative, cprModInt wraps positive
    ASSERT_EQ_INT("modInt -1%%60", cprModInt(-1, 60), 59);
    ASSERT_EQ_INT("modInt -7%%3", cprModInt(-7, 3), 2);

    // Zero numerator
    ASSERT_EQ_INT("modInt 0%%60", cprModInt(0, 60), 0);

    // Identity: a % b == a when a < b
    ASSERT_EQ_INT("modInt 5%%60", cprModInt(5, 60), 5);

    // Large negative
    ASSERT_EQ_INT("modInt -120%%59", cprModInt(-120, 59), (-120 % 59) + 59);

    fprintf(stderr, "testCprModInt: done\n\n");
}

// ---- testCprModDouble ----

static void testCprModDouble(void) {
    fprintf(stderr, "=== testCprModDouble ===\n");

    // Positive modulus
    ASSERT_EQ_DBL("modDbl 7.5%%3.0", cprModDouble(7.5, 3.0), 1.5, 1e-10);

    // Negative wrap
    ASSERT_EQ_DBL("modDbl -1.0%%360.0", cprModDouble(-1.0, 360.0), 359.0, 1e-10);

    // Fractional
    ASSERT_EQ_DBL("modDbl 0.3%%0.2", cprModDouble(0.3, 0.2), fmod(0.3, 0.2), 1e-10);

    // Zero
    ASSERT_EQ_DBL("modDbl 0.0%%6.0", cprModDouble(0.0, 6.0), 0.0, 1e-10);

    // Near-boundary (just under a full cycle)
    ASSERT_EQ_DBL("modDbl 359.9%%360.0", cprModDouble(359.9, 360.0), 359.9, 1e-10);

    // Exact multiple
    ASSERT_EQ_DBL("modDbl 360.0%%6.0", cprModDouble(360.0, 6.0), 0.0, 1e-10);

    fprintf(stderr, "testCprModDouble: done\n\n");
}

// ---- testCprNLFunction ----

static void testCprNLFunction(void) {
    fprintf(stderr, "=== testCprNLFunction ===\n");

    // Equator: NL(0) = 59
    ASSERT_EQ_INT("NL(0)", cprNLFunction(0.0), 59);

    // At 87.0 exactly: the table uses `lat < 87.0` for NL=2, so NL(87.0) = 1
    ASSERT_EQ_INT("NL(87)", cprNLFunction(87.0), 1);

    // Just below 87.0: NL = 2
    ASSERT_EQ_INT("NL(86.99)", cprNLFunction(86.99), 2);

    // At/beyond pole: NL(90) = 1
    ASSERT_EQ_INT("NL(90)", cprNLFunction(90.0), 1);

    // Symmetry: NL(-lat) == NL(lat)
    ASSERT_EQ_INT("NL(-51.5)==NL(51.5)", cprNLFunction(-51.5), cprNLFunction(51.5));
    ASSERT_EQ_INT("NL(-87)==NL(87)", cprNLFunction(-87.0), cprNLFunction(87.0));

    // Known transition boundaries from the table
    // lat < 10.47047130 -> 59
    ASSERT_EQ_INT("NL(10.0)", cprNLFunction(10.0), 59);
    // lat >= 10.47 -> 58
    ASSERT_EQ_INT("NL(10.5)", cprNLFunction(10.5), 58);

    // Mid-latitude check: lat=45 -> should be 42 (lat < 45.54626723)
    ASSERT_EQ_INT("NL(45.0)", cprNLFunction(45.0), 42);

    // Near pole boundary: lat < 86.53536998 -> 3, so 86.6 -> 2
    ASSERT_EQ_INT("NL(86.5)", cprNLFunction(86.5), 3);
    ASSERT_EQ_INT("NL(86.6)", cprNLFunction(86.6), 2);
    ASSERT_EQ_INT("NL(87.5)", cprNLFunction(87.5), 1);

    fprintf(stderr, "testCprNLFunction: done\n\n");
}

// ---- testCprNFunction ----

static void testCprNFunction(void) {
    fprintf(stderr, "=== testCprNFunction ===\n");

    // Even frame: N(0, 0) = NL(0) - 0 = 59
    ASSERT_EQ_INT("N(0,0)", cprNFunction(0.0, 0), 59);

    // Odd frame: N(0, 1) = NL(0) - 1 = 58
    ASSERT_EQ_INT("N(0,1)", cprNFunction(0.0, 1), 58);

    // NL(87.0)=1, even: max(1-0,1)=1, odd: max(1-1,1)=1
    ASSERT_EQ_INT("N(87,0)", cprNFunction(87.0, 0), 1);
    ASSERT_EQ_INT("N(87,1)", cprNFunction(87.0, 1), 1);

    // Just below 87: NL(86.99)=2, even=2, odd=1
    ASSERT_EQ_INT("N(86.99,0)", cprNFunction(86.99, 0), 2);
    ASSERT_EQ_INT("N(86.99,1)", cprNFunction(86.99, 1), 1);

    // At pole: NL(90)=1, even=1, odd=max(1-1,1)=1 (floor at 1)
    ASSERT_EQ_INT("N(90,0)", cprNFunction(90.0, 0), 1);
    ASSERT_EQ_INT("N(90,1)", cprNFunction(90.0, 1), 1);

    // Mid-latitude
    int nl_51 = cprNLFunction(51.5);
    ASSERT_EQ_INT("N(51.5,0)", cprNFunction(51.5, 0), nl_51);
    ASSERT_EQ_INT("N(51.5,1)", cprNFunction(51.5, 1), nl_51 - 1);

    fprintf(stderr, "testCprNFunction: done\n\n");
}

// ---- testCprDlonFunction ----

static void testCprDlonFunction(void) {
    fprintf(stderr, "=== testCprDlonFunction ===\n");

    // Airborne, equator, even: 360.0 / N(0,0) = 360/59
    ASSERT_EQ_DBL("Dlon air eq even", cprDlonFunction(0.0, 0, 0),
                  360.0 / 59.0, 1e-10);

    // Surface, equator, even: 90.0 / N(0,0) = 90/59
    ASSERT_EQ_DBL("Dlon sfc eq even", cprDlonFunction(0.0, 0, 1),
                  90.0 / 59.0, 1e-10);

    // Airborne, pole, odd: 360.0 / N(90,1) = 360/1 = 360
    ASSERT_EQ_DBL("Dlon air pole odd", cprDlonFunction(90.0, 1, 0),
                  360.0, 1e-10);

    // Surface, pole, odd: 90.0 / 1 = 90
    ASSERT_EQ_DBL("Dlon sfc pole odd", cprDlonFunction(90.0, 1, 1),
                  90.0, 1e-10);

    // Mid-latitude airborne even
    double expected = 360.0 / cprNFunction(51.5, 0);
    ASSERT_EQ_DBL("Dlon air 51.5 even", cprDlonFunction(51.5, 0, 0),
                  expected, 1e-10);

    fprintf(stderr, "testCprDlonFunction: done\n\n");
}

// ---- testDecodeCPRAirborne ----

static void testDecodeCPRAirborne(void) {
    fprintf(stderr, "=== testDecodeCPRAirborne ===\n");

    double rlat, rlon;
    int res;

    // Known test vector from cprtests.c
    res = decodeCPRairborne(80536, 9432, 61720, 9192, 0, &rlat, &rlon);
    ASSERT_EQ_INT("airborne even result", res, 0);
    ASSERT_EQ_DBL("airborne even lat", rlat, 51.686646, 1e-5);
    ASSERT_EQ_DBL("airborne even lon", rlon, 0.700156, 1e-5);

    // Odd frame
    res = decodeCPRairborne(80536, 9432, 61720, 9192, 1, &rlat, &rlon);
    ASSERT_EQ_INT("airborne odd result", res, 0);
    ASSERT_EQ_DBL("airborne odd lat", rlat, 51.686763, 1e-5);
    ASSERT_EQ_DBL("airborne odd lon", rlon, 0.701294, 1e-5);

    fprintf(stderr, "testDecodeCPRAirborne: done\n\n");
}

// ---- testDecodeCPRSurface ----

static void testDecodeCPRSurface(void) {
    fprintf(stderr, "=== testDecodeCPRSurface ===\n");

    double rlat, rlon;
    int res;

    // Cambridge airport surface test vector from cprtests.c
    res = decodeCPRsurface(52.00, 0.00, 105730, 9259, 29693, 8997, 0, &rlat, &rlon);
    ASSERT_EQ_INT("surface even result", res, 0);
    ASSERT_EQ_DBL("surface even lat", rlat, 52.209984, 1e-5);
    ASSERT_EQ_DBL("surface even lon", rlon, 0.176601, 1e-5);

    // Odd frame
    res = decodeCPRsurface(52.00, 0.00, 105730, 9259, 29693, 8997, 1, &rlat, &rlon);
    ASSERT_EQ_INT("surface odd result", res, 0);
    ASSERT_EQ_DBL("surface odd lat", rlat, 52.209976, 1e-5);
    ASSERT_EQ_DBL("surface odd lon", rlon, 0.176507, 1e-5);

    // With shifted reference longitude
    res = decodeCPRsurface(52.00, -180.00, 105730, 9259, 29693, 8997, 0, &rlat, &rlon);
    ASSERT_EQ_INT("surface shifted ref result", res, 0);
    ASSERT_EQ_DBL("surface shifted ref lat", rlat, 52.209984, 1e-5);
    ASSERT_EQ_DBL("surface shifted ref lon", rlon, 0.176601 - 180.0, 1e-5);

    fprintf(stderr, "testDecodeCPRSurface: done\n\n");
}

// ---- testDecodeCPRRelative ----

static void testDecodeCPRRelative(void) {
    fprintf(stderr, "=== testDecodeCPRRelative ===\n");

    double rlat, rlon;
    int res;

    // Airborne even relative decode
    res = decodeCPRrelative(52.00, 0.00, 80536, 9432, 0, 0, &rlat, &rlon);
    ASSERT_EQ_INT("rel air even result", res, 0);
    ASSERT_EQ_DBL("rel air even lat", rlat, 51.686646, 1e-5);
    ASSERT_EQ_DBL("rel air even lon", rlon, 0.700156, 1e-5);

    // Airborne odd relative decode
    res = decodeCPRrelative(52.00, 0.00, 61720, 9192, 1, 0, &rlat, &rlon);
    ASSERT_EQ_INT("rel air odd result", res, 0);
    ASSERT_EQ_DBL("rel air odd lat", rlat, 51.686763, 1e-5);
    ASSERT_EQ_DBL("rel air odd lon", rlon, 0.701294, 1e-5);

    // Surface even relative decode
    res = decodeCPRrelative(52.00, 0.00, 105730, 9259, 0, 1, &rlat, &rlon);
    ASSERT_EQ_INT("rel sfc even result", res, 0);
    ASSERT_EQ_DBL("rel sfc even lat", rlat, 52.209984, 1e-5);
    ASSERT_EQ_DBL("rel sfc even lon", rlon, 0.176601, 1e-5);

    // Surface odd relative decode
    res = decodeCPRrelative(52.00, 0.00, 29693, 8997, 1, 1, &rlat, &rlon);
    ASSERT_EQ_INT("rel sfc odd result", res, 0);
    ASSERT_EQ_DBL("rel sfc odd lat", rlat, 52.209976, 1e-5);
    ASSERT_EQ_DBL("rel sfc odd lon", rlon, 0.176507, 1e-5);

    // With shifted receiver reference, relative decode still works for nearby position
    res = decodeCPRrelative(48.70, 0.00, 80536, 9432, 0, 0, &rlat, &rlon);
    ASSERT_EQ_INT("rel shifted ref result", res, 0);
    ASSERT_EQ_DBL("rel shifted ref lat", rlat, 51.686646, 1e-5);

    fprintf(stderr, "testDecodeCPRRelative: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testCprModInt();
    testCprModDouble();
    testCprNLFunction();
    testCprNFunction();
    testCprDlonFunction();
    testDecodeCPRAirborne();
    testDecodeCPRSurface();
    testDecodeCPRRelative();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
