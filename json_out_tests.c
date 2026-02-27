// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// json_out_tests.c - unit tests for static functions in json_out.c
//
// Uses #include "json_out.c" to access static functions directly.
// Provides linker stubs for external symbols that json_out.c references
// but that we don't exercise in the tests.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];
void setExit(int __attribute__((unused)) arg) { }
int64_t mstime(void) { return 1700000000000LL; }
ssize_t check_write(int __attribute__((unused)) fd,
                    const void __attribute__((unused)) *buf,
                    size_t __attribute__((unused)) len,
                    const char __attribute__((unused)) *desc) { return 0; }
int checkAcasRaValid(unsigned char __attribute__((unused)) *bytes,
                     struct modesMessage __attribute__((unused)) *mm,
                     int __attribute__((unused)) debug) { return 0; }
dbEntry *dbGet(uint32_t __attribute__((unused)) addr,
               dbEntry __attribute__((unused)) **dbIndex) { return NULL; }
void from_state_all(struct state_all __attribute__((unused)) *sa,
                    struct state __attribute__((unused)) *s,
                    struct aircraft __attribute__((unused)) *a,
                    int64_t __attribute__((unused)) now) { }
void ca_lock_read(struct craftArray __attribute__((unused)) *ca) { }
void ca_unlock_read(struct craftArray __attribute__((unused)) *ca) { }
int nogps(int64_t __attribute__((unused)) now,
          struct aircraft __attribute__((unused)) *a) { return 0; }
char *sprint_uuid(uint64_t __attribute__((unused)) id1,
                  uint64_t __attribute__((unused)) id2,
                  char *p) { return p; }
void toBinCraft(struct aircraft __attribute__((unused)) *a,
                struct binCraft __attribute__((unused)) *new_bc,
                int64_t __attribute__((unused)) now) { }
static char _stub_buffer[4096];
void *check_grow_threadpool_buffer_t(threadpool_buffer_t __attribute__((unused)) *buf,
                                     ssize_t __attribute__((unused)) needed) { return _stub_buffer; }

// ---- Include source under test ----

#include "json_out.c"

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

#define ASSERT_EQ_DBL(tag, got, expected, tol) do { \
    double _g = (got), _e = (expected); \
    if (fabs(_g - _e) > (tol)) { \
        fprintf(stderr, "%s: FAIL: got %.6f, expected %.6f\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_UINT(tag, got, expected) do { \
    unsigned _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %u, expected %u\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

// ---- testTrimSpace ----

static void testTrimSpace(void) {
    fprintf(stderr, "=== testTrimSpace ===\n");

    char out[64];

    // Trailing spaces removed
    trimSpace("HELLO   ", out, 8);
    ASSERT_STR_EQ("trim trailing", out, "HELLO");

    // All spaces -> empty string
    trimSpace("        ", out, 8);
    ASSERT_STR_EQ("trim all spaces", out, "");

    // No trailing spaces -> unchanged
    trimSpace("HELLO", out, 5);
    ASSERT_STR_EQ("trim no trailing", out, "HELLO");

    // Single char with trailing spaces
    trimSpace("A   ", out, 4);
    ASSERT_STR_EQ("trim single char", out, "A");

    // Embedded null handling (null treated as non-space terminator)
    {
        char input[] = "AB\0  ";
        trimSpace(input, out, 5);
        ASSERT_STR_EQ("trim with null", out, "AB");
    }

    fprintf(stderr, "testTrimSpace: done\n\n");
}

// ---- testJsonEscapeString ----

static void testJsonEscapeString(void) {
    fprintf(stderr, "=== testJsonEscapeString ===\n");

    char buf[256];

    // Plain ASCII unchanged
    jsonEscapeString("hello world", buf, sizeof(buf));
    ASSERT_STR_EQ("esc plain", buf, "hello world");

    // Double quote escaped
    jsonEscapeString("say \"hi\"", buf, sizeof(buf));
    ASSERT_STR_EQ("esc dquote", buf, "say \\\"hi\\\"");

    // Backslash escaped
    jsonEscapeString("path\\to", buf, sizeof(buf));
    ASSERT_STR_EQ("esc backslash", buf, "path\\\\to");

    // Control char -> \u00XX
    {
        char input[] = "a\x01" "b";
        jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc ctrl", buf, "a\\u0001b");
    }

    // Tab (0x09)
    {
        char input[] = "a\tb";
        jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc tab", buf, "a\\u0009b");
    }

    // Newline (0x0A)
    {
        char input[] = "a\nb";
        jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc newline", buf, "a\\u000ab");
    }

    // High byte (0x80+) escaped
    {
        char input[] = "a\x80" "b";
        jsonEscapeString(input, buf, sizeof(buf));
        ASSERT_STR_EQ("esc highbyte", buf, "a\\u0080b");
    }

    // Empty string
    jsonEscapeString("", buf, sizeof(buf));
    ASSERT_STR_EQ("esc empty", buf, "");

    // Buffer overflow protection: small buffer
    jsonEscapeString("this is a long string that exceeds the buffer", buf, 20);
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
    p = append_nav_modes(buf, buf + sizeof(buf), 0, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav none", buf, "");

    // Single flag
    buf[0] = '\0';
    p = append_nav_modes(buf, buf + sizeof(buf), NAV_MODE_AUTOPILOT, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav single", buf, "\"autopilot\"");

    // Two flags comma-separated
    buf[0] = '\0';
    p = append_nav_modes(buf, buf + sizeof(buf), NAV_MODE_AUTOPILOT | NAV_MODE_VNAV, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav two", buf, "\"autopilot\",\"vnav\"");

    // All 6 flags
    buf[0] = '\0';
    nav_modes_t all = NAV_MODE_AUTOPILOT | NAV_MODE_VNAV | NAV_MODE_ALT_HOLD |
                      NAV_MODE_APPROACH | NAV_MODE_LNAV | NAV_MODE_TCAS;
    p = append_nav_modes(buf, buf + sizeof(buf), all, "\"", ",");
    *p = '\0';
    ASSERT_STR_EQ("nav all", buf, "\"autopilot\",\"vnav\",\"althold\",\"approach\",\"lnav\",\"tcas\"");

    // Without quotes, space separator
    buf[0] = '\0';
    p = append_nav_modes(buf, buf + sizeof(buf), NAV_MODE_LNAV | NAV_MODE_TCAS, "", " ");
    *p = '\0';
    ASSERT_STR_EQ("nav space sep", buf, "lnav tcas");

    fprintf(stderr, "testAppendNavModes: done\n\n");
}

// ---- testGetSignal ----

static void testGetSignal(void) {
    fprintf(stderr, "=== testGetSignal ===\n");

    // signalNext < 4: sum = 0, result = 10*log10(0/8 + 1.125e-5)
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.signalNext = 0;
        double result = getSignal(&a);
        double expected = 10.0 * log10(0.0 / 8.0 + 1.125e-5);
        ASSERT_EQ_DBL("signal none", result, expected, 0.01);
    }

    // signalNext = 4: sums first 4 via loop
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.signalNext = 4;
        for (int i = 0; i < 4; i++)
            a.signalLevel[i] = 0.01;
        double result = getSignal(&a);
        double expected = 10.0 * log10(0.04 / 8.0 + 1.125e-5);
        ASSERT_EQ_DBL("signal 4", result, expected, 0.01);
    }

    // signalNext = 8: unrolled sum of all 8
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.signalNext = 8;
        for (int i = 0; i < 8; i++)
            a.signalLevel[i] = 0.5;
        double result = getSignal(&a);
        double expected = 10.0 * log10(4.0 / 8.0 + 1.125e-5);
        ASSERT_EQ_DBL("signal 8", result, expected, 0.01);
    }

    fprintf(stderr, "testGetSignal: done\n\n");
}

// ---- testAppendFlags ----

static void testAppendFlags(void) {
    fprintf(stderr, "=== testAppendFlags ===\n");

    char buf[512];
    char *p;

    // No match: all sources default to SOURCE_INVALID (0), query SOURCE_ADSB
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        p = append_flags(buf, buf + sizeof(buf), &a, SOURCE_ADSB);
        *p = '\0';
        ASSERT_STR_EQ("flags none", buf, "[]");
    }

    // Single match: callsign
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.callsign_valid.source = SOURCE_ADSB;
        p = append_flags(buf, buf + sizeof(buf), &a, SOURCE_ADSB);
        *p = '\0';
        ASSERT_STR_EQ("flags single", buf, "[\"callsign\"]");
    }

    // Multiple match: callsign + gs + track
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.callsign_valid.source = SOURCE_ADSB;
        a.gs_valid.source = SOURCE_ADSB;
        a.track_valid.source = SOURCE_ADSB;
        p = append_flags(buf, buf + sizeof(buf), &a, SOURCE_ADSB);
        *p = '\0';
        ASSERT_STR_EQ("flags multi", buf, "[\"callsign\",\"gs\",\"track\"]");
    }

    // Wrong source: callsign is MODE_S, query ADSB -> no match
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.callsign_valid.source = SOURCE_MODE_S;
        p = append_flags(buf, buf + sizeof(buf), &a, SOURCE_ADSB);
        *p = '\0';
        ASSERT_STR_EQ("flags wrong src", buf, "[]");
    }

    fprintf(stderr, "testAppendFlags: done\n\n");
}

// ---- testNavModesFlagsString ----

static void testNavModesFlagsString(void) {
    fprintf(stderr, "=== testNavModesFlagsString ===\n");

    // No flags -> empty string
    {
        const char *result = nav_modes_flags_string(0);
        ASSERT_STR_EQ("nmfs none", result, "");
    }

    // Two flags -> space-separated, no quotes
    {
        const char *result = nav_modes_flags_string(NAV_MODE_AUTOPILOT | NAV_MODE_LNAV);
        ASSERT_STR_EQ("nmfs two", result, "autopilot lnav");
    }

    fprintf(stderr, "testNavModesFlagsString: done\n\n");
}

// ---- testSprintAircraftObjectUavPrefix ----

static void testSprintAircraftObjectUavPrefix(void) {
    fprintf(stderr, "=== testSprintAircraftObjectUavPrefix ===\n");

    char buf[4096];
    int64_t now = mstime();

    // Case 1: UAV address → "$" prefix
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.addrtype = ADDR_UAV;
        a.addr = 0x000001;
        char *p = sprintAircraftObject(buf, buf + sizeof(buf), &a, now, 0, NULL);
        *p = '\0';
        ASSERT_TRUE("uav $ prefix", strstr(buf, "\"hex\":\"$000001\"") != NULL);
        ASSERT_TRUE("uav type", strstr(buf, "\"type\":\"adsb_other\"") != NULL);
    }

    // Case 2: Non-ICAO address → "~" prefix
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.addrtype = ADDR_TISB_OTHER;
        a.addr = 0x000002 | MODES_NON_ICAO_ADDRESS;
        char *p = sprintAircraftObject(buf, buf + sizeof(buf), &a, now, 0, NULL);
        *p = '\0';
        ASSERT_TRUE("non-icao ~ prefix", strstr(buf, "\"hex\":\"~000002\"") != NULL);
    }

    // Case 3: Normal ICAO address → no prefix
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.addrtype = ADDR_ADSB_ICAO;
        a.addr = 0xABCDEF;
        char *p = sprintAircraftObject(buf, buf + sizeof(buf), &a, now, 0, NULL);
        *p = '\0';
        ASSERT_TRUE("icao no prefix", strstr(buf, "\"hex\":\"abcdef\"") != NULL);
    }

    // Case 4: printMode=1 (trace) skips hex entirely
    {
        struct aircraft a;
        memset(&a, 0, sizeof(a));
        a.addrtype = ADDR_UAV;
        a.addr = 0x000001;
        char *p = sprintAircraftObject(buf, buf + sizeof(buf), &a, now, 1, NULL);
        *p = '\0';
        ASSERT_TRUE("trace no hex", strstr(buf, "\"hex\"") == NULL);
    }

    fprintf(stderr, "testSprintAircraftObjectUavPrefix: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testTrimSpace();
    testJsonEscapeString();
    testAppendNavModes();
    testGetSignal();
    testAppendFlags();
    testNavModesFlagsString();
    testSprintAircraftObjectUavPrefix();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
