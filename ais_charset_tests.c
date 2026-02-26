// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// ais_charset_tests.c - unit tests for the AIS character set table
//
// Links against ais_charset.o to test the exported ais_charset[64] array.

#include <stdio.h>
#include <string.h>

#include "ais_charset.h"

// ---- test helpers ----

static int failures = 0;

#define ASSERT_EQ_INT(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_CHAR(tag, got, expected) do { \
    char _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got '%c' (0x%02x), expected '%c' (0x%02x)\n", \
                tag, _g, (unsigned char)_g, _e, (unsigned char)_e); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

// ---- testCharsetLength ----

static void testCharsetLength(void) {
    fprintf(stderr, "=== testCharsetLength ===\n");

    // ais_charset is declared as char[64] with NO null terminator.
    // sizeof cannot be used here (extern array), so verify content directly:
    // First char '@' and last char '?' should be at indices 0 and 63.
    ASSERT_EQ_CHAR("charset first", ais_charset[0], '@');
    ASSERT_EQ_CHAR("charset last", ais_charset[63], '?');

    // Verify all 64 bytes are printable ASCII (0x20-0x7E)
    for (int i = 0; i < 64; i++) {
        char tag[32];
        snprintf(tag, sizeof(tag), "charset printable[%d]", i);
        ASSERT_TRUE(tag, ais_charset[i] >= 0x20 && ais_charset[i] <= 0x7E);
    }

    fprintf(stderr, "testCharsetLength: done\n\n");
}

// ---- testCharsetMapping ----

static void testCharsetMapping(void) {
    fprintf(stderr, "=== testCharsetMapping ===\n");

    // AIS 6-bit encoding: index -> character
    // Table: @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_ !"#$%&'()*+,-./0123456789:;<=>?
    ASSERT_EQ_CHAR("idx 0 = @", ais_charset[0], '@');
    ASSERT_EQ_CHAR("idx 1 = A", ais_charset[1], 'A');
    ASSERT_EQ_CHAR("idx 13 = M", ais_charset[13], 'M');
    ASSERT_EQ_CHAR("idx 26 = Z", ais_charset[26], 'Z');
    ASSERT_EQ_CHAR("idx 32 = space", ais_charset[32], ' ');
    ASSERT_EQ_CHAR("idx 33 = !", ais_charset[33], '!');
    ASSERT_EQ_CHAR("idx 48 = 0", ais_charset[48], '0');
    ASSERT_EQ_CHAR("idx 57 = 9", ais_charset[57], '9');
    ASSERT_EQ_CHAR("idx 63 = ?", ais_charset[63], '?');

    // Verify uppercase alphabet is contiguous at indices 1-26
    for (int i = 0; i < 26; i++) {
        char tag[32];
        snprintf(tag, sizeof(tag), "alpha idx %d", i + 1);
        ASSERT_EQ_CHAR(tag, ais_charset[i + 1], 'A' + i);
    }

    // Verify digits are contiguous at indices 48-57
    for (int i = 0; i < 10; i++) {
        char tag[32];
        snprintf(tag, sizeof(tag), "digit idx %d", i + 48);
        ASSERT_EQ_CHAR(tag, ais_charset[i + 48], '0' + i);
    }

    fprintf(stderr, "testCharsetMapping: done\n\n");
}

// ---- testCharsetUniqueness ----

static void testCharsetUniqueness(void) {
    fprintf(stderr, "=== testCharsetUniqueness ===\n");

    // All 64 characters must be distinct
    int seen[256] = {0};
    int duplicates = 0;

    for (int i = 0; i < 64; i++) {
        unsigned char ch = (unsigned char)ais_charset[i];
        if (seen[ch]) {
            fprintf(stderr, "  duplicate char '%c' (0x%02x) at index %d\n", ch, ch, i);
            duplicates++;
        }
        seen[ch] = 1;
    }

    ASSERT_EQ_INT("no duplicates", duplicates, 0);

    fprintf(stderr, "testCharsetUniqueness: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testCharsetLength();
    testCharsetMapping();
    testCharsetUniqueness();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
