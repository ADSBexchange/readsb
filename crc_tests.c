// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// crc_tests.c - unit tests for CRC computation and error correction
//
// Uses #include "crc.c" to access static functions directly.
// Provides linker stubs for external symbols that crc.c references
// but that we don't exercise in the tests.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];
void setExit(int __attribute__((unused)) arg) { }

// ---- Include source under test ----

#include "crc.c"

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

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

// ---- testCombinations ----

static void testCombinations(void) {
    fprintf(stderr, "=== testCombinations ===\n");

    ASSERT_EQ_INT("C(0,0)", combinations(0, 0), 1);
    ASSERT_EQ_INT("C(5,0)", combinations(5, 0), 1);
    ASSERT_EQ_INT("C(5,5)", combinations(5, 5), 1);
    ASSERT_EQ_INT("C(5,1)", combinations(5, 1), 5);
    ASSERT_EQ_INT("C(5,2)", combinations(5, 2), 10);
    ASSERT_EQ_INT("C(10,3)", combinations(10, 3), 120);
    ASSERT_EQ_INT("C(3,5)", combinations(3, 5), 0);

    fprintf(stderr, "testCombinations: done\n\n");
}

// ---- testSyndromeCompare ----

static void testSyndromeCompare(void) {
    fprintf(stderr, "=== testSyndromeCompare ===\n");

    struct errorinfo a = {.syndrome = 100};
    struct errorinfo b = {.syndrome = 100};
    struct errorinfo c = {.syndrome = 200};

    // Same syndrome -> 0
    ASSERT_EQ_INT("same", syndrome_compare(&a, &b), 0);

    // Lower < higher -> negative
    ASSERT_TRUE("lower<higher", syndrome_compare(&a, &c) < 0);

    // Higher > lower -> positive
    ASSERT_TRUE("higher>lower", syndrome_compare(&c, &a) > 0);

    // Works with qsort
    struct errorinfo arr[] = {{.syndrome = 300}, {.syndrome = 100}, {.syndrome = 200}};
    qsort(arr, 3, sizeof(struct errorinfo), syndrome_compare);
    ASSERT_EQ_U32("sorted[0]", arr[0].syndrome, 100);
    ASSERT_EQ_U32("sorted[1]", arr[1].syndrome, 200);
    ASSERT_EQ_U32("sorted[2]", arr[2].syndrome, 300);

    fprintf(stderr, "testSyndromeCompare: done\n\n");
}

// ---- testModesChecksum ----

static void testModesChecksum(void) {
    fprintf(stderr, "=== testModesChecksum ===\n");

    // Initialize lookup tables
    initLookupTables();

    // A valid DF17 message with correct CRC should yield 0
    // DF17 message: 8D4840D6202CC371C32CE0576098 (well-known test vector)
    uint8_t valid_msg[] = {0x8D, 0x48, 0x40, 0xD6, 0x20, 0x2C, 0xC3,
                           0x71, 0xC3, 0x2C, 0xE0, 0x57, 0x60, 0x98};
    uint32_t crc = modesChecksum(valid_msg, 112);
    ASSERT_EQ_U32("valid DF17 CRC", crc, 0);

    // Flip one bit -> nonzero syndrome
    uint8_t corrupted[14];
    memcpy(corrupted, valid_msg, 14);
    corrupted[5] ^= 0x01;  // flip bit in byte 5
    uint32_t bad_crc = modesChecksum(corrupted, 112);
    ASSERT_TRUE("corrupted nonzero", bad_crc != 0);

    // All-zero message should produce a known CRC
    uint8_t zeros[14] = {0};
    uint32_t zero_crc = modesChecksum(zeros, 112);
    // For all-zero 14-byte message, CRC is 0 (all-zero XOR with last 3 bytes = 0)
    ASSERT_EQ_U32("all-zero CRC", zero_crc, 0);

    fprintf(stderr, "testModesChecksum: done\n\n");
}

// ---- testChecksumInitAndDiagnose ----

static void testChecksumInitAndDiagnose(void) {
    fprintf(stderr, "=== testChecksumInitAndDiagnose ===\n");

    // Initialize with fixBits=1
    modesChecksumInit(1);

    // Create a valid DF17 message
    uint8_t valid_msg[] = {0x8D, 0x48, 0x40, 0xD6, 0x20, 0x2C, 0xC3,
                           0x71, 0xC3, 0x2C, 0xE0, 0x57, 0x60, 0x98};

    // Flip one bit
    uint8_t corrupted[14];
    memcpy(corrupted, valid_msg, 14);
    corrupted[6] ^= 0x08;  // flip bit 3 of byte 6

    uint32_t syndrome = modesChecksum(corrupted, 112);
    ASSERT_TRUE("syndrome nonzero", syndrome != 0);

    // Diagnose should find the error
    struct errorinfo *ei = modesChecksumDiagnose(syndrome, 112);
    ASSERT_TRUE("diagnose found", ei != NULL);
    ASSERT_TRUE("diagnose 1 error", ei->errors == 1);

    // Fix should restore the message
    modesChecksumFix(corrupted, ei);
    uint32_t fixed_crc = modesChecksum(corrupted, 112);
    ASSERT_EQ_U32("fixed CRC zero", fixed_crc, 0);
    ASSERT_TRUE("fixed matches original", memcmp(corrupted, valid_msg, 14) == 0);

    // Syndrome 0 returns NO_ERRORS
    struct errorinfo *no_err = modesChecksumDiagnose(0, 112);
    ASSERT_TRUE("syndrome 0 non-null", no_err != NULL);
    ASSERT_EQ_INT("syndrome 0 errors", no_err->errors, 0);

    crcCleanupTables();

    fprintf(stderr, "testChecksumInitAndDiagnose: done\n\n");
}

// ---- testChecksumFixBitsZero ----

static void testChecksumFixBitsZero(void) {
    fprintf(stderr, "=== testChecksumFixBitsZero ===\n");

    // Initialize with fixBits=0 -> no error tables
    modesChecksumInit(0);

    // Any nonzero syndrome should return NULL
    struct errorinfo *ei = modesChecksumDiagnose(0x123456, 112);
    ASSERT_TRUE("fixBits=0 returns NULL", ei == NULL);

    ei = modesChecksumDiagnose(0x000001, 56);
    ASSERT_TRUE("fixBits=0 short NULL", ei == NULL);

    fprintf(stderr, "testChecksumFixBitsZero: done\n\n");
}

// ---- testPrepareErrorTableSizes ----

static void testPrepareErrorTableSizes(void) {
    fprintf(stderr, "=== testPrepareErrorTableSizes ===\n");

    initLookupTables();

    // 56-bit table with 1-bit correction: C(51,1) = 51 entries
    // (56 - 5 = 51 data bits, 1-bit errors)
    int size_short = 0;
    struct errorinfo *table_short = prepareErrorTable(56, 1, 1, &size_short);
    ASSERT_EQ_INT("56-bit size", size_short, combinations(51, 1));
    ASSERT_TRUE("56-bit non-null", table_short != NULL);
    free(table_short);

    // 112-bit table with 1-bit correction: C(107,1) = 107 entries
    int size_long = 0;
    struct errorinfo *table_long = prepareErrorTable(112, 1, 1, &size_long);
    ASSERT_EQ_INT("112-bit size", size_long, combinations(107, 1));
    ASSERT_TRUE("112-bit non-null", table_long != NULL);
    free(table_long);

    // fixBits=0 -> NULL table, size=0
    int size_zero = 99;
    struct errorinfo *table_zero = prepareErrorTable(112, 0, 0, &size_zero);
    ASSERT_EQ_INT("zero size", size_zero, 0);
    ASSERT_TRUE("zero null", table_zero == NULL);

    fprintf(stderr, "testPrepareErrorTableSizes: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testCombinations();
    testSyndromeCompare();
    testModesChecksum();
    testChecksumInitAndDiagnose();
    testChecksumFixBitsZero();
    testPrepareErrorTableSizes();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
