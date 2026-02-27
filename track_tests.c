// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// track_tests.c - unit tests for static functions in track.c
//
// Uses #include "track.c" to access static functions directly.
// Provides linker stubs for external symbols that track.c references
// but that we don't exercise in the tests.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
struct _Threads Threads;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

void setExit(int __attribute__((unused)) arg) { }
int64_t mstime(void) { return 1700000000000LL; }

// aircraft.c stubs
// Return static dummy to avoid -O2 NULL-dereference warnings from inlined code
static struct aircraft dummy_aircraft;
struct aircraft *aircraftCreate(uint32_t __attribute__((unused)) addr) { return &dummy_aircraft; }
struct aircraft *aircraftGet(uint32_t __attribute__((unused)) addr) { return &dummy_aircraft; }
void freeAircraft(struct aircraft __attribute__((unused)) *a) { }
void quickInit(void) { }

// cpr.c stubs
int decodeCPRairborne(int __attribute__((unused)) even_cprlat,
                      int __attribute__((unused)) even_cprlon,
                      int __attribute__((unused)) odd_cprlat,
                      int __attribute__((unused)) odd_cprlon,
                      int __attribute__((unused)) fflag,
                      double __attribute__((unused)) *out_lat,
                      double __attribute__((unused)) *out_lon) { return -1; }
int decodeCPRsurface(double __attribute__((unused)) reflat,
                     double __attribute__((unused)) reflon,
                     int __attribute__((unused)) even_cprlat,
                     int __attribute__((unused)) even_cprlon,
                     int __attribute__((unused)) odd_cprlat,
                     int __attribute__((unused)) odd_cprlon,
                     int __attribute__((unused)) fflag,
                     double __attribute__((unused)) *out_lat,
                     double __attribute__((unused)) *out_lon) { return -1; }
int decodeCPRrelative(double __attribute__((unused)) reflat,
                      double __attribute__((unused)) reflon,
                      int __attribute__((unused)) cprlat,
                      int __attribute__((unused)) cprlon,
                      int __attribute__((unused)) fflag,
                      int __attribute__((unused)) surface,
                      double __attribute__((unused)) *out_lat,
                      double __attribute__((unused)) *out_lon) { return -1; }

// globe_index.c stubs
int globe_index(double __attribute__((unused)) lat_in,
                double __attribute__((unused)) lon_in) { return 0; }
void set_globe_index(struct aircraft __attribute__((unused)) *a,
                     int __attribute__((unused)) new_index) { }
void ca_lock_read(struct craftArray __attribute__((unused)) *ca) { }
void ca_unlock_read(struct craftArray __attribute__((unused)) *ca) { }
void ca_add(struct craftArray __attribute__((unused)) *ca,
            struct aircraft __attribute__((unused)) *a) { }
int traceAdd(struct aircraft __attribute__((unused)) *a,
             struct modesMessage __attribute__((unused)) *mm,
             int64_t __attribute__((unused)) now,
             int __attribute__((unused)) stale) { return 0; }
void traceMaintenance(struct aircraft __attribute__((unused)) *a,
                      int64_t __attribute__((unused)) now,
                      threadpool_buffer_t __attribute__((unused)) *passbuffer) { }
int traceUsePosBuffered(struct aircraft __attribute__((unused)) *a) { return 0; }

// receiver.c stubs
struct receiver *receiverBad(uint64_t __attribute__((unused)) id,
                             uint32_t __attribute__((unused)) addr,
                             int64_t __attribute__((unused)) now) { return NULL; }
struct receiver *receiverGetReference(uint64_t __attribute__((unused)) id,
                                      double __attribute__((unused)) *lat,
                                      double __attribute__((unused)) *lon,
                                      struct aircraft __attribute__((unused)) *a,
                                      int __attribute__((unused)) noDebug) { return NULL; }
int receiverPositionReceived(struct aircraft __attribute__((unused)) *a,
                             struct modesMessage __attribute__((unused)) *mm,
                             double __attribute__((unused)) lat,
                             double __attribute__((unused)) lon,
                             int64_t __attribute__((unused)) now) { return 0; }

// json_out.c stubs
int includeAircraftJson(int64_t __attribute__((unused)) now,
                        struct aircraft __attribute__((unused)) *a) { return 0; }
void logACASInfoShort(uint32_t __attribute__((unused)) addr,
                      unsigned char __attribute__((unused)) *MV,
                      struct aircraft __attribute__((unused)) *a,
                      struct modesMessage __attribute__((unused)) *mm,
                      int64_t __attribute__((unused)) now) { }

// mode_s.c stubs
void displayModesMessage(struct modesMessage __attribute__((unused)) *mm) { }
unsigned modeCToModeA(int __attribute__((unused)) modeC) { return 0; }

// comm_b.c stubs
int checkAcasRaValid(unsigned char __attribute__((unused)) *MV,
                     struct modesMessage __attribute__((unused)) *mm,
                     int __attribute__((unused)) debug) { return 0; }

// util.c stubs
double greatcircle(double __attribute__((unused)) lat0,
                   double __attribute__((unused)) lon0,
                   double __attribute__((unused)) lat1,
                   double __attribute__((unused)) lon1,
                   int __attribute__((unused)) approx) { return 0; }
double bearing(double __attribute__((unused)) lat0,
               double __attribute__((unused)) lon0,
               double __attribute__((unused)) lat1,
               double __attribute__((unused)) lon1) { return 0; }
char *sprint_uuid1(uint64_t __attribute__((unused)) id1, char *p) { return p; }

// geomag.c stubs
int geomag_calc(double __attribute__((unused)) alt,
                double __attribute__((unused)) glat,
                double __attribute__((unused)) glon,
                double __attribute__((unused)) time,
                double *dec, double *dip,
                double *ti, double *gv) {
    if (dec) *dec = 0;
    if (dip) *dip = 0;
    if (ti) *ti = 0;
    if (gv) *gv = 0;
    return 1;
}

// threadpool.c stubs
void threadpool_run(threadpool_t __attribute__((unused)) *pool,
                    threadpool_task_t __attribute__((unused)) *tasks,
                    uint32_t __attribute__((unused)) count) { }

// ---- Include track.c to access static functions ----
#include "track.c"

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

#define ASSERT_STR_EQ(tag, got, expected) do { \
    if (strcmp((got), (expected)) != 0) { \
        fprintf(stderr, "%s: FAIL: got \"%s\", expected \"%s\"\n", tag, (got), (expected)); \
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

#define ASSERT_EQ_I32(tag, got, expected) do { \
    int32_t _g = (got), _e = (expected); \
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

#define ASSERT_EQ_U16(tag, got, expected) do { \
    uint16_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %u, expected %u\n", tag, _g, _e); \
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

// ---- testSourceString ----

static void testSourceString(void) {
    fprintf(stderr, "=== testSourceString ===\n");

    ASSERT_STR_EQ("src INVALID", source_string(SOURCE_INVALID), "INVALID ");
    ASSERT_STR_EQ("src INDIRECT", source_string(SOURCE_INDIRECT), "INDIRECT");
    ASSERT_STR_EQ("src MODE_AC", source_string(SOURCE_MODE_AC), "MODE_AC ");
    ASSERT_STR_EQ("src SBS", source_string(SOURCE_SBS), "SBS     ");
    ASSERT_STR_EQ("src MLAT", source_string(SOURCE_MLAT), "MLAT    ");
    ASSERT_STR_EQ("src MODE_S", source_string(SOURCE_MODE_S), "MODE_S  ");
    ASSERT_STR_EQ("src JAERO", source_string(SOURCE_JAERO), "JAERO   ");
    ASSERT_STR_EQ("src MODE_CH", source_string(SOURCE_MODE_S_CHECKED), "MODE_CH ");
    ASSERT_STR_EQ("src TISB", source_string(SOURCE_TISB), "TISB    ");
    ASSERT_STR_EQ("src ADSR", source_string(SOURCE_ADSR), "ADSR    ");
    ASSERT_STR_EQ("src ADSB", source_string(SOURCE_ADSB), "ADSB    ");
    ASSERT_STR_EQ("src PRIO", source_string(SOURCE_PRIO), "PRIO    ");
    ASSERT_STR_EQ("src default", source_string(99), "ERROR   ");

    fprintf(stderr, "testSourceString: done\n\n");
}

// ---- testCurrentReduceInterval ----

static void testCurrentReduceInterval(void) {
    fprintf(stderr, "=== testCurrentReduceInterval ===\n");

    // No doubling: doubleBeastReduceIntervalUntil <= now
    Modes.net_output_beast_reduce_interval = 1000;
    Modes.doubleBeastReduceIntervalUntil = 0;
    ASSERT_EQ_I32("reduce no double", currentReduceInterval(5000), 1000);

    // Active doubling: doubleBeastReduceIntervalUntil > now
    Modes.net_output_beast_reduce_interval = 1000;
    Modes.doubleBeastReduceIntervalUntil = 10000;
    ASSERT_EQ_I32("reduce doubled", currentReduceInterval(5000), 2000);

    // Exact boundary: doubleBeastReduceIntervalUntil == now → no doubling
    Modes.net_output_beast_reduce_interval = 1000;
    Modes.doubleBeastReduceIntervalUntil = 5000;
    ASSERT_EQ_I32("reduce boundary", currentReduceInterval(5000), 1000);

    // Cleanup
    Modes.net_output_beast_reduce_interval = 0;
    Modes.doubleBeastReduceIntervalUntil = 0;

    fprintf(stderr, "testCurrentReduceInterval: done\n\n");
}

// ---- testCalculateMessageRate ----

static void testCalculateMessageRate(void) {
    fprintf(stderr, "=== testCalculateMessageRate ===\n");

    struct aircraft a;
    memset(&a, 0, sizeof(a));

    // REMOVE_STALE_INTERVAL = 1*SECONDS = 1000
    // mult starts at REMOVE_STALE_INTERVAL/1000.0 = 1.0
    // For MESSAGE_RATE_CALC_POINTS=2:
    //   k=0: sum += acc[0]*1.0,  multSum += 1.0,  mult *= 0.7 -> 0.7
    //   k=1: sum += acc[1]*0.7,  multSum += 0.7
    // rate = sum / multSum * messageRateMult

    // Zero counts -> rate 0
    a.messageRateAcc[0] = 0;
    a.messageRateAcc[1] = 0;
    Modes.messageRateMult = 1.0f;
    calculateMessageRate(&a, 1000000);
    ASSERT_FLOAT_NEAR("msgrate zero", a.messageRate, 0.0, 0.001);

    // Known counts: acc={100,50}, mult=1.0
    // sum = 100*1.0 + 50*0.7 = 135.0
    // multSum = 1.0 + 0.7 = 1.7
    // rate = 135.0/1.7 = 79.4117...
    a.messageRateAcc[0] = 100;
    a.messageRateAcc[1] = 50;
    Modes.messageRateMult = 1.0f;
    calculateMessageRate(&a, 1000000);
    ASSERT_FLOAT_NEAR("msgrate known", a.messageRate, 79.4117, 0.01);

    // Verify shift: acc[1] should now be 100, acc[0] should be 0
    ASSERT_EQ_U16("msgrate shift[1]", a.messageRateAcc[1], 100);
    ASSERT_EQ_U16("msgrate shift[0]", a.messageRateAcc[0], 0);

    // Mult factor: acc={100,50}, messageRateMult=2.0
    a.messageRateAcc[0] = 100;
    a.messageRateAcc[1] = 50;
    Modes.messageRateMult = 2.0f;
    calculateMessageRate(&a, 2000000);
    ASSERT_FLOAT_NEAR("msgrate mult2", a.messageRate, 158.8235, 0.01);

    // Verify nextMessageRateCalc set
    ASSERT_EQ_I64("msgrate next", a.nextMessageRateCalc, 2000000 + REMOVE_STALE_INTERVAL);

    // Cleanup
    Modes.messageRateMult = 0;

    fprintf(stderr, "testCalculateMessageRate: done\n\n");
}

// ---- testCalculateMessageRateGlobal ----

static void testCalculateMessageRateGlobal(void) {
    fprintf(stderr, "=== testCalculateMessageRateGlobal ===\n");

    // Known values: acc={200,100}, messageRateMult=1.0
    // sum = 200*1.0 + 100*0.7 = 270.0
    // multSum = 1.7
    // rate = 270.0/1.7 = 158.8235...
    Modes.messageRateAcc[0] = 200;
    Modes.messageRateAcc[1] = 100;
    Modes.messageRateMult = 1.0f;
    int64_t now = 5000000;
    calculateMessageRateGlobal(now);
    ASSERT_FLOAT_NEAR("global rate", Modes.messageRate, 158.8235, 0.01);

    // Verify shift: acc[1] should be 200 (shifted from [0]), acc[0] should be 0
    ASSERT_EQ_U16("global shift[1]", Modes.messageRateAcc[1], 200);
    ASSERT_EQ_U16("global shift[0]", Modes.messageRateAcc[0], 0);

    // Verify nextMessageRateCalc
    ASSERT_EQ_I64("global next", Modes.nextMessageRateCalc, now + REMOVE_STALE_INTERVAL);

    // Cleanup
    Modes.messageRate = 0;
    Modes.messageRateMult = 0;
    memset(Modes.messageRateAcc, 0, sizeof(Modes.messageRateAcc));

    fprintf(stderr, "testCalculateMessageRateGlobal: done\n\n");
}

// ---- testWillAcceptData ----

static void testWillAcceptData(void) {
    fprintf(stderr, "=== testWillAcceptData ===\n");

    struct aircraft a;
    struct modesMessage mm;
    data_validity d;

    // Disable receiver ID checking for these tests
    Modes.netReceiverId = 0;

    // SOURCE_INVALID -> reject
    {
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        mm.sysTimestamp = 1000000;
        ASSERT_EQ_INT("accept invalid", will_accept_data(&d, SOURCE_INVALID, &mm, &a), 0);
    }

    // Timestamp regression: now < d->updated -> reject
    {
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        d.updated = 2000000;
        mm.sysTimestamp = 1000000; // before updated
        ASSERT_EQ_INT("accept time regress", will_accept_data(&d, SOURCE_ADSB, &mm, &a), 0);
    }

    // Lower priority + fresh data -> reject
    {
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        d.source = SOURCE_ADSB;
        d.updated = 1000000;
        mm.sysTimestamp = 1000000 + TRACK_STALE - 1; // just before stale
        ASSERT_EQ_INT("accept lower fresh", will_accept_data(&d, SOURCE_MODE_S, &mm, &a), 0);
    }

    // Lower priority + stale data -> accept
    {
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        d.source = SOURCE_ADSB;
        d.updated = 1000000;
        mm.sysTimestamp = 1000000 + TRACK_STALE; // exactly stale
        ASSERT_EQ_INT("accept lower stale", will_accept_data(&d, SOURCE_MODE_S, &mm, &a), 1);
    }

    // Same priority + same/later timestamp -> accept
    {
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        d.source = SOURCE_ADSB;
        d.updated = 1000000;
        mm.sysTimestamp = 1000000; // same time
        ASSERT_EQ_INT("accept same prio", will_accept_data(&d, SOURCE_ADSB, &mm, &a), 1);
    }

    fprintf(stderr, "testWillAcceptData: done\n\n");
}

// ---- testTimeBetween ----

static void testTimeBetween(void) {
    fprintf(stderr, "=== testTimeBetween ===\n");

    // t1 > t2
    ASSERT_EQ_I64("time t1>t2", time_between(1000, 700), 300);

    // t1 < t2
    ASSERT_EQ_I64("time t1<t2", time_between(500, 900), 400);

    // t1 == t2
    ASSERT_EQ_I64("time t1==t2", time_between(1234, 1234), 0);

    fprintf(stderr, "testTimeBetween: done\n\n");
}

// ---- testCombineValidity ----

static void testCombineValidity(void) {
    fprintf(stderr, "=== testCombineValidity ===\n");

    int64_t now = 1000000;

    // from1 SOURCE_INVALID -> result = *from2
    {
        data_validity to, from1, from2;
        memset(&to, 0, sizeof(to));
        memset(&from1, 0, sizeof(from1));
        memset(&from2, 0, sizeof(from2));
        from1.source = SOURCE_INVALID;
        from2.source = SOURCE_ADSB;
        from2.updated = 500;
        combine_validity(&to, &from1, &from2, now);
        ASSERT_EQ_INT("comb inv1 source", to.source, SOURCE_ADSB);
        ASSERT_EQ_I64("comb inv1 updated", to.updated, 500);
    }

    // from2 SOURCE_INVALID -> result = *from1
    {
        data_validity to, from1, from2;
        memset(&to, 0, sizeof(to));
        memset(&from1, 0, sizeof(from1));
        memset(&from2, 0, sizeof(from2));
        from1.source = SOURCE_MLAT;
        from1.updated = 800;
        from2.source = SOURCE_INVALID;
        combine_validity(&to, &from1, &from2, now);
        ASSERT_EQ_INT("comb inv2 source", to.source, SOURCE_MLAT);
        ASSERT_EQ_I64("comb inv2 updated", to.updated, 800);
    }

    // Both valid, different sources: takes worse source, later timestamp
    {
        data_validity to, from1, from2;
        memset(&to, 0, sizeof(to));
        memset(&from1, 0, sizeof(from1));
        memset(&from2, 0, sizeof(from2));
        from1.source = SOURCE_ADSB;    // better
        from1.updated = 100;
        from2.source = SOURCE_MLAT;    // worse
        from2.updated = 200;
        combine_validity(&to, &from1, &from2, now);
        ASSERT_EQ_INT("comb both source", to.source, SOURCE_MLAT); // worse
        ASSERT_EQ_I64("comb both updated", to.updated, 200);       // later
    }

    // Stale calculation: now > updated + TRACK_STALE
    {
        data_validity to, from1, from2;
        memset(&to, 0, sizeof(to));
        memset(&from1, 0, sizeof(from1));
        memset(&from2, 0, sizeof(from2));
        from1.source = SOURCE_ADSB;
        from1.updated = 100;
        from2.source = SOURCE_MLAT;
        from2.updated = 200;
        int64_t future_now = 200 + TRACK_STALE + 1; // past stale threshold
        combine_validity(&to, &from1, &from2, future_now);
        ASSERT_EQ_INT("comb stale", to.stale, 1);
    }

    fprintf(stderr, "testCombineValidity: done\n\n");
}

// ---- testCompareValidity ----

static void testCompareValidity(void) {
    fprintf(stderr, "=== testCompareValidity ===\n");

    // lhs better source, not stale -> returns 1
    {
        data_validity lhs, rhs;
        memset(&lhs, 0, sizeof(lhs));
        memset(&rhs, 0, sizeof(rhs));
        lhs.source = SOURCE_ADSB;
        lhs.stale = 0;
        rhs.source = SOURCE_MLAT;
        ASSERT_EQ_INT("compare lhs better", compare_validity(&lhs, &rhs), 1);
    }

    // rhs better source, not stale -> returns -1
    {
        data_validity lhs, rhs;
        memset(&lhs, 0, sizeof(lhs));
        memset(&rhs, 0, sizeof(rhs));
        lhs.source = SOURCE_MLAT;
        rhs.source = SOURCE_ADSB;
        rhs.stale = 0;
        ASSERT_EQ_INT("compare rhs better", compare_validity(&lhs, &rhs), -1);
    }

    // Equal source, lhs newer -> returns 1
    {
        data_validity lhs, rhs;
        memset(&lhs, 0, sizeof(lhs));
        memset(&rhs, 0, sizeof(rhs));
        lhs.source = SOURCE_ADSB;
        lhs.stale = 0;
        lhs.updated = 200;
        rhs.source = SOURCE_ADSB;
        rhs.stale = 0;
        rhs.updated = 100;
        ASSERT_EQ_INT("compare lhs newer", compare_validity(&lhs, &rhs), 1);
    }

    // Equal source, rhs newer -> returns -1
    {
        data_validity lhs, rhs;
        memset(&lhs, 0, sizeof(lhs));
        memset(&rhs, 0, sizeof(rhs));
        lhs.source = SOURCE_ADSB;
        lhs.stale = 0;
        lhs.updated = 100;
        rhs.source = SOURCE_ADSB;
        rhs.stale = 0;
        rhs.updated = 200;
        ASSERT_EQ_INT("compare rhs newer", compare_validity(&lhs, &rhs), -1);
    }

    fprintf(stderr, "testCompareValidity: done\n\n");
}

// ---- testCprGlobalAirborneMaxElapsed ----

static void testCprGlobalAirborneMaxElapsed(void) {
    fprintf(stderr, "=== testCprGlobalAirborneMaxElapsed ===\n");

    struct aircraft a;
    int64_t now = 1000000;

    // Stale gs (>20s old): returns 10*SECONDS
    {
        memset(&a, 0, sizeof(a));
        a.gs_valid.source = SOURCE_ADSB;
        a.gs_valid.updated = now - 21 * SECONDS; // 21s old
        a.gs = 500;
        ASSERT_EQ_I64("cpr stale gs", cpr_global_airborne_max_elapsed(now, &a), 10 * SECONDS);
    }

    // 500kt gs, fresh: returns 19*SECONDS (ref=19s, 19*500/500=19s)
    {
        memset(&a, 0, sizeof(a));
        a.gs_valid.source = SOURCE_ADSB;
        a.gs_valid.updated = now - 5 * SECONDS; // 5s old, fresh
        a.gs = 500;
        ASSERT_EQ_I64("cpr 500kt", cpr_global_airborne_max_elapsed(now, &a), 19 * SECONDS);
    }

    // 250kt (slow, capped at 30s): min(30s, 19*500/250=38s) = 30s
    {
        memset(&a, 0, sizeof(a));
        a.gs_valid.source = SOURCE_ADSB;
        a.gs_valid.updated = now - 5 * SECONDS;
        a.gs = 250;
        ASSERT_EQ_I64("cpr 250kt", cpr_global_airborne_max_elapsed(now, &a), 30 * SECONDS);
    }

    // 1000kt (fast): 19*500/1000 = 9500ms = 9.5s
    {
        memset(&a, 0, sizeof(a));
        a.gs_valid.source = SOURCE_ADSB;
        a.gs_valid.updated = now - 5 * SECONDS;
        a.gs = 1000;
        ASSERT_EQ_I64("cpr 1000kt", cpr_global_airborne_max_elapsed(now, &a), 9500);
    }

    fprintf(stderr, "testCprGlobalAirborneMaxElapsed: done\n\n");
}

// ---- testComputeV0Nacp ----

static void testComputeV0Nacp(void) {
    fprintf(stderr, "=== testComputeV0Nacp ===\n");

    struct modesMessage mm;

    // msgtype != 17 or 18 -> -1
    memset(&mm, 0, sizeof(mm));
    mm.msgtype = 11;
    mm.metype = 5;
    ASSERT_EQ_INT("v0nacp wrong msgtype", compute_v0_nacp(&mm), -1);

    // msgtype=17, metype=0 -> 0
    memset(&mm, 0, sizeof(mm));
    mm.msgtype = 17;
    mm.metype = 0;
    ASSERT_EQ_INT("v0nacp me0", compute_v0_nacp(&mm), 0);

    // metype=5 -> 11
    mm.metype = 5;
    ASSERT_EQ_INT("v0nacp me5", compute_v0_nacp(&mm), 11);

    // metype=8 -> 0
    mm.metype = 8;
    ASSERT_EQ_INT("v0nacp me8", compute_v0_nacp(&mm), 0);

    // metype=13 -> 6
    mm.metype = 13;
    ASSERT_EQ_INT("v0nacp me13", compute_v0_nacp(&mm), 6);

    // metype=20 -> 11
    mm.metype = 20;
    ASSERT_EQ_INT("v0nacp me20", compute_v0_nacp(&mm), 11);

    // msgtype=18 works too
    mm.msgtype = 18;
    mm.metype = 5;
    ASSERT_EQ_INT("v0nacp mt18 me5", compute_v0_nacp(&mm), 11);

    // default -> -1
    mm.msgtype = 17;
    mm.metype = 99;
    ASSERT_EQ_INT("v0nacp default", compute_v0_nacp(&mm), -1);

    fprintf(stderr, "testComputeV0Nacp: done\n\n");
}

// ---- testComputeV0Sil ----

static void testComputeV0Sil(void) {
    fprintf(stderr, "=== testComputeV0Sil ===\n");

    struct modesMessage mm;

    // msgtype != 17/18 -> -1
    memset(&mm, 0, sizeof(mm));
    mm.msgtype = 11;
    mm.metype = 5;
    ASSERT_EQ_INT("v0sil wrong msgtype", compute_v0_sil(&mm), -1);

    // msgtype=17, metype=0 -> 0
    memset(&mm, 0, sizeof(mm));
    mm.msgtype = 17;
    mm.metype = 0;
    ASSERT_EQ_INT("v0sil me0", compute_v0_sil(&mm), 0);

    // metype=5 -> 2
    mm.metype = 5;
    ASSERT_EQ_INT("v0sil me5", compute_v0_sil(&mm), 2);

    // metype=17 -> 2
    mm.metype = 17;
    ASSERT_EQ_INT("v0sil me17", compute_v0_sil(&mm), 2);

    // metype=18 -> 0
    mm.metype = 18;
    ASSERT_EQ_INT("v0sil me18", compute_v0_sil(&mm), 0);

    // metype=20 -> 2
    mm.metype = 20;
    ASSERT_EQ_INT("v0sil me20", compute_v0_sil(&mm), 2);

    // metype=22 -> 0
    mm.metype = 22;
    ASSERT_EQ_INT("v0sil me22", compute_v0_sil(&mm), 0);

    // default -> -1
    mm.metype = 99;
    ASSERT_EQ_INT("v0sil default", compute_v0_sil(&mm), -1);

    fprintf(stderr, "testComputeV0Sil: done\n\n");
}

// ---- testDuplicateCheck ----

static void testDuplicateCheck(void) {
    fprintf(stderr, "=== testDuplicateCheck ===\n");

    int64_t now = 1700000000000LL;

    // Case 1: Old position (>2s) -> returns 0
    {
        struct aircraft a;
        struct modesMessage mm;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        a.seen_pos = now - 3 * SECONDS;
        a.lat = 1.0; a.lon = 1.0;
        ASSERT_EQ_INT("dup old pos", duplicate_check(now, &a, 1.0, 1.0, &mm), 0);
    }

    // Case 2: Current pos matches within 2s -> returns 1
    {
        struct aircraft a;
        struct modesMessage mm;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        a.seen_pos = now;
        a.lat = 1.0; a.lon = 1.0;
        int ret = duplicate_check(now, &a, 1.0, 1.0, &mm);
        ASSERT_EQ_INT("dup match ret", ret, 1);
        ASSERT_EQ_INT("dup match flag", mm.duplicate, 1);
    }

    // Case 3: Prev pos matches within 2s -> returns 1
    {
        struct aircraft a;
        struct modesMessage mm;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        a.seen_pos = now;        // current is recent
        a.lat = 2.0; a.lon = 2.0;  // current doesn't match
        a.prev_lat = 1.0; a.prev_lon = 1.0;
        a.prev_pos_time = now;
        int ret = duplicate_check(now, &a, 1.0, 1.0, &mm);
        ASSERT_EQ_INT("dup prev ret", ret, 1);
        ASSERT_EQ_INT("dup prev flag", mm.duplicate, 1);
    }

    // Case 4: Already checked (duplicate_checked=1, duplicate=0) -> returns 0
    {
        struct aircraft a;
        struct modesMessage mm;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        mm.duplicate_checked = 1;
        mm.duplicate = 0;
        a.seen_pos = now;
        a.lat = 1.0; a.lon = 1.0;
        ASSERT_EQ_INT("dup checked", duplicate_check(now, &a, 1.0, 1.0, &mm), 0);
    }

    fprintf(stderr, "testDuplicateCheck: done\n\n");
}

// ---- testUat2esntDuplicate ----

static void testUat2esntDuplicate(void) {
    fprintf(stderr, "=== testUat2esntDuplicate ===\n");

    int64_t now = 1700000000000LL;

    // Case 1: All conditions met -> 1
    {
        struct aircraft a;
        struct modesMessage mm;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        mm.cpr_valid = 1;
        mm.cpr_odd = 1;
        mm.msgtype = 18;
        mm.timestamp = MAGIC_UAT_TIMESTAMP;
        a.seenPosReliable = now - 2000; // 2s ago, < 2500ms
        ASSERT_EQ_INT("uat dup all", uat2esnt_duplicate(now, &a, &mm), 1);
    }

    // Case 2: Missing cpr_odd -> 0
    {
        struct aircraft a;
        struct modesMessage mm;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        mm.cpr_valid = 1;
        mm.cpr_odd = 0; // missing
        mm.msgtype = 18;
        mm.timestamp = MAGIC_UAT_TIMESTAMP;
        a.seenPosReliable = now - 2000;
        ASSERT_EQ_INT("uat dup no odd", uat2esnt_duplicate(now, &a, &mm), 0);
    }

    // Case 3: Wrong msgtype -> 0
    {
        struct aircraft a;
        struct modesMessage mm;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        mm.cpr_valid = 1;
        mm.cpr_odd = 1;
        mm.msgtype = 17; // wrong
        mm.timestamp = MAGIC_UAT_TIMESTAMP;
        a.seenPosReliable = now - 2000;
        ASSERT_EQ_INT("uat dup wrong mt", uat2esnt_duplicate(now, &a, &mm), 0);
    }

    fprintf(stderr, "testUat2esntDuplicate: done\n\n");
}

// ---- testAcceptData ----

static void testAcceptData(void) {
    fprintf(stderr, "=== testAcceptData ===\n");

    // Disable receiver ID checking
    Modes.netReceiverId = 0;

    // Case 1: Rejected (SOURCE_INVALID)
    {
        struct aircraft a;
        struct modesMessage mm;
        data_validity d;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        mm.sysTimestamp = 1000000;
        ASSERT_EQ_INT("accept rejected", accept_data(&d, SOURCE_INVALID, &mm, &a, 0), 0);
    }

    // Case 2: Accepted, sets fields
    {
        struct aircraft a;
        struct modesMessage mm;
        data_validity d;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        d.source = SOURCE_INVALID;
        mm.sysTimestamp = 1000000;
        Modes.net_output_beast_reduce_interval = 1000;
        Modes.doubleBeastReduceIntervalUntil = 0;
        int ret = accept_data(&d, SOURCE_ADSB, &mm, &a, REDUCE_OFTEN);
        ASSERT_EQ_INT("accept ok ret", ret, 1);
        ASSERT_EQ_INT("accept ok source", d.source, SOURCE_ADSB);
        ASSERT_EQ_I64("accept ok updated", d.updated, 1000000);
        ASSERT_EQ_INT("accept ok stale", d.stale, 0);
    }

    // Case 3: SOURCE_PRIO -> d.source becomes SOURCE_ADSB
    {
        struct aircraft a;
        struct modesMessage mm;
        data_validity d;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        d.source = SOURCE_INVALID;
        mm.sysTimestamp = 2000000;
        Modes.net_output_beast_reduce_interval = 1000;
        Modes.doubleBeastReduceIntervalUntil = 0;
        accept_data(&d, SOURCE_PRIO, &mm, &a, REDUCE_OFTEN);
        ASSERT_EQ_INT("accept prio src", d.source, SOURCE_ADSB);
    }

    // Case 4: Sets next_reduce_forward (REDUCE_OFTEN: interval * 3/4 = 750)
    {
        struct aircraft a;
        struct modesMessage mm;
        data_validity d;
        memset(&a, 0, sizeof(a));
        memset(&mm, 0, sizeof(mm));
        memset(&d, 0, sizeof(d));
        d.source = SOURCE_INVALID;
        mm.sysTimestamp = 3000000;
        Modes.net_output_beast_reduce_interval = 1000;
        Modes.doubleBeastReduceIntervalUntil = 0;
        accept_data(&d, SOURCE_ADSB, &mm, &a, REDUCE_OFTEN);
        ASSERT_TRUE("accept reduce_fwd", d.next_reduce_forward > 0);
        // REDUCE_OFTEN: reduceInterval = 1000 * 3/4 = 750
        ASSERT_EQ_I64("accept reduce_val", d.next_reduce_forward, 3000000 + 750);
    }

    // Cleanup
    Modes.net_output_beast_reduce_interval = 0;

    fprintf(stderr, "testAcceptData: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testComputeNic();
    testComputeRc();
    testAltitudeToFeet();
    testAddressReliable();
    testSimpleHash();
    testSourceString();
    testCurrentReduceInterval();
    testCalculateMessageRate();
    testCalculateMessageRateGlobal();
    testWillAcceptData();
    testTimeBetween();
    testCombineValidity();
    testCompareValidity();
    testCprGlobalAirborneMaxElapsed();
    testComputeV0Nacp();
    testComputeV0Sil();
    testDuplicateCheck();
    testUat2esntDuplicate();
    testAcceptData();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
