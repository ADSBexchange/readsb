// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// api_tests.c - unit tests for pure filter/search/hash functions from api.c
//
// Uses #include "api.c" to access static functions directly.
// Provides linker stubs for external symbols that api.c references
// but that we don't exercise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <zstd.h>

#include "readsb.h"

// ---- Linker stubs (signatures must match header declarations) ----

struct _Modes Modes;
struct _Threads Threads;

void setExit(int __attribute__((unused)) arg) { }
int64_t mstime(void) { return 1700000000000LL; }
int64_t microtime(void) { return 1700000000000000LL; }
double greatcircle(double __attribute__((unused)) lat0, double __attribute__((unused)) lon0,
                   double __attribute__((unused)) lat1, double __attribute__((unused)) lon1,
                   int __attribute__((unused)) approx) { return 0; }
double bearing(double __attribute__((unused)) lat0, double __attribute__((unused)) lon0,
               double __attribute__((unused)) lat1, double __attribute__((unused)) lon1) { return 0; }
struct aircraft *aircraftGet(uint32_t __attribute__((unused)) addr) { return NULL; }
int receiverPositionReceived(struct aircraft __attribute__((unused)) *a,
                             struct modesMessage __attribute__((unused)) *mm,
                             double __attribute__((unused)) lat,
                             double __attribute__((unused)) lon,
                             int64_t __attribute__((unused)) now) { return 0; }
int includeAircraftJson(int64_t __attribute__((unused)) now,
                        struct aircraft __attribute__((unused)) *a) { return 0; }
void toBinCraft(struct aircraft __attribute__((unused)) *a,
                struct binCraft __attribute__((unused)) *new,
                int64_t __attribute__((unused)) now) { }
struct char_buffer generateAircraftJson(int64_t __attribute__((unused)) onlyRecent) {
    return (struct char_buffer){0};
}
struct char_buffer generateGlobeJson(int __attribute__((unused)) gi,
                                     threadpool_buffer_t __attribute__((unused)) *buffer) {
    return (struct char_buffer){0};
}
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];
struct char_buffer ident(struct char_buffer __attribute__((unused)) target) {
    return (struct char_buffer){0};
}
int globe_index(double __attribute__((unused)) lat_in, double __attribute__((unused)) lon_in) { return 0; }
void ca_lock_read(struct craftArray __attribute__((unused)) *ca) { }
void ca_unlock_read(struct craftArray __attribute__((unused)) *ca) { }
unsigned int get_seed(void) { return 42; }
void start_cpu_timing(struct timespec __attribute__((unused)) *start_time) { }
void end_cpu_timing(const struct timespec __attribute__((unused)) *start_time,
                    struct timespec __attribute__((unused)) *add_to) { }
void threadTimedWait(threadT __attribute__((unused)) *thread,
                     struct timespec __attribute__((unused)) *ts,
                     int64_t __attribute__((unused)) increment) { }
int threadAffinity(int __attribute__((unused)) core_id) { return 0; }
int my_epoll_create(int __attribute__((unused)) *event_fd_ptr) { return 0; }
static struct epoll_event dummy_epoll_events[1];
void epollAllocEvents(struct epoll_event **events,
                      int *maxEvents) {
    *events = dummy_epoll_events;
    *maxEvents = 1;
}
void threadSignalJoin(threadT __attribute__((unused)) *thread) { }
void serviceListen(struct net_service __attribute__((unused)) *service,
                   char __attribute__((unused)) *bind_addr,
                   char __attribute__((unused)) *bind_ports,
                   int __attribute__((unused)) epfd) { }
void *check_grow_threadpool_buffer_t(threadpool_buffer_t __attribute__((unused)) *buffer,
                                     ssize_t __attribute__((unused)) newSize) { return NULL; }
void threadCreate(threadT __attribute__((unused)) *thread,
                  const pthread_attr_t __attribute__((unused)) *attr,
                  void __attribute__((unused)) *(*start_routine)(void *),
                  void __attribute__((unused)) *arg) { }

// ---- Include api.c to access static functions ----
#include "api.c"

// ---- test helpers ----

static int failures = 0;

#define ASSERT_TRUE(tag, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s: FAIL: condition false\n", tag); \
        failures++; \
    } \
} while(0)

#define ASSERT_INT_EQ(tag, got, expected) do { \
    if ((got) != (expected)) { \
        fprintf(stderr, "%s: FAIL: got %d, expected %d\n", tag, (int)(got), (int)(expected)); \
        failures++; \
    } \
} while(0)

// ---- testHexHash ----

static void testHexHash(void) {
    fprintf(stderr, "=== testHexHash ===\n");

    uint32_t mask = (1u << API_HASH_BITS) - 1;

    uint32_t h1 = hexHash(0x4840D6);
    uint32_t h2 = hexHash(0x4840D6);
    ASSERT_INT_EQ("hex consistent", h1, h2);
    ASSERT_TRUE("hex in range", h1 <= mask);

    uint32_t h3 = hexHash(0xABCDEF);
    ASSERT_TRUE("hex diff addr", h1 != h3);
    ASSERT_TRUE("hex diff in range", h3 <= mask);

    uint32_t h0 = hexHash(0);
    ASSERT_TRUE("hex zero in range", h0 <= mask);

    uint32_t hmax = hexHash(0xFFFFFF);
    ASSERT_TRUE("hex max in range", hmax <= mask);

    fprintf(stderr, "testHexHash: done\n\n");
}

// ---- testRegHash ----

static void testRegHash(void) {
    fprintf(stderr, "=== testRegHash ===\n");

    uint32_t mask = (1u << API_HASH_BITS) - 1;

    char reg1[12] = "N12345      ";
    uint32_t h1 = regHash(reg1);
    uint32_t h2 = regHash(reg1);
    ASSERT_INT_EQ("reg consistent", h1, h2);
    ASSERT_TRUE("reg in range", h1 <= mask);

    char reg2[12] = "G-ABCD      ";
    uint32_t h3 = regHash(reg2);
    ASSERT_TRUE("reg diff", h1 != h3);
    ASSERT_TRUE("reg diff in range", h3 <= mask);

    char reg3[12] = "            ";
    uint32_t h4 = regHash(reg3);
    ASSERT_TRUE("reg empty in range", h4 <= mask);

    fprintf(stderr, "testRegHash: done\n\n");
}

// ---- testCallsignHash ----

static void testCallsignHash(void) {
    fprintf(stderr, "=== testCallsignHash ===\n");

    uint32_t mask = (1u << API_HASH_BITS) - 1;

    char cs1[8] = "BAW256  ";
    uint32_t h1 = callsignHash(cs1);
    uint32_t h2 = callsignHash(cs1);
    ASSERT_INT_EQ("cs consistent", h1, h2);
    ASSERT_TRUE("cs in range", h1 <= mask);

    char cs2[8] = "DLH1234 ";
    uint32_t h3 = callsignHash(cs2);
    ASSERT_TRUE("cs diff", h1 != h3);
    ASSERT_TRUE("cs diff in range", h3 <= mask);

    fprintf(stderr, "testCallsignHash: done\n\n");
}

// ---- testCompareLon ----

static void testCompareLon(void) {
    fprintf(stderr, "=== testCompareLon ===\n");

    struct apiEntry e1 = {0}, e2 = {0}, e3 = {0};
    e1.bin.lon = -1000000;
    e2.bin.lon = 0;
    e3.bin.lon = 1000000;

    ASSERT_TRUE("cmp e1<e2", compareLon(&e1, &e2) < 0);
    ASSERT_TRUE("cmp e2<e3", compareLon(&e2, &e3) < 0);
    ASSERT_TRUE("cmp e3>e1", compareLon(&e3, &e1) > 0);
    ASSERT_TRUE("cmp equal", compareLon(&e1, &e1) == 0);

    struct apiEntry arr[3] = {e3, e1, e2};
    qsort(arr, 3, sizeof(struct apiEntry), compareLon);
    ASSERT_TRUE("qsort[0]", arr[0].bin.lon == -1000000);
    ASSERT_TRUE("qsort[1]", arr[1].bin.lon == 0);
    ASSERT_TRUE("qsort[2]", arr[2].bin.lon == 1000000);

    fprintf(stderr, "testCompareLon: done\n\n");
}

// ---- testFindLonRange ----

static void testFindLonRange(void) {
    fprintf(stderr, "=== testFindLonRange ===\n");

    struct apiEntry entries[5];
    memset(entries, 0, sizeof(entries));
    entries[0].bin.lon = -100 * (int32_t)1000000;
    entries[1].bin.lon =  -50 * (int32_t)1000000;
    entries[2].bin.lon =    0;
    entries[3].bin.lon =   50 * (int32_t)1000000;
    entries[4].bin.lon =  100 * (int32_t)1000000;

    struct range r = findLonRange(-180 * (int32_t)1000000, 180 * (int32_t)1000000, entries, 5);
    ASSERT_INT_EQ("all from", r.from, 0);
    ASSERT_INT_EQ("all to", r.to, 5);

    r = findLonRange(-50 * (int32_t)1000000, 50 * (int32_t)1000000, entries, 5);
    ASSERT_INT_EQ("mid from", r.from, 1);
    ASSERT_INT_EQ("mid to", r.to, 4);

    r = findLonRange(10 * (int32_t)1000000, 20 * (int32_t)1000000, entries, 5);
    ASSERT_TRUE("none: empty", r.to - r.from == 0);

    r = findLonRange(-180 * (int32_t)1000000, 180 * (int32_t)1000000, entries, 0);
    ASSERT_INT_EQ("empty from", r.from, 0);
    ASSERT_INT_EQ("empty to", r.to, 0);

    struct apiEntry single[1];
    memset(single, 0, sizeof(single));
    single[0].bin.lon = 0;
    r = findLonRange(-10 * (int32_t)1000000, 10 * (int32_t)1000000, single, 1);
    ASSERT_INT_EQ("single from", r.from, 0);
    ASSERT_INT_EQ("single to", r.to, 1);

    r = findLonRange(10 * (int32_t)1000000, 20 * (int32_t)1000000, single, 1);
    ASSERT_TRUE("single nomatch", r.to - r.from == 0);

    r = findLonRange(50 * (int32_t)1000000, -50 * (int32_t)1000000, entries, 5);
    ASSERT_INT_EQ("inverted from", r.from, 0);
    ASSERT_INT_EQ("inverted to", r.to, 0);

    fprintf(stderr, "testFindLonRange: done\n\n");
}

// ---- testFilterAltBaro ----

static void testFilterAltBaro(void) {
    fprintf(stderr, "=== testFilterAltBaro ===\n");

    struct apiEntry haystack[4];
    memset(haystack, 0, sizeof(haystack));

    haystack[0].bin.baro_alt = (int16_t)(35000 * BINCRAFT_ALT_FACTOR);
    haystack[0].bin.baro_alt_valid = 1;

    haystack[1].bin.baro_alt = (int16_t)(5000 * BINCRAFT_ALT_FACTOR);
    haystack[1].bin.baro_alt_valid = 1;

    haystack[2].bin.airground = AG_GROUND;
    haystack[2].bin.baro_alt_valid = 0;

    haystack[3].bin.baro_alt_valid = 0;
    haystack[3].bin.airground = AG_AIRBORNE;

    struct apiEntry matches[4];
    struct apiOptions options;
    memset(&options, 0, sizeof(options));
    size_t alloc = 0;

    options.above_alt_baro = 0;
    options.below_alt_baro = 10000;
    int count = filter_alt_baro(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("alt 0-10k count", count, 2);

    alloc = 0;
    options.above_alt_baro = 30000;
    options.below_alt_baro = 40000;
    count = filter_alt_baro(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("alt 30k-40k count", count, 1);

    alloc = 0;
    options.above_alt_baro = 10000;
    options.below_alt_baro = 20000;
    count = filter_alt_baro(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("alt 10k-20k count", count, 0);

    fprintf(stderr, "testFilterAltBaro: done\n\n");
}

// ---- testFilterDbFlags ----

static void testFilterDbFlags(void) {
    fprintf(stderr, "=== testFilterDbFlags ===\n");

    struct apiEntry haystack[4];
    memset(haystack, 0, sizeof(haystack));
    haystack[0].bin.dbFlags = 1;
    haystack[1].bin.dbFlags = 2;
    haystack[2].bin.dbFlags = 4;
    haystack[3].bin.dbFlags = 8;

    struct apiEntry matches[4];
    struct apiOptions options;
    size_t alloc;

    memset(&options, 0, sizeof(options));
    options.filter_mil = 1;
    alloc = 0;
    int count = filter_dbFlags(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("dbflags mil count", count, 1);
    ASSERT_TRUE("dbflags mil hex", matches[0].bin.dbFlags == 1);

    memset(&options, 0, sizeof(options));
    options.filter_interesting = 1;
    alloc = 0;
    count = filter_dbFlags(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("dbflags interesting", count, 1);

    memset(&options, 0, sizeof(options));
    options.filter_pia = 1;
    alloc = 0;
    count = filter_dbFlags(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("dbflags pia", count, 1);

    memset(&options, 0, sizeof(options));
    options.filter_ladd = 1;
    alloc = 0;
    count = filter_dbFlags(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("dbflags ladd", count, 1);

    memset(&options, 0, sizeof(options));
    options.filter_mil = 1;
    options.filter_interesting = 1;
    alloc = 0;
    count = filter_dbFlags(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("dbflags mil|int", count, 2);

    memset(&options, 0, sizeof(options));
    alloc = 0;
    count = filter_dbFlags(haystack, 4, matches, &alloc, &options);
    ASSERT_INT_EQ("dbflags none", count, 0);

    fprintf(stderr, "testFilterDbFlags: done\n\n");
}

// ---- testFilterSquawk ----

static void testFilterSquawk(void) {
    fprintf(stderr, "=== testFilterSquawk ===\n");

    struct apiEntry haystack[3];
    memset(haystack, 0, sizeof(haystack));
    haystack[0].bin.squawk = 7700;
    haystack[0].bin.squawk_valid = 1;
    haystack[1].bin.squawk = 7000;
    haystack[1].bin.squawk_valid = 1;
    haystack[2].bin.squawk = 7700;
    haystack[2].bin.squawk_valid = 0;

    struct apiEntry matches[3];
    size_t alloc = 0;

    int count = filterSquawk(haystack, 3, matches, &alloc, 7700);
    ASSERT_INT_EQ("squawk 7700 count", count, 1);
    ASSERT_TRUE("squawk 7700 match", matches[0].bin.squawk == 7700);
    ASSERT_TRUE("squawk 7700 valid", matches[0].bin.squawk_valid);

    alloc = 0;
    count = filterSquawk(haystack, 3, matches, &alloc, 7000);
    ASSERT_INT_EQ("squawk 7000 count", count, 1);

    alloc = 0;
    count = filterSquawk(haystack, 3, matches, &alloc, 1200);
    ASSERT_INT_EQ("squawk 1200 count", count, 0);

    fprintf(stderr, "testFilterSquawk: done\n\n");
}

// ---- testFilterCallsign ----

static void testFilterCallsign(void) {
    fprintf(stderr, "=== testFilterCallsign ===\n");

    struct apiEntry haystack[3];
    memset(haystack, 0, sizeof(haystack));

    memcpy(haystack[0].bin.callsign, "BAW256  ", 8);
    haystack[0].bin.callsign_valid = 1;

    memcpy(haystack[1].bin.callsign, "BAW1234 ", 8);
    haystack[1].bin.callsign_valid = 1;

    memcpy(haystack[2].bin.callsign, "DLH100  ", 8);
    haystack[2].bin.callsign_valid = 1;

    struct apiEntry matches[3];
    size_t alloc;

    alloc = 0;
    int count = filterCallsignPrefix(haystack, 3, matches, &alloc, "BAW");
    ASSERT_INT_EQ("prefix BAW count", count, 2);

    alloc = 0;
    count = filterCallsignPrefix(haystack, 3, matches, &alloc, "DLH");
    ASSERT_INT_EQ("prefix DLH count", count, 1);

    alloc = 0;
    count = filterCallsignPrefix(haystack, 3, matches, &alloc, "AAL");
    ASSERT_INT_EQ("prefix AAL count", count, 0);

    char exact[9] = "BAW256\0\0";
    alloc = 0;
    count = filterCallsignExact(haystack, 3, matches, &alloc, exact);
    ASSERT_INT_EQ("exact BAW256 count", count, 1);

    char exact2[9] = "BAW999\0\0";
    alloc = 0;
    count = filterCallsignExact(haystack, 3, matches, &alloc, exact2);
    ASSERT_INT_EQ("exact BAW999 count", count, 0);

    alloc = 0;
    count = filterCallsignPrefix(haystack, 3, matches, &alloc, "baw");
    ASSERT_INT_EQ("prefix baw (case) count", count, 0);

    fprintf(stderr, "testFilterCallsign: done\n\n");
}

// ---- testFindInBox ----

static void testFindInBox(void) {
    fprintf(stderr, "=== testFindInBox ===\n");

    struct apiEntry entries[5];
    memset(entries, 0, sizeof(entries));

    entries[0].bin.lat = (int32_t)(51.5 * 1E6);
    entries[0].bin.lon = (int32_t)(-0.1 * 1E6);
    entries[0].bin.position_valid = 1;

    entries[1].bin.lat = (int32_t)(48.8 * 1E6);
    entries[1].bin.lon = (int32_t)(2.3 * 1E6);
    entries[1].bin.position_valid = 1;

    entries[2].bin.lat = (int32_t)(40.7 * 1E6);
    entries[2].bin.lon = (int32_t)(-74.0 * 1E6);
    entries[2].bin.position_valid = 1;

    entries[3].bin.lat = (int32_t)(35.7 * 1E6);
    entries[3].bin.lon = (int32_t)(139.7 * 1E6);
    entries[3].bin.position_valid = 1;

    entries[4].bin.lat = (int32_t)(-33.9 * 1E6);
    entries[4].bin.lon = (int32_t)(151.2 * 1E6);
    entries[4].bin.position_valid = 1;

    qsort(entries, 5, sizeof(struct apiEntry), compareLon);

    struct apiEntry matches[5];
    struct apiOptions options;
    size_t alloc;

    memset(&options, 0, sizeof(options));
    options.box[0] = 40.0;
    options.box[1] = 55.0;
    options.box[2] = -10.0;
    options.box[3] = 10.0;
    options.is_box = 1;
    alloc = 0;
    int count = findInBox(entries, 5, &options, matches, &alloc);
    ASSERT_INT_EQ("europe box count", count, 2);

    memset(&options, 0, sizeof(options));
    options.box[0] = 35.0;
    options.box[1] = 45.0;
    options.box[2] = -80.0;
    options.box[3] = -70.0;
    options.is_box = 1;
    alloc = 0;
    count = findInBox(entries, 5, &options, matches, &alloc);
    ASSERT_INT_EQ("us east box count", count, 1);

    memset(&options, 0, sizeof(options));
    options.box[0] = 60.0;
    options.box[1] = 70.0;
    options.box[2] = 10.0;
    options.box[3] = 20.0;
    options.is_box = 1;
    alloc = 0;
    count = findInBox(entries, 5, &options, matches, &alloc);
    ASSERT_INT_EQ("empty box count", count, 0);

    memset(&options, 0, sizeof(options));
    options.box[0] = -40.0;
    options.box[1] = 40.0;
    options.box[2] = 130.0;
    options.box[3] = -170.0;
    options.is_box = 1;
    alloc = 0;
    count = findInBox(entries, 5, &options, matches, &alloc);
    ASSERT_INT_EQ("dateline box count", count, 2);

    fprintf(stderr, "testFindInBox: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testHexHash();
    testRegHash();
    testCallsignHash();
    testCompareLon();
    testFindLonRange();
    testFilterAltBaro();
    testFilterDbFlags();
    testFilterSquawk();
    testFilterCallsign();
    testFindInBox();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
