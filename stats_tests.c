// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// stats_tests.c - unit tests for pure functions from stats.c
//
// Copies small pure functions to avoid the massive stub surface of linking stats.o.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#include "readsb.h"

// Linker stubs
struct _Modes Modes;
void setExit(int __attribute__((unused)) arg) { }
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

// ---- Copied pure functions from stats.c ----

static void test_add_timespecs(const struct timespec *x, const struct timespec *y, struct timespec *z) {
    z->tv_sec = x->tv_sec + y->tv_sec;
    z->tv_nsec = x->tv_nsec + y->tv_nsec;
    z->tv_sec += z->tv_nsec / 1000000000L;
    z->tv_nsec = z->tv_nsec % 1000000000L;
}

static void test_reset_stats(struct stats *st) {
    static struct stats st_zero;
    *st = st_zero;
    st->distance_min = 2E42;
}

static void test_add_stats_partial(const struct stats *st1, const struct stats *st2, struct stats *target) {
    // Test the key logic paths of add_stats: start, end, additive fields, peak_signal_power, distance_min
    if (st1->start == 0)
        target->start = st2->start;
    else if (st2->start == 0)
        target->start = st1->start;
    else if (st1->start < st2->start)
        target->start = st1->start;
    else
        target->start = st2->start;

    target->end = st1->end > st2->end ? st1->end : st2->end;

    target->messages_total = st1->messages_total + st2->messages_total;
    target->demod_preambles = st1->demod_preambles + st2->demod_preambles;
    target->unique_aircraft = st1->unique_aircraft + st2->unique_aircraft;

    if (st1->peak_signal_power > st2->peak_signal_power)
        target->peak_signal_power = st1->peak_signal_power;
    else
        target->peak_signal_power = st2->peak_signal_power;

    target->distance_max = st1->distance_max > st2->distance_max ? st1->distance_max : st2->distance_max;
    target->distance_min = st1->distance_min < st2->distance_min ? st1->distance_min : st2->distance_min;
}

static float test_percentile(float p, float *values, int len) {
    float x = p * (len - 1);
    float d = x - ((int) x);
    int index = (int) x;

    float res;
    if (index + 1 < len)
        res = values[index] + d * (values[index + 1] - values[index]);
    else
        res = values[index];
    return res;
}

static int test_compareFloat(const void *p1, const void *p2) {
    float a1 = *(const float *) p1;
    float a2 = *(const float *) p2;
    return (a1 > a2) - (a1 < a2);
}

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
    test_add_timespecs(&x, &y, &z);
    ASSERT_EQ_I64("simple sec", z.tv_sec, 3);
    ASSERT_EQ_I64("simple nsec", z.tv_nsec, 0);

    // Nanosecond overflow: 500M + 600M = 1s + 100M
    x = (struct timespec){.tv_sec = 0, .tv_nsec = 500000000};
    y = (struct timespec){.tv_sec = 0, .tv_nsec = 600000000};
    test_add_timespecs(&x, &y, &z);
    ASSERT_EQ_I64("overflow sec", z.tv_sec, 1);
    ASSERT_EQ_I64("overflow nsec", z.tv_nsec, 100000000);

    // Edge case: 999999999ns + 1ns = 1s + 0ns
    x = (struct timespec){.tv_sec = 5, .tv_nsec = 999999999};
    y = (struct timespec){.tv_sec = 0, .tv_nsec = 1};
    test_add_timespecs(&x, &y, &z);
    ASSERT_EQ_I64("edge sec", z.tv_sec, 6);
    ASSERT_EQ_I64("edge nsec", z.tv_nsec, 0);

    // Both zero
    x = (struct timespec){.tv_sec = 0, .tv_nsec = 0};
    y = (struct timespec){.tv_sec = 0, .tv_nsec = 0};
    test_add_timespecs(&x, &y, &z);
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

    test_reset_stats(&st);

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
    test_add_stats_partial(&s1, &s2, &target);
    ASSERT_EQ_U32("msg sum", target.messages_total, 300);
    ASSERT_EQ_U32("preambles sum", target.demod_preambles, 125);
    ASSERT_EQ_U32("aircraft sum", target.unique_aircraft, 15);

    // Start takes earlier non-zero
    s1.start = 1000; s2.start = 500;
    test_add_stats_partial(&s1, &s2, &target);
    ASSERT_EQ_I64("start earlier", target.start, 500);

    // Start takes non-zero when one is zero
    s1.start = 0; s2.start = 500;
    test_add_stats_partial(&s1, &s2, &target);
    ASSERT_EQ_I64("start nonzero", target.start, 500);

    s1.start = 1000; s2.start = 0;
    test_add_stats_partial(&s1, &s2, &target);
    ASSERT_EQ_I64("start nonzero2", target.start, 1000);

    // End takes later
    s1.end = 2000; s2.end = 3000;
    test_add_stats_partial(&s1, &s2, &target);
    ASSERT_EQ_I64("end later", target.end, 3000);

    // Peak signal power takes max
    s1.peak_signal_power = -5.0; s2.peak_signal_power = -3.0;
    test_add_stats_partial(&s1, &s2, &target);
    ASSERT_FLOAT_NEAR("peak max", target.peak_signal_power, -3.0, 0.001);

    // Distance min takes min
    s1.distance_min = 100.0; s2.distance_min = 50.0;
    test_add_stats_partial(&s1, &s2, &target);
    ASSERT_FLOAT_NEAR("dist min", target.distance_min, 50.0, 0.001);

    fprintf(stderr, "testAddStats: done\n\n");
}

// ---- testPercentile ----

static void testPercentile(void) {
    fprintf(stderr, "=== testPercentile ===\n");

    // Single element
    {
        float vals[] = {42.0f};
        ASSERT_FLOAT_NEAR("pct single", test_percentile(0.5f, vals, 1), 42.0, 0.001);
    }

    // Median of 5 sorted values
    {
        float vals[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        ASSERT_FLOAT_NEAR("pct median5", test_percentile(0.5f, vals, 5), 3.0, 0.001);
    }

    // 25th percentile of 5 sorted values
    {
        float vals[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        ASSERT_FLOAT_NEAR("pct 25th", test_percentile(0.25f, vals, 5), 2.0, 0.001);
    }

    // 75th percentile of 5 sorted values
    {
        float vals[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        ASSERT_FLOAT_NEAR("pct 75th", test_percentile(0.75f, vals, 5), 4.0, 0.001);
    }

    // Boundary values: p=0 -> first, p=1 -> last
    {
        float vals[] = {10.0f, 20.0f, 30.0f};
        ASSERT_FLOAT_NEAR("pct p0", test_percentile(0.0f, vals, 3), 10.0, 0.001);
        ASSERT_FLOAT_NEAR("pct p1", test_percentile(1.0f, vals, 3), 30.0, 0.001);
    }

    fprintf(stderr, "testPercentile: done\n\n");
}

// ---- testCompareFloat ----

static void testCompareFloat(void) {
    fprintf(stderr, "=== testCompareFloat ===\n");

    float a, b;

    // Less
    a = 1.0f; b = 2.0f;
    ASSERT_TRUE("cmp less", test_compareFloat(&a, &b) < 0);

    // Greater
    a = 3.0f; b = 2.0f;
    ASSERT_TRUE("cmp greater", test_compareFloat(&a, &b) > 0);

    // Equal
    a = 5.0f; b = 5.0f;
    ASSERT_EQ_INT("cmp equal", test_compareFloat(&a, &b), 0);

    // Works with qsort
    {
        float vals[] = {5.0f, 1.0f, 3.0f, 2.0f, 4.0f};
        qsort(vals, 5, sizeof(float), test_compareFloat);
        for (int i = 0; i < 4; i++) {
            ASSERT_TRUE("qsort ordered", vals[i] <= vals[i + 1]);
        }
        ASSERT_FLOAT_NEAR("qsort first", vals[0], 1.0, 0.001);
        ASSERT_FLOAT_NEAR("qsort last", vals[4], 5.0, 0.001);
    }

    fprintf(stderr, "testCompareFloat: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testAddTimespecs();
    testResetStats();
    testAddStats();
    testPercentile();
    testCompareFloat();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
