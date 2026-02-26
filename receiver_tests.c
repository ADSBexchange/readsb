// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// receiver_tests.c - unit tests for receiver hash table and bad-state logic
//
// Uses #include "receiver.c" to access static functions directly.
// Provides linker stubs for external symbols that receiver.c references
// but that we don't exercise in the tests.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
struct _Threads Threads;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];
void setExit(int __attribute__((unused)) arg) { }

static int64_t test_now = 1700000000000LL;
int64_t mstime(void) { return test_now; }

// greatcircle stub — receiver.c:179 uses it for distance
double greatcircle(double lat0, double lon0, double lat1, double lon1,
                   int __attribute__((unused)) approx) {
    double x = (lon1 - lon0) * cos((lat0 + lat1) * M_PI / 360.0) * 111319.9;
    double y = (lat1 - lat0) * 111319.9;
    return sqrt(x * x + y * y);
}

// sprint_uuid1 stub — receiver.c uses it for debug/JSON output
char *sprint_uuid1(uint64_t __attribute__((unused)) id1, char *p) {
    strcpy(p, "test-uuid");
    return p + 9;
}

// ---- Include source under test ----

#include "receiver.c"

// ---- Test helpers ----

static int failures = 0;

#define ASSERT_EQ_INT(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
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

#define ASSERT_EQ_U64(tag, got, expected) do { \
    uint64_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %"PRIu64", expected %"PRIu64"\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

#define ASSERT_NULL(tag, ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "%s: FAIL: expected NULL\n", tag); \
        failures++; \
    } \
} while(0)

#define ASSERT_NOT_NULL(tag, ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "%s: FAIL: expected non-NULL\n", tag); \
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

// Helper to reset Modes and receiver table between tests
static void resetReceiverState(void) {
    if (Modes.receiverTable) {
        receiverCleanup();
    }
    memset(&Modes, 0, sizeof(Modes));
    test_now = 1700000000000LL;
}

// ---- testReceiverHash ----

static void testReceiverHash(void) {
    fprintf(stderr, "=== testReceiverHash ===\n");

    // Need to set up hash bits/size for receiverHash to work
    Modes.receiver_table_hash_bits = 8;
    Modes.receiver_table_size = 256;

    uint64_t id1 = 0x1234567890ABCDEFULL;
    uint64_t id2 = 0xFEDCBA0987654321ULL;

    uint32_t h1 = receiverHash(id1);
    uint32_t h2 = receiverHash(id2);

    // Result in range [0, table_size - 1]
    ASSERT_TRUE("h1 in range", h1 < (uint32_t)Modes.receiver_table_size);
    ASSERT_TRUE("h2 in range", h2 < (uint32_t)Modes.receiver_table_size);

    // Deterministic
    ASSERT_EQ_U32("h1 deterministic", receiverHash(id1), h1);

    // Different IDs should (almost certainly) differ
    ASSERT_TRUE("different IDs differ", h1 != h2);

    resetReceiverState();
    fprintf(stderr, "testReceiverHash: done\n\n");
}

// ---- testReceiverInitCleanup ----

static void testReceiverInitCleanup(void) {
    fprintf(stderr, "=== testReceiverInitCleanup ===\n");

    resetReceiverState();

    // receiverInit sets up the table
    receiverInit();
    ASSERT_NOT_NULL("table allocated", Modes.receiverTable);
    ASSERT_EQ_INT("hash_bits", Modes.receiver_table_hash_bits, 8);
    ASSERT_EQ_INT("table_size", Modes.receiver_table_size, 256);

    // Cleanup frees and NULLs
    receiverCleanup();
    ASSERT_NULL("table freed", Modes.receiverTable);

    resetReceiverState();
    fprintf(stderr, "testReceiverInitCleanup: done\n\n");
}

// ---- testReceiverCreateGet ----

static void testReceiverCreateGet(void) {
    fprintf(stderr, "=== testReceiverCreateGet ===\n");

    resetReceiverState();
    receiverInit();

    uint64_t id = 0xAABBCCDD00112233ULL;

    // Get before create -> NULL
    ASSERT_NULL("get before create", receiverGet(id));

    // Create returns non-NULL with correct id
    struct receiver *r = receiverCreate(id);
    ASSERT_NOT_NULL("create non-null", r);
    ASSERT_EQ_U64("correct id", r->id, id);

    // Get returns same pointer
    struct receiver *r2 = receiverGet(id);
    ASSERT_TRUE("get same pointer", r == r2);

    // Create is idempotent
    struct receiver *r3 = receiverCreate(id);
    ASSERT_TRUE("create idempotent", r == r3);

    // Count incremented once
    ASSERT_EQ_U64("count=1", Modes.receiverCount, 1);

    // Create a second distinct receiver
    uint64_t id2 = 0x1122334455667788ULL;
    struct receiver *r4 = receiverCreate(id2);
    ASSERT_NOT_NULL("second create", r4);
    ASSERT_EQ_U64("count=2", Modes.receiverCount, 2);

    receiverCleanup();
    resetReceiverState();
    fprintf(stderr, "testReceiverCreateGet: done\n\n");
}

// ---- testReceiverTimeout ----

static void testReceiverTimeout(void) {
    fprintf(stderr, "=== testReceiverTimeout ===\n");

    resetReceiverState();
    receiverInit();

    // Create an old receiver (>24h ago)
    uint64_t old_id = 0x1111111111111111ULL;
    struct receiver *r_old = receiverCreate(old_id);
    r_old->lastSeen = test_now - 25 * HOURS;

    // Create a recent receiver (<1h ago)
    uint64_t new_id = 0x2222222222222222ULL;
    struct receiver *r_new = receiverCreate(new_id);
    r_new->lastSeen = test_now - 30 * MINUTES;

    ASSERT_EQ_U64("count before timeout", Modes.receiverCount, 2);

    // Run timeout for the entire table
    receiverTimeout(0, 1, test_now);

    // Old receiver should be removed, recent retained
    ASSERT_NULL("old removed", receiverGet(old_id));
    ASSERT_NOT_NULL("recent retained", receiverGet(new_id));
    ASSERT_EQ_U64("count after timeout", Modes.receiverCount, 1);

    receiverCleanup();
    resetReceiverState();
    fprintf(stderr, "testReceiverTimeout: done\n\n");
}

// ---- testReceiverCheckBad ----

static void testReceiverCheckBad(void) {
    fprintf(stderr, "=== testReceiverCheckBad ===\n");

    resetReceiverState();
    receiverInit();

    uint64_t id = 0x3333333333333333ULL;

    // No receiver -> 0
    ASSERT_EQ_INT("no receiver", receiverCheckBad(id, test_now), 0);

    // Create receiver with timedOutUntil in the future
    struct receiver *r = receiverCreate(id);
    r->timedOutUntil = test_now + 10 * SECONDS;
    ASSERT_EQ_INT("future timeout", receiverCheckBad(id, test_now), 1);

    // Expired timeout -> 0
    r->timedOutUntil = test_now - 1;
    ASSERT_EQ_INT("expired timeout", receiverCheckBad(id, test_now), 0);

    receiverCleanup();
    resetReceiverState();
    fprintf(stderr, "testReceiverCheckBad: done\n\n");
}

// ---- testReceiverMaintenance ----

static void testReceiverMaintenance(void) {
    fprintf(stderr, "=== testReceiverMaintenance ===\n");

    resetReceiverState();
    receiverInit();

    // Wide extent (>10 deg) should shrink by decay
    uint64_t wide_id = 0x4444444444444444ULL;
    struct receiver *r_wide = receiverCreate(wide_id);
    r_wide->lastSeen = test_now;
    r_wide->latMin = 30.0;
    r_wide->latMax = 50.0;  // 20 deg span
    r_wide->lonMin = -20.0;
    r_wide->lonMax = 10.0;  // 30 deg span

    double orig_latMin = r_wide->latMin;
    double orig_latMax = r_wide->latMax;
    double orig_lonMin = r_wide->lonMin;
    double orig_lonMax = r_wide->lonMax;

    receiverMaintenance(r_wide);

    // After decay, latMax should decrease and latMin should increase
    ASSERT_TRUE("lat decay max", r_wide->latMax < orig_latMax);
    ASSERT_TRUE("lat decay min", r_wide->latMin > orig_latMin);
    ASSERT_TRUE("lon decay max", r_wide->lonMax < orig_lonMax);
    ASSERT_TRUE("lon decay min", r_wide->lonMin > orig_lonMin);

    // Narrow extent (<10 deg) should stay unchanged
    uint64_t narrow_id = 0x5555555555555555ULL;
    struct receiver *r_narrow = receiverCreate(narrow_id);
    r_narrow->lastSeen = test_now;
    r_narrow->latMin = 50.0;
    r_narrow->latMax = 52.0;  // 2 deg span
    r_narrow->lonMin = -1.0;
    r_narrow->lonMax = 1.0;   // 2 deg span

    double n_latMin = r_narrow->latMin;
    double n_latMax = r_narrow->latMax;
    double n_lonMin = r_narrow->lonMin;
    double n_lonMax = r_narrow->lonMax;

    receiverMaintenance(r_narrow);

    ASSERT_FLOAT_NEAR("narrow lat min", r_narrow->latMin, n_latMin, 0.0001);
    ASSERT_FLOAT_NEAR("narrow lat max", r_narrow->latMax, n_latMax, 0.0001);
    ASSERT_FLOAT_NEAR("narrow lon min", r_narrow->lonMin, n_lonMin, 0.0001);
    ASSERT_FLOAT_NEAR("narrow lon max", r_narrow->lonMax, n_lonMax, 0.0001);

    receiverCleanup();
    resetReceiverState();
    fprintf(stderr, "testReceiverMaintenance: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testReceiverHash();
    testReceiverInitCleanup();
    testReceiverCreateGet();
    testReceiverTimeout();
    testReceiverCheckBad();
    testReceiverMaintenance();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
