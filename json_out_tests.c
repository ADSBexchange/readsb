// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// json_out_tests.c - unit tests for pure functions from json_out.c
//
// Self-contained: copies trimSpace, jsonEscapeString, and
// append_nav_modes to avoid linking json_out.o.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>

#include "readsb.h"

// Linker stubs
struct _Modes Modes;
void setExit(int __attribute__((unused)) arg) { }
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

// ---- Copied pure functions from json_out.c ----

static const char *test_trimSpace(const char *in, char *out, int len) {
    out[len] = '\0';
    int found = 0;

    for (int i = len - 1; i >= 0; i--) {
        if (!found && in[i] == ' ') {
            out[i] = '\0';
        } else if (in[i] == '\0') {
            out[i] = '\0';
        } else {
            out[i] = in[i];
            found = 1;
        }
    }

    return out;
}

static const char *test_jsonEscapeString(const char *str, char *buf, int len) {
    const char *in = str;
    char *out = buf, *end = buf + len - 10;

    for (; *in && out < end; ++in) {
        unsigned char ch = *in;
        if (ch == '"' || ch == '\\') {
            *out++ = '\\';
            *out++ = ch;
        } else if (ch < 32 || ch > 126) {
            out = safe_snprintf(out, end, "\\u%04x", ch);
        } else {
            *out++ = ch;
        }
    }

    *out++ = 0;
    return buf;
}

typedef struct {
    nav_modes_t flag;
    const char *name;
} test_nav_mode_entry;

static test_nav_mode_entry test_nav_modes_names[] = {
    { NAV_MODE_AUTOPILOT, "autopilot"},
    { NAV_MODE_VNAV, "vnav"},
    { NAV_MODE_ALT_HOLD, "althold"},
    { NAV_MODE_APPROACH, "approach"},
    { NAV_MODE_LNAV, "lnav"},
    { NAV_MODE_TCAS, "tcas"},
    { 0, NULL}
};

static char *test_append_nav_modes(char *p, char *end, nav_modes_t flags, const char *quote, const char *sep) {
    int first = 1;
    for (int i = 0; test_nav_modes_names[i].name; ++i) {
        if (!(flags & test_nav_modes_names[i].flag)) {
            continue;
        }

        if (!first) {
            p = safe_snprintf(p, end, "%s", sep);
        }

        first = 0;
        p = safe_snprintf(p, end, "%s%s%s", quote, test_nav_modes_names[i].name, quote);
    }

    return p;
}

// ---- test helpers ----

static int failures = 0;

#define ASSERT_STR_EQ(tag, got, expected) do { \
    if (strcmp((got), (expected)) != 0) { \
        fprintf(stderr, "%s: FAIL: got \"%s\", expected \"%s\"\n", tag, (got), (expected)); \
        failures++; \
    } \
} while(0)

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

// ---- testTrimSpace ----

static void testTrimSpace(void) {
    fprintf(stderr, "=== testTrimSpace ===\n");

    char out[64];

    // Trailing spaces removed
    test_trimSpace("HELLO   ", out, 8);
    ASSERT_STR_EQ("trim trailing", out, "HELLO");

    // All spaces -> empty string
    test_trimSpace("        ", out, 8);
    ASSERT_STR_EQ("trim all spaces", out, "");

    // No trailing spaces -> unchanged
    test_trimSpace("HELLO", out, 5);
    ASSERT_STR_EQ("trim no trailing", out, "HELLO");

    // Single char with trailing spaces
    test_trimSpace("A   ", out, 4);
    ASSERT_STR_EQ("trim single char", out, "A");

    // Embedded null handling (null treated as non-space terminator)
    {
        char input[] = "AB\0  ";
        test_trimSpace(input, out, 5);
        ASSERT_STR_EQ("trim with null", out, "AB");
    }

    fprintf(stderr, "testTrimSpace: done\n\n");
}

// ---- testJsonEscapeString ----

static void testJsonEscapeString(void) {
    fprintf(stderr, "=== testJsonEscapeString ===\n");

    char buf[256];

    // Plain ASCII unchanged
    test_jsonEscapeString("hello world", buf, sizeof(buf));
    ASSERT_STR_EQ("esc plain", buf, "hello world");

    // Double quote escaped
    test_jsonEscapeString("say \"hi\"", buf, sizeof(buf));
    ASSERT_STR_EQ("esc dquote", buf, "say \\\"hi\\\"");

    // Backslash escaped
    test_jsonEscapeString("path\\to", buf, sizeof(buf));
    ASSERT_STR_EQ("esc backslash", buf, "path\\\\to");

    // Control char -> \u00XX
    {
        char input[] = "a\x01" "b";
        test_jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc ctrl", buf, "a\\u0001b");
    }

    // Tab (0x09)
    {
        char input[] = "a\tb";
        test_jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc tab", buf, "a\\u0009b");
    }

    // Newline (0x0A)
    {
        char input[] = "a\nb";
        test_jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc newline", buf, "a\\u000ab");
    }

    // High byte (0x80+) escaped
    {
        char input[] = "a\x80" "b";
        test_jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc highbyte", buf, "a\\u0080b");
    }

    // Empty string
    test_jsonEscapeString("", buf, sizeof(buf));
    ASSERT_STR_EQ("esc empty", buf, "");

    // Buffer overflow protection: small buffer
    test_jsonEscapeString("this is a long string that exceeds the buffer", buf, 20);
    // Should not crash; result should be truncated
    ASSERT_TRUE("esc overflow no crash", strlen(buf) < 20);

    fprintf(stderr, "testJsonEscapeString: done\n\n");
}

// ---- testAppendNavModes ----

static void testAppendNavModes(void) {
    fprintf(stderr, "=== testAppendNavModes ===\n");

    char buf[256];
    char *p;

    // No flags -> empty
    buf[0] = '\0';
    p = test_append_nav_modes(buf, buf + sizeof(buf), 0, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav none", buf, "");

    // Single flag
    buf[0] = '\0';
    p = test_append_nav_modes(buf, buf + sizeof(buf), NAV_MODE_AUTOPILOT, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav single", buf, "\"autopilot\"");

    // Two flags comma-separated
    buf[0] = '\0';
    p = test_append_nav_modes(buf, buf + sizeof(buf), NAV_MODE_AUTOPILOT | NAV_MODE_VNAV, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav two", buf, "\"autopilot\",\"vnav\"");

    // All 6 flags
    buf[0] = '\0';
    nav_modes_t all = NAV_MODE_AUTOPILOT | NAV_MODE_VNAV | NAV_MODE_ALT_HOLD |
                      NAV_MODE_APPROACH | NAV_MODE_LNAV | NAV_MODE_TCAS;
    p = test_append_nav_modes(buf, buf + sizeof(buf), all, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav all", buf, "\"autopilot\",\"vnav\",\"althold\",\"approach\",\"lnav\",\"tcas\"");

    // Without quotes, space separator
    buf[0] = '\0';
    p = test_append_nav_modes(buf, buf + sizeof(buf), NAV_MODE_LNAV | NAV_MODE_TCAS, "", " ");
    *p = '\0';
    ASSERT_STR_EQ("nav space sep", buf, "lnav tcas");

    fprintf(stderr, "testAppendNavModes: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testTrimSpace();
    testJsonEscapeString();
    testAppendNavModes();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
