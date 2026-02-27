// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// icao_filter_tests.c - unit tests for ICAO address hash table
//
// All icao_filter functions are public, so we link icao_filter.o directly.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "readsb.h"

// Linker stubs
struct _Modes Modes;
void setExit(int __attribute__((unused)) arg) { }

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

// ---- testBasicAddAndTest ----

static void testBasicAddAndTest(void) {
    fprintf(stderr, "=== testBasicAddAndTest ===\n");

    icaoFilterInit();

    icaoFilterAdd(0x4840D6);
    ASSERT_EQ_INT("added addr present", icaoFilterTest(0x4840D6), 1);
    ASSERT_EQ_INT("non-added addr absent", icaoFilterTest(0x000001), 0);

    icaoFilterDestroy();
    fprintf(stderr, "testBasicAddAndTest: done\n\n");
}

// ---- testMultipleAddresses ----

static void testMultipleAddresses(void) {
    fprintf(stderr, "=== testMultipleAddresses ===\n");

    icaoFilterInit();

    // Add 100 distinct addresses
    for (uint32_t i = 0; i < 100; i++) {
        icaoFilterAdd(0x100000 + i);
    }

    // All should be present
    int all_present = 1;
    for (uint32_t i = 0; i < 100; i++) {
        if (!icaoFilterTest(0x100000 + i)) {
            fprintf(stderr, "  missing addr 0x%06X\n", 0x100000 + i);
            all_present = 0;
            failures++;
            break;
        }
    }
    if (all_present) {
        fprintf(stderr, "  100 addresses present: PASS\n");
    }

    // Non-added addresses should be absent
    ASSERT_EQ_INT("non-added 0xFFFFFF", icaoFilterTest(0xFFFFFF), 0);
    ASSERT_EQ_INT("non-added 0x200000", icaoFilterTest(0x200000), 0);

    icaoFilterDestroy();
    fprintf(stderr, "testMultipleAddresses: done\n\n");
}

// ---- testDuplicateAdd ----

static void testDuplicateAdd(void) {
    fprintf(stderr, "=== testDuplicateAdd ===\n");

    icaoFilterInit();

    icaoFilterAdd(0xABCDEF);
    icaoFilterAdd(0xABCDEF); // duplicate
    ASSERT_EQ_INT("duplicate add", icaoFilterTest(0xABCDEF), 1);

    icaoFilterDestroy();
    fprintf(stderr, "testDuplicateAdd: done\n\n");
}

// ---- testExpireCycle ----

static void testExpireCycle(void) {
    fprintf(stderr, "=== testExpireCycle ===\n");

    icaoFilterInit();

    icaoFilterAdd(0xAABBCC);
    ASSERT_EQ_INT("before expire", icaoFilterTest(0xAABBCC), 1);

    // First expire: switches active table, clears old one
    // Address is now in the inactive table
    icaoFilterExpire();
    ASSERT_EQ_INT("after 1st expire", icaoFilterTest(0xAABBCC), 1);

    // Second expire: the inactive table (which had our address) gets cleared
    icaoFilterExpire();
    ASSERT_EQ_INT("after 2nd expire", icaoFilterTest(0xAABBCC), 0);

    icaoFilterDestroy();
    fprintf(stderr, "testExpireCycle: done\n\n");
}

// ---- testExpireAndReAdd ----

static void testExpireAndReAdd(void) {
    fprintf(stderr, "=== testExpireAndReAdd ===\n");

    icaoFilterInit();

    icaoFilterAdd(0x112233);
    icaoFilterExpire(); // now in inactive table

    // Re-add to new active table
    icaoFilterAdd(0x112233);
    icaoFilterExpire(); // now in inactive table again

    ASSERT_EQ_INT("expire+readd", icaoFilterTest(0x112233), 1);

    icaoFilterDestroy();
    fprintf(stderr, "testExpireAndReAdd: done\n\n");
}

// ---- testResizeTrigger ----

static void testResizeTrigger(void) {
    fprintf(stderr, "=== testResizeTrigger ===\n");

    icaoFilterInit();
    // Initial size: 256 buckets (MINBITS=8)
    // Resize triggers at > 1/3 capacity = > 85 addresses

    for (uint32_t i = 0; i < 100; i++) {
        icaoFilterAdd(0x300000 + i);
    }

    // All addresses should survive the resize
    int all_present = 1;
    for (uint32_t i = 0; i < 100; i++) {
        if (!icaoFilterTest(0x300000 + i)) {
            fprintf(stderr, "  missing after resize: 0x%06X\n", 0x300000 + i);
            all_present = 0;
            failures++;
            break;
        }
    }
    if (all_present) {
        fprintf(stderr, "  100 addresses survived resize: PASS\n");
    }

    icaoFilterDestroy();
    fprintf(stderr, "testResizeTrigger: done\n\n");
}

// ---- testDestroyAndReinit ----

static void testDestroyAndReinit(void) {
    fprintf(stderr, "=== testDestroyAndReinit ===\n");

    icaoFilterInit();
    icaoFilterAdd(0xDEAD01);
    ASSERT_EQ_INT("before destroy", icaoFilterTest(0xDEAD01), 1);

    icaoFilterDestroy();
    icaoFilterInit();
    ASSERT_EQ_INT("after reinit", icaoFilterTest(0xDEAD01), 0);

    icaoFilterDestroy();
    fprintf(stderr, "testDestroyAndReinit: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testBasicAddAndTest();
    testMultipleAddresses();
    testDuplicateAdd();
    testExpireCycle();
    testExpireAndReAdd();
    testResizeTrigger();
    testDestroyAndReinit();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
