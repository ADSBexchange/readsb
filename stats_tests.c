// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// stats_tests.c - unit tests for functions in stats.c
//
// Uses #include "stats.c" to access static functions directly.
// Provides linker stubs for external symbols that stats.c references
// but that we don't exercise in the tests.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <stdatomic.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
struct _Threads Threads;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];
void setExit(int __attribute__((unused)) arg) { }
int64_t mstime(void) { return 1700000000000LL; }

int64_t roundSeconds(int __attribute__((unused)) interval,
                     int __attribute__((unused)) offset,
                     int64_t __attribute__((unused)) epoch_ms) { return 0; }
struct char_buffer writeJsonToFile(const char __attribute__((unused)) *dir,
                                   const char __attribute__((unused)) *file,
                                   struct char_buffer __attribute__((unused)) cb) {
    return (struct char_buffer){0};
}

// stubs for statsCountAircraft dependencies
struct aircraft *aircraftGet(uint32_t __attribute__((unused)) addr) { return NULL; }
void quickInit(void) { }

// ---- Include source under test ----

#include "stats.c"

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

#define ASSERT_EQ_U32(tag, got, expected) do { \
    uint32_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %u, expected %u\n", tag, _g, _e); \
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

// ---- testAddTimespecs ----

static void testAddTimespecs(void) {
    fprintf(stderr, "=== testAddTimespecs ===\n");

    struct timespec x, y, z;

    // Simple seconds addition
    x = (struct timespec){.tv_sec = 1, .tv_nsec = 0};
    y = (struct timespec){.tv_sec = 2, .tv_nsec = 0};
    add_timespecs(&x, &y, &z);
    ASSERT_EQ_I64("simple sec", z.tv_sec, 3);
    ASSERT_EQ_I64("simple nsec", z.tv_nsec, 0);

    // Nanosecond overflow: 500M + 600M = 1s + 100M
    x = (struct timespec){.tv_sec = 0, .tv_nsec = 500000000};
    y = (struct timespec){.tv_sec = 0, .tv_nsec = 600000000};
    add_timespecs(&x, &y, &z);
    ASSERT_EQ_I64("overflow sec", z.tv_sec, 1);
    ASSERT_EQ_I64("overflow nsec", z.tv_nsec, 100000000);

    // Edge case: 999999999ns + 1ns = 1s + 0ns
    x = (struct timespec){.tv_sec = 5, .tv_nsec = 999999999};
    y = (struct timespec){.tv_sec = 0, .tv_nsec = 1};
    add_timespecs(&x, &y, &z);
    ASSERT_EQ_I64("edge sec", z.tv_sec, 6);
    ASSERT_EQ_I64("edge nsec", z.tv_nsec, 0);

    // Both zero
    x = (struct timespec){.tv_sec = 0, .tv_nsec = 0};
    y = (struct timespec){.tv_sec = 0, .tv_nsec = 0};
    add_timespecs(&x, &y, &z);
    ASSERT_EQ_I64("zero sec", z.tv_sec, 0);
    ASSERT_EQ_I64("zero nsec", z.tv_nsec, 0);

    fprintf(stderr, "testAddTimespecs: done\n\n");
}

// ---- testResetStats ----

static void testResetStats(void) {
    fprintf(stderr, "=== testResetStats ===\n");

    struct stats st;
    // Fill with non-zero data
    memset(&st, 0xFF, sizeof(st));

    reset_stats(&st);

    ASSERT_EQ_I64("start zero", st.start, 0);
    ASSERT_EQ_I64("end zero", st.end, 0);
    ASSERT_EQ_U32("messages_total zero", st.messages_total, 0);
    ASSERT_EQ_U32("demod_preambles zero", st.demod_preambles, 0);
    ASSERT_EQ_U32("unique_aircraft zero", st.unique_aircraft, 0);
    ASSERT_TRUE("distance_min set", st.distance_min == 2E42);
    ASSERT_TRUE("peak_signal_power zero", st.peak_signal_power == 0.0);

    fprintf(stderr, "testResetStats: done\n\n");
}

// ---- testAddStats ----

static void testAddStats(void) {
    fprintf(stderr, "=== testAddStats ===\n");

    struct stats s1, s2, target;
    memset(&s1, 0, sizeof(s1));
    memset(&s2, 0, sizeof(s2));
    memset(&target, 0, sizeof(target));

    // Additive fields sum
    s1.messages_total = 100;
    s2.messages_total = 200;
    s1.demod_preambles = 50;
    s2.demod_preambles = 75;
    s1.unique_aircraft = 10;
    s2.unique_aircraft = 5;
    add_stats(&s1, &s2, &target);
    ASSERT_EQ_U32("msg sum", target.messages_total, 300);
    ASSERT_EQ_U32("preambles sum", target.demod_preambles, 125);
    ASSERT_EQ_U32("aircraft sum", target.unique_aircraft, 15);

    // Start takes earlier non-zero
    s1.start = 1000; s2.start = 500;
    add_stats(&s1, &s2, &target);
    ASSERT_EQ_I64("start earlier", target.start, 500);

    // Start takes non-zero when one is zero
    s1.start = 0; s2.start = 500;
    add_stats(&s1, &s2, &target);
    ASSERT_EQ_I64("start nonzero", target.start, 500);

    s1.start = 1000; s2.start = 0;
    add_stats(&s1, &s2, &target);
    ASSERT_EQ_I64("start nonzero2", target.start, 1000);

    // End takes later
    s1.end = 2000; s2.end = 3000;
    add_stats(&s1, &s2, &target);
    ASSERT_EQ_I64("end later", target.end, 3000);

    // Peak signal power takes max
    s1.peak_signal_power = -5.0; s2.peak_signal_power = -3.0;
    add_stats(&s1, &s2, &target);
    ASSERT_FLOAT_NEAR("peak max", target.peak_signal_power, -3.0, 0.001);

    // Distance min takes min
    s1.distance_min = 100.0; s2.distance_min = 50.0;
    add_stats(&s1, &s2, &target);
    ASSERT_FLOAT_NEAR("dist min", target.distance_min, 50.0, 0.001);

    fprintf(stderr, "testAddStats: done\n\n");
}

// ---- testPercentile ----

static void testPercentile(void) {
    fprintf(stderr, "=== testPercentile ===\n");

    // Single element
    {
        float vals[] = {42.0f};
        ASSERT_FLOAT_NEAR("pct single", percentile(0.5f, vals, 1), 42.0, 0.001);
    }

    // Median of 5 sorted values
    {
        float vals[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        ASSERT_FLOAT_NEAR("pct median5", percentile(0.5f, vals, 5), 3.0, 0.001);
    }

    // 25th percentile of 5 sorted values
    {
        float vals[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        ASSERT_FLOAT_NEAR("pct 25th", percentile(0.25f, vals, 5), 2.0, 0.001);
    }

    // 75th percentile of 5 sorted values
    {
        float vals[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        ASSERT_FLOAT_NEAR("pct 75th", percentile(0.75f, vals, 5), 4.0, 0.001);
    }

    // Boundary values: p=0 -> first, p=1 -> last
    {
        float vals[] = {10.0f, 20.0f, 30.0f};
        ASSERT_FLOAT_NEAR("pct p0", percentile(0.0f, vals, 3), 10.0, 0.001);
        ASSERT_FLOAT_NEAR("pct p1", percentile(1.0f, vals, 3), 30.0, 0.001);
    }

    fprintf(stderr, "testPercentile: done\n\n");
}

// ---- testCompareFloat ----

static void testCompareFloat(void) {
    fprintf(stderr, "=== testCompareFloat ===\n");

    float a, b;

    // Less
    a = 1.0f; b = 2.0f;
    ASSERT_TRUE("cmp less", compareFloat(&a, &b) < 0);

    // Greater
    a = 3.0f; b = 2.0f;
    ASSERT_TRUE("cmp greater", compareFloat(&a, &b) > 0);

    // Equal
    a = 5.0f; b = 5.0f;
    ASSERT_EQ_INT("cmp equal", compareFloat(&a, &b), 0);

    // Works with qsort
    {
        float vals[] = {5.0f, 1.0f, 3.0f, 2.0f, 4.0f};
        qsort(vals, 5, sizeof(float), compareFloat);
        for (int i = 0; i < 4; i++) {
            ASSERT_TRUE("qsort ordered", vals[i] <= vals[i + 1]);
        }
        ASSERT_FLOAT_NEAR("qsort first", vals[0], 1.0, 0.001);
        ASSERT_FLOAT_NEAR("qsort last", vals[4], 5.0, 0.001);
    }

    fprintf(stderr, "testCompareFloat: done\n\n");
}

// ---- testStatsResetCount ----

static void testStatsResetCount(void) {
    fprintf(stderr, "=== testStatsResetCount ===\n");

    // Populate globalStatsCount with nonzero values
    struct statsCount *s = &Modes.globalStatsCount;
    s->readsb_aircraft_with_position = 42;
    s->readsb_aircraft_no_position = 10;
    s->readsb_aircraft_total = 52;
    s->readsb_aircraft_adsb_version_0 = 5;
    s->readsb_aircraft_adsb_version_1 = 15;
    s->readsb_aircraft_adsb_version_2 = 22;
    s->readsb_aircraft_emergency = 1;
    s->readsb_aircraft_rssi_average = -20.0;
    s->readsb_aircraft_rssi_max = -5.0;
    s->readsb_aircraft_rssi_min = -40.0;
    s->readsb_aircraft_with_flight_number = 30;
    s->readsb_aircraft_without_flight_number = 22;
    s->rssi_table_len = 100;

    statsResetCount();

    ASSERT_EQ_INT("with_position", s->readsb_aircraft_with_position, 0);
    ASSERT_EQ_INT("no_position", s->readsb_aircraft_no_position, 0);
    ASSERT_EQ_INT("total", s->readsb_aircraft_total, 0);
    ASSERT_EQ_INT("v0", s->readsb_aircraft_adsb_version_0, 0);
    ASSERT_EQ_INT("v1", s->readsb_aircraft_adsb_version_1, 0);
    ASSERT_EQ_INT("v2", s->readsb_aircraft_adsb_version_2, 0);
    ASSERT_EQ_INT("emergency", s->readsb_aircraft_emergency, 0);
    ASSERT_FLOAT_NEAR("rssi_max reset", s->readsb_aircraft_rssi_max, -50.0, 0.001);
    ASSERT_FLOAT_NEAR("rssi_min reset", s->readsb_aircraft_rssi_min, 42.0, 0.001);
    ASSERT_EQ_INT("with_flight", s->readsb_aircraft_with_flight_number, 0);
    ASSERT_EQ_INT("without_flight", s->readsb_aircraft_without_flight_number, 0);
    ASSERT_EQ_INT("rssi_table_len", s->rssi_table_len, 0);

    fprintf(stderr, "testStatsResetCount: done\n\n");
}

// ---- testAppendStatsJson ----

static void testAppendStatsJson(void) {
    fprintf(stderr, "=== testAppendStatsJson ===\n");

    struct stats st;
    memset(&st, 0, sizeof(st));
    st.start = 1700000000000LL;
    st.end = 1700000060000LL;
    st.messages_total = 12345;
    st.unique_aircraft = 42;

    char buf[8192];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = appendStatsJson(p, end, &st, "test_key");

    // Null-terminate for string search
    *p = '\0';

    ASSERT_TRUE("has key", strstr(buf, "\"test_key\"") != NULL);
    ASSERT_TRUE("has start", strstr(buf, "\"start\"") != NULL);
    ASSERT_TRUE("has end", strstr(buf, "\"end\"") != NULL);
    ASSERT_TRUE("has messages", strstr(buf, "\"messages\":12345") != NULL);

    fprintf(stderr, "testAppendStatsJson: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testAddTimespecs();
    testResetStats();
    testAddStats();
    testPercentile();
    testCompareFloat();
    testStatsResetCount();
    testAppendStatsJson();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
