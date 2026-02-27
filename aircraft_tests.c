// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// aircraft_tests.c - unit tests for static functions in aircraft.c
//
// Uses #include "aircraft.c" to access static functions directly.
// Provides linker stubs for external symbols that aircraft.c references
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
void startWatch(struct timespec __attribute__((unused)) *start_time) { }
int64_t stopWatch(struct timespec __attribute__((unused)) *start_time) { return 0; }
int nogps(int64_t __attribute__((unused)) now, struct aircraft __attribute__((unused)) *a) { return 0; }
void set_globe_index(struct aircraft __attribute__((unused)) *a, int __attribute__((unused)) new_index) { }
void ca_remove(struct craftArray __attribute__((unused)) *ca, struct aircraft __attribute__((unused)) *a) { }
void traceCleanup(struct aircraft __attribute__((unused)) *a) { }
struct char_buffer writeJsonToFile(const char __attribute__((unused)) *dir,
                                   const char __attribute__((unused)) *file,
                                   struct char_buffer __attribute__((unused)) cb) {
    return (struct char_buffer){0};
}
struct char_buffer generateReceiverJson(void) { return (struct char_buffer){0}; }
struct char_buffer readWholeGz(gzFile __attribute__((unused)) gzfp,
                               char __attribute__((unused)) *errorContext) {
    return (struct char_buffer){0};
}
void threadpool_distribute_and_run(threadpool_t __attribute__((unused)) *pool,
                                   task_group_t __attribute__((unused)) *task_group,
                                   threadpool_function_t __attribute__((unused)) func,
                                   int __attribute__((unused)) totalRange,
                                   int __attribute__((unused)) taskCount,
                                   int64_t __attribute__((unused)) now) { }

// ---- Include aircraft.c to access static functions ----
#include "aircraft.c"

// ---- test helpers ----

static int failures = 0;

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL\n", tag); \
        failures++; \
    } \
} while(0)

#define ASSERT_INT_EQ(tag, got, expected) do { \
    int _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_UINT_EQ(tag, got, expected) do { \
    unsigned _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %u, expected %u\n", tag, _g, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_STR_EQ(tag, got, expected) do { \
    if (strcmp((got), (expected)) != 0) { \
        fprintf(stderr, "%s: FAIL: got \"%s\", expected \"%s\"\n", tag, (got), (expected)); \
        failures++; \
    } \
} while(0)

#define ASSERT_STRN_EQ(tag, got, expected, n) do { \
    if (strncmp((got), (expected), (n)) != 0) { \
        fprintf(stderr, "%s: FAIL: got \"%.*s\", expected \"%s\"\n", tag, (int)(n), (got), (expected)); \
        failures++; \
    } \
} while(0)

// ---- testNextToken_basic ----

static void testNextToken_basic(void) {
    fprintf(stderr, "=== testNextToken_basic ===\n");

    // nextToken expects all tokens to be followed by a delimiter.
    // Use strdup so compiler cannot trace array bounds through eot = line - 1.
    char *line = strdup("abc;def;ghi;");
    char *eol = line + strlen(line);
    char *sot, *eot;

    // Initialize: eot points just before the start.
    // The actual usage in aircraft.c has eot = buf - 1 initially.
    eot = line - 1;

    // First token: "abc"
    int ok = nextToken(';', &sot, &eot, &eol);
    ASSERT_TRUE("basic t1 ok", ok == 1);
    ASSERT_STR_EQ("basic t1", sot, "abc");

    // Second token: "def"
    ok = nextToken(';', &sot, &eot, &eol);
    ASSERT_TRUE("basic t2 ok", ok == 1);
    ASSERT_STR_EQ("basic t2", sot, "def");

    // Third token: "ghi"
    ok = nextToken(';', &sot, &eot, &eol);
    ASSERT_TRUE("basic t3 ok", ok == 1);
    ASSERT_STR_EQ("basic t3", sot, "ghi");

    // No more tokens after trailing delimiter
    ok = nextToken(';', &sot, &eot, &eol);
    ASSERT_TRUE("basic t4 end", ok == 0);
    free(line);

    // Verify: without trailing delimiter, last token is not returned
    {
        char *line2 = strdup("abc;def;ghi");
        char *eol2 = line2 + strlen(line2);
        char *sot2, *eot2 = line2 - 1;
        int count = 0;
        while (nextToken(';', &sot2, &eot2, &eol2))
            count++;
        ASSERT_INT_EQ("basic no trailing count", count, 2);
        free(line2);
    }

    fprintf(stderr, "testNextToken_basic: done\n\n");
}

// ---- testNextToken_edge_cases ----

static void testNextToken_edge_cases(void) {
    fprintf(stderr, "=== testNextToken_edge_cases ===\n");

    // Empty token between delimiters: "a;;b;"
    // Use strdup so compiler cannot trace array bounds through eot = line - 1.
    {
        char *line = strdup("a;;b;");
        char *eol = line + strlen(line);
        char *sot, *eot = line - 1;

        int ok = nextToken(';', &sot, &eot, &eol);
        ASSERT_TRUE("empty t1 ok", ok == 1);
        ASSERT_STR_EQ("empty t1", sot, "a");

        ok = nextToken(';', &sot, &eot, &eol);
        ASSERT_TRUE("empty t2 ok", ok == 1);
        ASSERT_STR_EQ("empty t2 (empty)", sot, "");

        ok = nextToken(';', &sot, &eot, &eol);
        ASSERT_TRUE("empty t3 ok", ok == 1);
        ASSERT_STR_EQ("empty t3", sot, "b");

        // Past end
        ok = nextToken(';', &sot, &eot, &eol);
        ASSERT_TRUE("empty t4 end", ok == 0);
        free(line);
    }

    // Single token, no delimiter: "hello"
    {
        char *line = strdup("hello");
        char *eol = line + strlen(line);
        char *sot, *eot = line - 1;

        // No delimiter found, nextToken returns 0
        int ok = nextToken(';', &sot, &eot, &eol);
        ASSERT_TRUE("single no delim", ok == 0);
        free(line);
    }

    // Empty string
    {
        char *line = strdup("");
        char *eol = line;
        char *sot, *eot = line - 1;

        int ok = nextToken(';', &sot, &eot, &eol);
        ASSERT_TRUE("empty string", ok == 0);
        free(line);
    }

    fprintf(stderr, "testNextToken_edge_cases: done\n\n");
}

// ---- testSanitize_quotes_and_control ----

static void testSanitize_quotes_and_control(void) {
    fprintf(stderr, "=== testSanitize_quotes_and_control ===\n");

    // Double quote replaced with single quote
    {
        char str[] = "Hello \"World\"";
        sanitize(str, strlen(str));
        ASSERT_STR_EQ("sanitize quotes", str, "Hello 'World'");
    }

    // Control characters (0x01-0x1E) replaced with space
    {
        char str[] = "A\x01" "B\x0A" "C";
        sanitize(str, strlen(str));
        ASSERT_STR_EQ("sanitize ctrl", str, "A B C");
    }

    // Trailing backslash removed
    {
        char str[] = "test\\";
        sanitize(str, strlen(str));
        ASSERT_STR_EQ("sanitize trailing backslash", str, "test");
    }

    // Mixed: quote + control + backslash
    {
        char str[] = "\"hi\x05\\";
        sanitize(str, strlen(str));
        ASSERT_STR_EQ("sanitize mixed", str, "'hi ");
    }

    fprintf(stderr, "testSanitize_quotes_and_control: done\n\n");
}

// ---- testSanitize_broken_utf8 ----

static void testSanitize_broken_utf8(void) {
    fprintf(stderr, "=== testSanitize_broken_utf8 ===\n");

    // 2-byte UTF-8 leader at last position (broken, no continuation byte)
    {
        char str[4] = {'A', 'B', (char)0xC0, '\0'};
        sanitize(str, 3);
        // The broken 2-byte leader at str[2] should be NUL'd
        ASSERT_TRUE("utf8 2byte broken", str[2] == '\0');
        ASSERT_TRUE("utf8 2byte prefix", str[0] == 'A' && str[1] == 'B');
    }

    // 3-byte UTF-8 leader at second-to-last position (broken)
    {
        char str[4] = {'A', (char)0xE0, 'X', '\0'};
        sanitize(str, 3);
        // The broken 3-byte leader at str[1] should be NUL'd
        ASSERT_TRUE("utf8 3byte broken", str[1] == '\0');
    }

    // 4-byte UTF-8 leader at third-to-last position (broken)
    {
        char str[5] = {'A', (char)0xF0, 'X', 'Y', '\0'};
        sanitize(str, 4);
        // The broken 4-byte leader at str[1] should be NUL'd
        ASSERT_TRUE("utf8 4byte broken", str[1] == '\0');
    }

    fprintf(stderr, "testSanitize_broken_utf8: done\n\n");
}

// ---- testSanitize_clean ----

static void testSanitize_clean(void) {
    fprintf(stderr, "=== testSanitize_clean ===\n");

    // Clean ASCII passes through unchanged
    {
        char str[] = "Hello World 123";
        char expected[] = "Hello World 123";
        sanitize(str, strlen(str));
        ASSERT_STR_EQ("sanitize clean ascii", str, expected);
    }

    // Empty string
    {
        char str[] = "";
        sanitize(str, 0);
        ASSERT_STR_EQ("sanitize empty", str, "");
    }

    fprintf(stderr, "testSanitize_clean: done\n\n");
}

// ---- testAircraftHash ----

static void testAircraftHash(void) {
    fprintf(stderr, "=== testAircraftHash ===\n");

    uint32_t aircraft_mask = (1u << AIRCRAFT_HASH_BITS) - 1;
    uint32_t db_mask = (1u << DB_HASH_BITS) - 1;

    // aircraftHash result fits within AIRCRAFT_HASH_BITS
    uint32_t h1 = aircraftHash(0x4840D6);
    ASSERT_TRUE("ac hash in range", h1 <= aircraft_mask);

    // dbHash result fits within DB_HASH_BITS
    uint32_t h2 = dbHash(0x4840D6);
    ASSERT_TRUE("db hash in range", h2 <= db_mask);

    // Consistency: same input -> same output
    uint32_t h3 = aircraftHash(0x4840D6);
    ASSERT_UINT_EQ("ac hash consistent", h1, h3);

    uint32_t h4 = dbHash(0x4840D6);
    ASSERT_UINT_EQ("db hash consistent", h2, h4);

    // Different inputs -> likely different outputs
    uint32_t h5 = aircraftHash(0xABCDEF);
    ASSERT_TRUE("ac hash diff addr", h1 != h5);

    // Zero address
    uint32_t h6 = aircraftHash(0);
    ASSERT_TRUE("ac hash zero in range", h6 <= aircraft_mask);

    // Max address
    uint32_t h7 = aircraftHash(0xFFFFFF);
    ASSERT_TRUE("ac hash max in range", h7 <= aircraft_mask);

    fprintf(stderr, "testAircraftHash: done\n\n");
}

// ---- testSprintDB ----

static void testSprintDB(void) {
    fprintf(stderr, "=== testSprintDB ===\n");

    char buf[1024];

    // Full entry with all fields
    {
        dbEntry d = {0};
        d.addr = 0x4840D6;
        memcpy(d.registration, "G-EUPJ", 6);
        memcpy(d.typeCode, "A320", 4);
        memcpy(d.typeLong, "Airbus A320", 11);
        d.dbFlags = 0;
        memcpy(d.ownOp, "British Airways", 15);
        memcpy(d.year, "2005", 4);

        char *p = buf;
        char *end = buf + sizeof(buf);
        p = sprintDB(p, end, &d);
        *p = '\0';

        ASSERT_TRUE("sprintDB full has hex", strstr(buf, "\"4840d6\"") != NULL);
        ASSERT_TRUE("sprintDB full has reg", strstr(buf, "\"r\":\"G-EUPJ") != NULL);
        ASSERT_TRUE("sprintDB full has type", strstr(buf, "\"t\":\"A320") != NULL);
        ASSERT_TRUE("sprintDB full has desc", strstr(buf, "\"desc\":\"Airbus A320") != NULL);
        ASSERT_TRUE("sprintDB full has ownOp", strstr(buf, "\"ownOp\":\"British Airways") != NULL);
        ASSERT_TRUE("sprintDB full has year", strstr(buf, "\"year\":\"2005") != NULL);
        ASSERT_TRUE("sprintDB full no noRegData", strstr(buf, "noRegData") == NULL);
    }

    // Empty entry -> noRegData:true
    {
        dbEntry d = {0};
        d.addr = 0x123456;

        char *p = buf;
        char *end = buf + sizeof(buf);
        p = sprintDB(p, end, &d);
        *p = '\0';

        ASSERT_TRUE("sprintDB empty noRegData", strstr(buf, "\"noRegData\":true") != NULL);
        ASSERT_TRUE("sprintDB empty hex", strstr(buf, "\"123456\"") != NULL);
    }

    // UAV address (has $ prefix)
    {
        dbEntry d = {0};
        d.addr = MODES_UAV_ADDRESS | 0x000001;
        memcpy(d.registration, "DRONE1", 6);

        char *p = buf;
        char *end = buf + sizeof(buf);
        p = sprintDB(p, end, &d);
        *p = '\0';

        ASSERT_TRUE("sprintDB UAV prefix", strstr(buf, "\"$000001\"") != NULL);
    }

    // Non-ICAO address (has ~ prefix)
    {
        dbEntry d = {0};
        d.addr = MODES_NON_ICAO_ADDRESS | 0x000002;
        memcpy(d.registration, "TEST", 4);

        char *p = buf;
        char *end = buf + sizeof(buf);
        p = sprintDB(p, end, &d);
        *p = '\0';

        ASSERT_TRUE("sprintDB non-ICAO prefix", strstr(buf, "\"~000002\"") != NULL);
    }

    // dbFlags field present when nonzero
    {
        dbEntry d = {0};
        d.addr = 0xABCDEF;
        d.dbFlags = 3;
        memcpy(d.registration, "N12345", 6);

        char *p = buf;
        char *end = buf + sizeof(buf);
        p = sprintDB(p, end, &d);
        *p = '\0';

        ASSERT_TRUE("sprintDB flags present", strstr(buf, "\"dbFlags\":3") != NULL);
    }

    fprintf(stderr, "testSprintDB: done\n\n");
}

// ---- testQuickCache ----

static void testQuickCache(void) {
    fprintf(stderr, "=== testQuickCache ===\n");

    // Init quick cache
    quickBits = 0; // force init from scratch
    quickResize(quickMinBits);
    ASSERT_TRUE("quick init", quick != NULL);

    // Create 3 test aircraft with different addresses
    struct aircraft a1, a2, a3;
    memset(&a1, 0, sizeof(a1)); a1.addr = 0x4840D6;
    memset(&a2, 0, sizeof(a2)); a2.addr = 0xABCDEF;
    memset(&a3, 0, sizeof(a3)); a3.addr = 0x123456;

    // Get miss before add
    ASSERT_TRUE("quick miss", quickGet(0x4840D6) == NULL);

    // Add + Get
    quickAdd(&a1);
    {
        struct ap *q = quickGet(0x4840D6);
        ASSERT_TRUE("quick add+get", q != NULL);
        ASSERT_TRUE("quick ptr", q->ptr == &a1);
        ASSERT_UINT_EQ("quick addr", q->addr, 0x4840D6);
    }

    // Duplicate add — no change
    quickAdd(&a1);
    {
        struct ap *q = quickGet(0x4840D6);
        ASSERT_TRUE("quick dup ptr", q != NULL && q->ptr == &a1);
    }

    // Multiple entries
    quickAdd(&a2);
    quickAdd(&a3);
    {
        struct ap *q1 = quickGet(0x4840D6);
        struct ap *q2 = quickGet(0xABCDEF);
        struct ap *q3 = quickGet(0x123456);
        ASSERT_TRUE("quick multi a1", q1 != NULL && q1->ptr == &a1);
        ASSERT_TRUE("quick multi a2", q2 != NULL && q2->ptr == &a2);
        ASSERT_TRUE("quick multi a3", q3 != NULL && q3->ptr == &a3);
    }

    // Remove
    quickRemove(&a1);
    ASSERT_TRUE("quick removed", quickGet(0x4840D6) == NULL);
    // Others still present
    ASSERT_TRUE("quick a2 after rm", quickGet(0xABCDEF) != NULL);
    ASSERT_TRUE("quick a3 after rm", quickGet(0x123456) != NULL);

    quickDestroy();

    fprintf(stderr, "testQuickCache: done\n\n");
}

// ---- testAircraftGetLookup ----

static void testAircraftGetLookup(void) {
    fprintf(stderr, "=== testAircraftGetLookup ===\n");

    // Init quick cache and hash table
    quickBits = 0;
    quickResize(quickMinBits);
    memset(Modes.aircraft, 0, sizeof(Modes.aircraft));

    // Not found
    ASSERT_TRUE("get empty", aircraftGet(0x999999) == NULL);

    // Insert aircraft directly into hash chain (bypass quickAdd)
    struct aircraft *a = cmalloc(sizeof(struct aircraft));
    memset(a, 0, sizeof(*a));
    a->addr = 0x4840D6;
    uint32_t hash = aircraftHash(0x4840D6);
    a->next = Modes.aircraft[hash];
    Modes.aircraft[hash] = a;

    // Clear quick cache so lookup falls through to hash chain
    quickDestroy();
    quickBits = 0;
    quickResize(quickMinBits);

    // Lookup via hash chain
    struct aircraft *found = aircraftGet(0x4840D6);
    ASSERT_TRUE("get hash chain", found == a);

    // After lookup, it should be in quick cache now
    struct ap *q = quickGet(0x4840D6);
    ASSERT_TRUE("get auto-cached", q != NULL && q->ptr == a);

    // Second lookup should come from quick cache
    struct aircraft *found2 = aircraftGet(0x4840D6);
    ASSERT_TRUE("get cached", found2 == a);

    // Cleanup
    Modes.aircraft[hash] = NULL;
    free(a);
    quickDestroy();

    fprintf(stderr, "testAircraftGetLookup: done\n\n");
}

// ---- testAircraftCreateAndFree ----

static void testAircraftCreateAndFree(void) {
    fprintf(stderr, "=== testAircraftCreateAndFree ===\n");

    // Init
    quickBits = 0;
    quickResize(quickMinBits);
    memset(Modes.aircraft, 0, sizeof(Modes.aircraft));
    Modes.dbIndex = NULL;
    Modes.json_globe_index = 0;

    // Create sets addr
    struct aircraft *a = aircraftCreate(0xABCDEF);
    ASSERT_TRUE("create non-null", a != NULL);
    ASSERT_UINT_EQ("create addr", a->addr, 0xABCDEF);

    // Default fields initialized
    ASSERT_INT_EQ("create adsb_version", a->adsb_version, -1);
    ASSERT_TRUE("create callsign zero", a->callsign[0] == '\0');
    ASSERT_UINT_EQ("create addrtype", a->addrtype, ADDR_UNKNOWN);

    // Create again with same addr returns same pointer
    struct aircraft *a2 = aircraftCreate(0xABCDEF);
    ASSERT_TRUE("create dedup", a2 == a);

    // Create with different addr returns different pointer
    struct aircraft *b = aircraftCreate(0x123456);
    ASSERT_TRUE("create different", b != a);
    ASSERT_UINT_EQ("create addr2", b->addr, 0x123456);

    // Cleanup via freeAircraft (stubs handle globe_index, ca_remove, traceCleanup)
    uint32_t hash_a = aircraftHash(0xABCDEF);
    uint32_t hash_b = aircraftHash(0x123456);

    // Remove from hash chains manually before freeAircraft (which does quickRemove + free)
    Modes.aircraft[hash_a] = NULL;
    Modes.aircraft[hash_b] = NULL;
    freeAircraft(a);
    freeAircraft(b);
    quickDestroy();

    fprintf(stderr, "testAircraftCreateAndFree: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testNextToken_basic();
    testNextToken_edge_cases();
    testSanitize_quotes_and_control();
    testSanitize_broken_utf8();
    testSanitize_clean();
    testAircraftHash();
    testSprintDB();
    testQuickCache();
    testAircraftGetLookup();
    testAircraftCreateAndFree();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
