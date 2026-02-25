// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// track_tests.c - unit tests for static functions in track.c
//
// Copies the small pure static functions from track.c to avoid pulling in
// the entire track.c dependency graph. Tests: compute_nic, compute_rc,
// altitude_to_feet, addressReliable, simpleHash.

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

// ---- Copied static functions from track.c ----

#define RC_UNKNOWN 0

static uint16_t simpleHash(uint64_t receiverId) {
    uint16_t simpleHash = receiverId;
    simpleHash ^= (uint16_t) (receiverId >> 16);
    simpleHash ^= (uint16_t) (receiverId >> 32);
    simpleHash ^= (uint16_t) (receiverId >> 48);
    if (simpleHash == 0)
        return 1;
    return simpleHash;
}

static unsigned compute_nic(unsigned metype, unsigned version, unsigned nic_a, unsigned nic_b, unsigned nic_c) {
    (void)nic_b; // used in some cases below via the original parameter name
    switch (metype) {
        case 5: case 9: case 20:
            return 11;
        case 6: case 10: case 21:
            return 10;
        case 7:
            if (version == 2) {
                if (nic_a && !nic_c) return 9;
                else return 8;
            } else if (version == 1) {
                if (nic_a) return 9;
                else return 8;
            } else {
                return 8;
            }
        case 8:
            if (version == 2) {
                if (nic_a && nic_c) return 7;
                else if (nic_a && !nic_c) return 6;
                else if (!nic_a && nic_c) return 6;
                else return 0;
            } else {
                return 0;
            }
        case 11:
            if (version == 2) {
                if (nic_a && nic_b) return 9;
                else return 8;
            } else if (version == 1) {
                if (nic_a) return 9;
                else return 8;
            } else {
                return 8;
            }
        case 12: return 7;
        case 13: return 6;
        case 14: return 5;
        case 15: return 4;
        case 16:
            if (nic_a && nic_b) return 3;
            else return 2;
        case 17: return 1;
        default: return 0;
    }
}

static unsigned compute_rc(unsigned metype, unsigned version, unsigned nic_a, unsigned nic_b, unsigned nic_c) {
    switch (metype) {
        case 5: case 9: case 20:
            return 8;
        case 6: case 10: case 21:
            return 25;
        case 7:
            if (version == 2) {
                if (nic_a && !nic_c) return 75;
                else return 186;
            } else if (version == 1) {
                if (nic_a) return 75;
                else return 186;
            } else {
                return 186;
            }
        case 8:
            if (version == 2) {
                if (nic_a && nic_c) return 371;
                else if (nic_a && !nic_c) return 556;
                else if (!nic_a && nic_c) return 926;
                else return RC_UNKNOWN;
            } else {
                return RC_UNKNOWN;
            }
        case 11:
            if (version == 2) {
                if (nic_a && nic_b) return 75;
                else return 186;
            } else if (version == 1) {
                if (nic_a) return 75;
                else return 186;
            } else {
                return 186;
            }
        case 12: return 371;
        case 13:
            if (version == 2) {
                if (!nic_a && nic_b) return 556;
                else if (!nic_a && !nic_b) return 926;
                else if (nic_a && nic_b) return 1112;
                else return RC_UNKNOWN;
            } else if (version == 1) {
                if (nic_a) return 1112;
                else return 926;
            } else {
                return 926;
            }
        case 14: return 1852;
        case 15: return 3704;
        case 16:
            if (version == 2) {
                if (nic_a && nic_b) return 7408;
                else return 14816;
            } else if (version == 1) {
                if (nic_a) return 7408;
                else return 14816;
            } else {
                return 18520;
            }
        case 17: return 37040;
        default: return RC_UNKNOWN;
    }
}

static int altitude_to_feet(int raw, altitude_unit_t unit) {
    switch (unit) {
        case UNIT_METERS:
            return raw / 0.3048;
        case UNIT_FEET:
            return raw;
        default:
            return 0;
    }
}

static int addressReliable(struct modesMessage *mm) {
    if (mm->msgtype == 17 || mm->msgtype == 18 || (mm->msgtype == 11 && mm->IID == 0) || mm->sbs_in) {
        return 1;
    }
    return 0;
}

// ---- test helpers ----

static int failures = 0;

#define ASSERT_EQ_UINT(tag, got, expected) do { \
    unsigned _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %u, expected %u\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

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

// ---- testComputeNic ----

static void testComputeNic(void) {
    fprintf(stderr, "=== testComputeNic ===\n");

    // metype 5/9/20 -> NIC 11
    ASSERT_EQ_UINT("nic metype5", compute_nic(5, 0, 0, 0, 0), 11);
    ASSERT_EQ_UINT("nic metype9", compute_nic(9, 0, 0, 0, 0), 11);
    ASSERT_EQ_UINT("nic metype20", compute_nic(20, 0, 0, 0, 0), 11);

    // metype 6/10/21 -> NIC 10
    ASSERT_EQ_UINT("nic metype6", compute_nic(6, 0, 0, 0, 0), 10);
    ASSERT_EQ_UINT("nic metype10", compute_nic(10, 0, 0, 0, 0), 10);
    ASSERT_EQ_UINT("nic metype21", compute_nic(21, 0, 0, 0, 0), 10);

    // metype 7 v2 with nic_a=1,nic_c=0 -> 9; otherwise -> 8
    ASSERT_EQ_UINT("nic me7 v2 a1c0", compute_nic(7, 2, 1, 0, 0), 9);
    ASSERT_EQ_UINT("nic me7 v2 a0c0", compute_nic(7, 2, 0, 0, 0), 8);
    ASSERT_EQ_UINT("nic me7 v2 a1c1", compute_nic(7, 2, 1, 0, 1), 8);

    // metype 7 v1 with nic_a -> 9 or 8
    ASSERT_EQ_UINT("nic me7 v1 a1", compute_nic(7, 1, 1, 0, 0), 9);
    ASSERT_EQ_UINT("nic me7 v1 a0", compute_nic(7, 1, 0, 0, 0), 8);

    // metype 7 v0 -> 8
    ASSERT_EQ_UINT("nic me7 v0", compute_nic(7, 0, 0, 0, 0), 8);

    // metype 8 v2 with nic_a+nic_c combos -> 7/6/0
    ASSERT_EQ_UINT("nic me8 v2 a1c1", compute_nic(8, 2, 1, 0, 1), 7);
    ASSERT_EQ_UINT("nic me8 v2 a1c0", compute_nic(8, 2, 1, 0, 0), 6);
    ASSERT_EQ_UINT("nic me8 v2 a0c1", compute_nic(8, 2, 0, 0, 1), 6);
    ASSERT_EQ_UINT("nic me8 v2 a0c0", compute_nic(8, 2, 0, 0, 0), 0);

    // metype 8 v0/v1 -> 0
    ASSERT_EQ_UINT("nic me8 v0", compute_nic(8, 0, 0, 0, 0), 0);
    ASSERT_EQ_UINT("nic me8 v1", compute_nic(8, 1, 0, 0, 0), 0);

    // metype 11 v2 with nic_a+nic_b -> 9 or 8
    ASSERT_EQ_UINT("nic me11 v2 ab", compute_nic(11, 2, 1, 1, 0), 9);
    ASSERT_EQ_UINT("nic me11 v2 !ab", compute_nic(11, 2, 0, 0, 0), 8);

    // metype 11 v1 with nic_a -> 9 or 8
    ASSERT_EQ_UINT("nic me11 v1 a1", compute_nic(11, 1, 1, 0, 0), 9);
    ASSERT_EQ_UINT("nic me11 v1 a0", compute_nic(11, 1, 0, 0, 0), 8);

    // metype 11 v0 -> 8
    ASSERT_EQ_UINT("nic me11 v0", compute_nic(11, 0, 0, 0, 0), 8);

    // metype 12 -> 7
    ASSERT_EQ_UINT("nic metype12", compute_nic(12, 0, 0, 0, 0), 7);

    // metype 13 -> 6
    ASSERT_EQ_UINT("nic metype13", compute_nic(13, 0, 0, 0, 0), 6);

    // metype 14 -> 5
    ASSERT_EQ_UINT("nic metype14", compute_nic(14, 0, 0, 0, 0), 5);

    // metype 15 -> 4
    ASSERT_EQ_UINT("nic metype15", compute_nic(15, 0, 0, 0, 0), 4);

    // metype 16 with nic_a+nic_b -> 3 else 2
    ASSERT_EQ_UINT("nic me16 ab", compute_nic(16, 0, 1, 1, 0), 3);
    ASSERT_EQ_UINT("nic me16 !ab", compute_nic(16, 0, 0, 0, 0), 2);

    // metype 17 -> 1
    ASSERT_EQ_UINT("nic metype17", compute_nic(17, 0, 0, 0, 0), 1);

    // default -> 0
    ASSERT_EQ_UINT("nic default", compute_nic(0, 0, 0, 0, 0), 0);
    ASSERT_EQ_UINT("nic metype99", compute_nic(99, 0, 0, 0, 0), 0);

    fprintf(stderr, "testComputeNic: done\n\n");
}

// ---- testComputeRc ----

static void testComputeRc(void) {
    fprintf(stderr, "=== testComputeRc ===\n");

    // metype 5/9/20 -> RC 8
    ASSERT_EQ_UINT("rc metype5", compute_rc(5, 0, 0, 0, 0), 8);
    ASSERT_EQ_UINT("rc metype9", compute_rc(9, 0, 0, 0, 0), 8);
    ASSERT_EQ_UINT("rc metype20", compute_rc(20, 0, 0, 0, 0), 8);

    // metype 6/10/21 -> RC 25
    ASSERT_EQ_UINT("rc metype6", compute_rc(6, 0, 0, 0, 0), 25);
    ASSERT_EQ_UINT("rc metype10", compute_rc(10, 0, 0, 0, 0), 25);
    ASSERT_EQ_UINT("rc metype21", compute_rc(21, 0, 0, 0, 0), 25);

    // metype 7 v2 nic_a=1,nic_c=0 -> 75; otherwise -> 186
    ASSERT_EQ_UINT("rc me7 v2 a1c0", compute_rc(7, 2, 1, 0, 0), 75);
    ASSERT_EQ_UINT("rc me7 v2 a0c0", compute_rc(7, 2, 0, 0, 0), 186);

    // metype 7 v1
    ASSERT_EQ_UINT("rc me7 v1 a1", compute_rc(7, 1, 1, 0, 0), 75);
    ASSERT_EQ_UINT("rc me7 v1 a0", compute_rc(7, 1, 0, 0, 0), 186);

    // metype 7 v0 -> 186
    ASSERT_EQ_UINT("rc me7 v0", compute_rc(7, 0, 0, 0, 0), 186);

    // metype 8 v2 combos -> 371/556/926/RC_UNKNOWN
    ASSERT_EQ_UINT("rc me8 v2 a1c1", compute_rc(8, 2, 1, 0, 1), 371);
    ASSERT_EQ_UINT("rc me8 v2 a1c0", compute_rc(8, 2, 1, 0, 0), 556);
    ASSERT_EQ_UINT("rc me8 v2 a0c1", compute_rc(8, 2, 0, 0, 1), 926);
    ASSERT_EQ_UINT("rc me8 v2 a0c0", compute_rc(8, 2, 0, 0, 0), RC_UNKNOWN);
    ASSERT_EQ_UINT("rc me8 v0", compute_rc(8, 0, 0, 0, 0), RC_UNKNOWN);

    // metype 11 v2 nic_a+nic_b -> 75 else 186
    ASSERT_EQ_UINT("rc me11 v2 ab", compute_rc(11, 2, 1, 1, 0), 75);
    ASSERT_EQ_UINT("rc me11 v2 !ab", compute_rc(11, 2, 0, 0, 0), 186);

    // metype 12 -> 371
    ASSERT_EQ_UINT("rc metype12", compute_rc(12, 0, 0, 0, 0), 371);

    // metype 13 v2 combos
    ASSERT_EQ_UINT("rc me13 v2 !a b", compute_rc(13, 2, 0, 1, 0), 556);
    ASSERT_EQ_UINT("rc me13 v2 !a !b", compute_rc(13, 2, 0, 0, 0), 926);
    ASSERT_EQ_UINT("rc me13 v2 a b", compute_rc(13, 2, 1, 1, 0), 1112);
    ASSERT_EQ_UINT("rc me13 v2 a !b", compute_rc(13, 2, 1, 0, 0), RC_UNKNOWN);

    // metype 13 v1
    ASSERT_EQ_UINT("rc me13 v1 a1", compute_rc(13, 1, 1, 0, 0), 1112);
    ASSERT_EQ_UINT("rc me13 v1 a0", compute_rc(13, 1, 0, 0, 0), 926);

    // metype 13 v0
    ASSERT_EQ_UINT("rc me13 v0", compute_rc(13, 0, 0, 0, 0), 926);

    // metype 14 -> 1852
    ASSERT_EQ_UINT("rc metype14", compute_rc(14, 0, 0, 0, 0), 1852);

    // metype 15 -> 3704
    ASSERT_EQ_UINT("rc metype15", compute_rc(15, 0, 0, 0, 0), 3704);

    // metype 16 v2 nic_a+nic_b -> 7408 else 14816
    ASSERT_EQ_UINT("rc me16 v2 ab", compute_rc(16, 2, 1, 1, 0), 7408);
    ASSERT_EQ_UINT("rc me16 v2 !ab", compute_rc(16, 2, 0, 0, 0), 14816);

    // metype 16 v1
    ASSERT_EQ_UINT("rc me16 v1 a1", compute_rc(16, 1, 1, 0, 0), 7408);
    ASSERT_EQ_UINT("rc me16 v1 a0", compute_rc(16, 1, 0, 0, 0), 14816);

    // metype 16 v0 -> 18520
    ASSERT_EQ_UINT("rc me16 v0", compute_rc(16, 0, 0, 0, 0), 18520);

    // metype 17 -> 37040
    ASSERT_EQ_UINT("rc metype17", compute_rc(17, 0, 0, 0, 0), 37040);

    // default -> RC_UNKNOWN
    ASSERT_EQ_UINT("rc default", compute_rc(0, 0, 0, 0, 0), RC_UNKNOWN);

    fprintf(stderr, "testComputeRc: done\n\n");
}

// ---- testAltitudeToFeet ----

static void testAltitudeToFeet(void) {
    fprintf(stderr, "=== testAltitudeToFeet ===\n");

    // UNIT_FEET identity
    ASSERT_EQ_INT("feet identity", altitude_to_feet(35000, UNIT_FEET), 35000);
    ASSERT_EQ_INT("feet zero", altitude_to_feet(0, UNIT_FEET), 0);
    ASSERT_EQ_INT("feet negative", altitude_to_feet(-500, UNIT_FEET), -500);

    // UNIT_METERS conversion (100m -> 328ft)
    ASSERT_EQ_INT("meters 100m", altitude_to_feet(100, UNIT_METERS), 328);

    // UNIT_METERS conversion (10668m -> 35000ft)
    {
        int result = altitude_to_feet(10668, UNIT_METERS);
        ASSERT_TRUE("meters 10668m ~35000ft", abs(result - 35000) < 5);
    }

    // Default -> 0
    ASSERT_EQ_INT("alt default unit", altitude_to_feet(99999, 99), 0);

    fprintf(stderr, "testAltitudeToFeet: done\n\n");
}

// ---- testAddressReliable ----

static void testAddressReliable(void) {
    fprintf(stderr, "=== testAddressReliable ===\n");

    struct modesMessage mm;
    memset(&mm, 0, sizeof(mm));

    // DF17 -> 1
    mm.msgtype = 17; mm.IID = 0; mm.sbs_in = 0;
    ASSERT_EQ_INT("DF17 reliable", addressReliable(&mm), 1);

    // DF18 -> 1
    mm.msgtype = 18; mm.IID = 0; mm.sbs_in = 0;
    ASSERT_EQ_INT("DF18 reliable", addressReliable(&mm), 1);

    // DF11 IID=0 -> 1
    mm.msgtype = 11; mm.IID = 0; mm.sbs_in = 0;
    ASSERT_EQ_INT("DF11 IID0 reliable", addressReliable(&mm), 1);

    // DF11 IID=1 -> 0
    mm.msgtype = 11; mm.IID = 1; mm.sbs_in = 0;
    ASSERT_EQ_INT("DF11 IID1 unreliable", addressReliable(&mm), 0);

    // DF0 -> 0
    mm.msgtype = 0; mm.IID = 0; mm.sbs_in = 0;
    ASSERT_EQ_INT("DF0 unreliable", addressReliable(&mm), 0);

    // sbs_in -> 1
    mm.msgtype = 0; mm.IID = 0; mm.sbs_in = 1;
    ASSERT_EQ_INT("sbs_in reliable", addressReliable(&mm), 1);

    fprintf(stderr, "testAddressReliable: done\n\n");
}

// ---- testSimpleHash ----

static void testSimpleHash(void) {
    fprintf(stderr, "=== testSimpleHash ===\n");

    // Zero input -> non-zero (function guarantees != 0, returns 1)
    ASSERT_EQ_UINT("hash zero", simpleHash(0), 1);

    // Non-zero input -> some non-zero value
    ASSERT_TRUE("hash one nonzero", simpleHash(1) != 0);

    // All-ones input
    ASSERT_TRUE("hash allones nonzero", simpleHash(0xFFFFFFFFFFFFFFFFULL) != 0);

    // Different inputs -> likely different outputs
    uint16_t h1 = simpleHash(0x1234567890ABCDEFULL);
    uint16_t h2 = simpleHash(0xFEDCBA0987654321ULL);
    ASSERT_TRUE("hash diff inputs diff", h1 != h2);

    fprintf(stderr, "testSimpleHash: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testComputeNic();
    testComputeRc();
    testAltitudeToFeet();
    testAddressReliable();
    testSimpleHash();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
