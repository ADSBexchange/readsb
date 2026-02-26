// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// net_io_tests.c - unit tests for static functions in net_io.c
//
// Uses #include "net_io.c" to access static functions directly.
// Provides linker stubs for external symbols that net_io.c references
// but that we don't exercise in the tests.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <zstd.h>

#include "readsb.h"

// ---- Linker stubs ----

struct _Modes Modes;
struct _Threads Threads;
uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

void setExit(int __attribute__((unused)) arg) { }
int64_t mstime(void) { return 1700000000000LL; }
void startWatch(struct timespec __attribute__((unused)) *start_time) { }
int64_t lapWatch(struct timespec __attribute__((unused)) *start_time) { return 0; }
void timespec_add_elapsed(const struct timespec __attribute__((unused)) *start_time,
                          const struct timespec __attribute__((unused)) *end_time,
                          struct timespec __attribute__((unused)) *add_to) { }

// anet.c stubs
int anetTcpServer(char __attribute__((unused)) *err, char __attribute__((unused)) *service,
                  char __attribute__((unused)) *bindaddr, int __attribute__((unused)) *fds,
                  int __attribute__((unused)) nfds, int __attribute__((unused)) flags,
                  int __attribute__((unused)) sndsize, int __attribute__((unused)) rcvsize) { return -1; }
int anetUnixSocket(char __attribute__((unused)) *err, char __attribute__((unused)) *path,
                   int __attribute__((unused)) flags) { return -1; }
int anetGenericAccept(char __attribute__((unused)) *err, int __attribute__((unused)) s,
                      struct sockaddr __attribute__((unused)) *sa,
                      socklen_t __attribute__((unused)) *len,
                      int __attribute__((unused)) flags) { return -1; }
int anetWrite(int __attribute__((unused)) fd, char __attribute__((unused)) *buf,
              int __attribute__((unused)) count) { return 0; }
int anetTcpKeepAlive(char __attribute__((unused)) *err, int __attribute__((unused)) fd) { return 0; }
int anetCreateSocket(char __attribute__((unused)) *err, int __attribute__((unused)) domain,
                     int __attribute__((unused)) typeFlags) { return -1; }
void anetCloseSocket(int __attribute__((unused)) fd) { }

// aircraft.c stubs
struct aircraft *aircraftCreate(uint32_t __attribute__((unused)) addr) { return NULL; }
struct aircraft *aircraftGet(uint32_t __attribute__((unused)) addr) { return NULL; }
int receiverCheckBad(uint64_t __attribute__((unused)) id, int64_t __attribute__((unused)) now) { return 0; }

// mode_s.c stubs
int decodeModesMessage(struct modesMessage __attribute__((unused)) *mm) { return 0; }
void displayModesMessage(struct modesMessage __attribute__((unused)) *mm) { }
void decodeModeAMessage(struct modesMessage __attribute__((unused)) *mm,
                        int __attribute__((unused)) ModeA) { }

// track.c stubs
struct aircraft *trackUpdateFromMessage(struct modesMessage __attribute__((unused)) *mm) { return NULL; }
void receiverPositionChanged(float __attribute__((unused)) lat,
                             float __attribute__((unused)) lon,
                             float __attribute__((unused)) alt) { }

// stats.c / receiver.c stubs
void receiverTimeout(int __attribute__((unused)) part, int __attribute__((unused)) nParts,
                     int64_t __attribute__((unused)) now) { }

// json_out.c stubs
struct char_buffer generateReceiverJson(void) { return (struct char_buffer){0}; }
struct char_buffer generateVRS(int __attribute__((unused)) part,
                               int __attribute__((unused)) n_parts,
                               int __attribute__((unused)) reduced_data) {
    return (struct char_buffer){0};
}
struct char_buffer writeJsonToFile(const char __attribute__((unused)) *dir,
                                   const char __attribute__((unused)) *file,
                                   struct char_buffer __attribute__((unused)) cb) {
    return (struct char_buffer){0};
}
char *sprintAircraftObject(char *p, char __attribute__((unused)) *end,
                           struct aircraft __attribute__((unused)) *a,
                           int64_t __attribute__((unused)) now,
                           int __attribute__((unused)) printMode,
                           struct modesMessage __attribute__((unused)) *mm) { return p; }
// writeJsonToNet and jsonPositionOutput are defined in net_io.c itself

// util.c stubs
zstd_fw_t *createZstdFw(size_t __attribute__((unused)) inBufSize) { return NULL; }
void destroyZstdFw(zstd_fw_t __attribute__((unused)) *fw) { }
void zstdFwFinishFile(zstd_fw_t __attribute__((unused)) *fw) { }
void zstdFwPutData(zstd_fw_t __attribute__((unused)) *fw,
                   const uint8_t __attribute__((unused)) *data,
                   size_t __attribute__((unused)) len) { }
char *sprint_uuid(uint64_t __attribute__((unused)) id1,
                  uint64_t __attribute__((unused)) id2,
                  char *p) { return p; }
void dump_beast_check(int64_t __attribute__((unused)) now) { }
int my_epoll_create(int __attribute__((unused)) *event_fd_ptr) { return 0; }
static struct epoll_event dummy_epoll_events[1];
void epollAllocEvents(struct epoll_event **events, int *maxEvents) {
    *events = dummy_epoll_events;
    *maxEvents = 1;
}

// threadpool.c stubs
threadpool_t *threadpool_create(uint32_t __attribute__((unused)) thread_count,
                                uint32_t __attribute__((unused)) buffer_count) { return NULL; }
void threadpool_destroy(threadpool_t __attribute__((unused)) *pool) { }
void threadpool_run(threadpool_t __attribute__((unused)) *pool,
                    threadpool_task_t __attribute__((unused)) *tasks,
                    uint32_t __attribute__((unused)) count) { }
struct timespec threadpool_get_cumulative_thread_time(threadpool_t __attribute__((unused)) *threadpool) {
    return (struct timespec){0};
}
task_group_t *allocate_task_group(uint32_t __attribute__((unused)) count) { return NULL; }
void destroy_task_group(task_group_t __attribute__((unused)) *group) { }

// readsb.c stubs
int priorityTasksPending(void) { return 0; }
void priorityTasksRun(void) { }

// uat2esnt stubs
void uat2esnt_initCrcTables(void) { }
void uat2esnt_convert_message(char __attribute__((unused)) *p,
                              char __attribute__((unused)) *end,
                              char __attribute__((unused)) *out,
                              char __attribute__((unused)) *out_end) { }

// ---- Include net_io.c to access static functions ----
#include "net_io.c"

// ---- test helpers ----

static int failures = 0;

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

#define ASSERT_STR_EQ(tag, got, expected) do { \
    if (strcmp((got), (expected)) != 0) { \
        fprintf(stderr, "%s: FAIL: got \"%s\", expected \"%s\"\n", tag, (got), (expected)); \
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

#define ASSERT_EQ_U32(tag, got, expected) do { \
    uint32_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %u (0x%x), expected %u (0x%x)\n", tag, _g, _g, _e, _e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_UINT8(tag, got, expected) do { \
    uint8_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %u, expected %u\n", tag, (unsigned)_g, (unsigned)_e); \
        failures++; \
    } \
} while(0)

// ---- testIeee754Binary32LeToFloat ----

static void testIeee754Binary32LeToFloat(void) {
    fprintf(stderr, "=== testIeee754Binary32LeToFloat ===\n");

    // +0.0: {00,00,00,00}
    {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee +0.0", result, 0.0, 0.0001);
    }

    // +1.0: {00,00,80,3F}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0x3F};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee +1.0", result, 1.0, 0.0001);
    }

    // -1.0: {00,00,80,BF}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0xBF};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee -1.0", result, -1.0, 0.0001);
    }

    // +inf: {00,00,80,7F}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0x7F};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee +inf", isinf(result) && result > 0);
    }

    // -inf: {00,00,80,FF}
    {
        uint8_t data[] = {0x00, 0x00, 0x80, 0xFF};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee -inf", isinf(result) && result < 0);
    }

    // NaN: {01,00,80,7F}
    {
        uint8_t data[] = {0x01, 0x00, 0x80, 0x7F};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee NaN", isnan(result));
    }

    // 3.14159: {DB,0F,49,40}
    {
        uint8_t data[] = {0xDB, 0x0F, 0x49, 0x40};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee pi", result, 3.14159, 0.001);
    }

    // Denormal: {01,00,00,00} - smallest positive denormal
    {
        uint8_t data[] = {0x01, 0x00, 0x00, 0x00};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_TRUE("ieee denormal > 0", result > 0.0);
        ASSERT_TRUE("ieee denormal tiny", result < 1e-38);
    }

    // -0.0: {00,00,00,80} - treated as +0.0
    {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x80};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee -0.0", result, 0.0, 0.0001);
    }

    // 100.0: {00,00,C8,42}
    {
        uint8_t data[] = {0x00, 0x00, 0xC8, 0x42};
        float result = ieee754_binary32_le_to_float(data);
        ASSERT_FLOAT_NEAR("ieee 100.0", result, 100.0, 0.001);
    }

    fprintf(stderr, "testIeee754Binary32LeToFloat: done\n\n");
}

// ---- testAirgroundEnumString ----

static void testAirgroundEnumString(void) {
    fprintf(stderr, "=== testAirgroundEnumString ===\n");

    ASSERT_STR_EQ("ag airborne", airground_enum_string(AG_AIRBORNE), "A+");
    ASSERT_STR_EQ("ag ground", airground_enum_string(AG_GROUND), "G+");
    ASSERT_STR_EQ("ag invalid", airground_enum_string(AG_INVALID), "?");
    ASSERT_STR_EQ("ag uncertain", airground_enum_string(AG_UNCERTAIN), "?");

    fprintf(stderr, "testAirgroundEnumString: done\n\n");
}

// ---- testHexDigitVal ----

static void testHexDigitVal(void) {
    fprintf(stderr, "=== testHexDigitVal ===\n");

    // '0'-'9' -> 0-9
    for (int i = 0; i <= 9; i++) {
        char tag[32];
        snprintf(tag, sizeof(tag), "hex digit '%c'", '0' + i);
        ASSERT_INT_EQ(tag, hexDigitVal('0' + i), i);
    }

    // 'A'-'F' -> 10-15
    for (int i = 0; i <= 5; i++) {
        char tag[32];
        snprintf(tag, sizeof(tag), "hex digit '%c'", 'A' + i);
        ASSERT_INT_EQ(tag, hexDigitVal('A' + i), 10 + i);
    }

    // 'a'-'f' -> 10-15
    for (int i = 0; i <= 5; i++) {
        char tag[32];
        snprintf(tag, sizeof(tag), "hex digit '%c'", 'a' + i);
        ASSERT_INT_EQ(tag, hexDigitVal('a' + i), 10 + i);
    }

    // Invalid chars -> -1
    ASSERT_INT_EQ("hex 'G' invalid", hexDigitVal('G'), -1);
    ASSERT_INT_EQ("hex 'g' invalid", hexDigitVal('g'), -1);
    ASSERT_INT_EQ("hex ' ' invalid", hexDigitVal(' '), -1);
    ASSERT_INT_EQ("hex '/' invalid", hexDigitVal('/'), -1);
    ASSERT_INT_EQ("hex ':' invalid", hexDigitVal(':'), -1);

    fprintf(stderr, "testHexDigitVal: done\n\n");
}

// ---- testPrintHexDigit ----

static void testPrintHexDigit(void) {
    fprintf(stderr, "=== testPrintHexDigit ===\n");

    char buf[3];

    // 0x00 -> "00"
    buf[2] = '\0';
    printHexDigit(buf, 0x00);
    ASSERT_STR_EQ("hex 0x00", buf, "00");

    // 0xFF -> "FF"
    buf[2] = '\0';
    printHexDigit(buf, 0xFF);
    ASSERT_STR_EQ("hex 0xFF", buf, "FF");

    // 0xAB -> "AB"
    buf[2] = '\0';
    printHexDigit(buf, 0xAB);
    ASSERT_STR_EQ("hex 0xAB", buf, "AB");

    // 0x1C -> "1C"
    buf[2] = '\0';
    printHexDigit(buf, 0x1C);
    ASSERT_STR_EQ("hex 0x1C", buf, "1C");

    // 0x0A -> "0A"
    buf[2] = '\0';
    printHexDigit(buf, 0x0A);
    ASSERT_STR_EQ("hex 0x0A", buf, "0A");

    fprintf(stderr, "testPrintHexDigit: done\n\n");
}

// ---- testNetTimestamp ----

static void testNetTimestamp(void) {
    fprintf(stderr, "=== testNetTimestamp ===\n");

    char buf[16];
    char *p;

    // No escaping: 0x112233445566
    {
        memset(buf, 0, sizeof(buf));
        p = netTimestamp(buf, 0x112233445566LL);
        ASSERT_INT_EQ("ts len no esc", (int)(p - buf), 6);
        ASSERT_TRUE("ts byte0", (unsigned char)buf[0] == 0x11);
        ASSERT_TRUE("ts byte1", (unsigned char)buf[1] == 0x22);
        ASSERT_TRUE("ts byte2", (unsigned char)buf[2] == 0x33);
        ASSERT_TRUE("ts byte3", (unsigned char)buf[3] == 0x44);
        ASSERT_TRUE("ts byte4", (unsigned char)buf[4] == 0x55);
        ASSERT_TRUE("ts byte5", (unsigned char)buf[5] == 0x66);
    }

    // With 0x1A in byte 1 position (bits 32-39): should be doubled
    {
        memset(buf, 0, sizeof(buf));
        p = netTimestamp(buf, 0x001A33445566LL);
        ASSERT_INT_EQ("ts len with esc", (int)(p - buf), 7);
        ASSERT_TRUE("ts esc byte0", (unsigned char)buf[0] == 0x00);
        ASSERT_TRUE("ts esc byte1", (unsigned char)buf[1] == 0x1A);
        ASSERT_TRUE("ts esc byte2", (unsigned char)buf[2] == 0x1A); // doubled
        ASSERT_TRUE("ts esc byte3", (unsigned char)buf[3] == 0x33);
    }

    // All zeros
    {
        memset(buf, 0xFF, sizeof(buf));
        p = netTimestamp(buf, 0);
        ASSERT_INT_EQ("ts len zeros", (int)(p - buf), 6);
        for (int i = 0; i < 6; i++) {
            ASSERT_TRUE("ts zero byte", (unsigned char)buf[i] == 0x00);
        }
    }

    fprintf(stderr, "testNetTimestamp: done\n\n");
}

// ---- testReadPing ----

static void testReadPing(void) {
    fprintf(stderr, "=== testReadPing ===\n");

    // Zero
    {
        char data[] = {0x00, 0x00, 0x00};
        ASSERT_EQ_U32("ping zero", readPing(data), 0);
    }

    // Max 24-bit
    {
        char data[] = {(char)0xFF, (char)0xFF, (char)0xFF};
        ASSERT_EQ_U32("ping max", readPing(data), 16777215);
    }

    // Known value 0x123456
    {
        char data[] = {0x12, 0x34, 0x56};
        ASSERT_EQ_U32("ping known", readPing(data), 0x123456);
    }

    fprintf(stderr, "testReadPing: done\n\n");
}

// ---- testReadPingEscaped ----

static void testReadPingEscaped(void) {
    fprintf(stderr, "=== testReadPingEscaped ===\n");

    // No escapes
    {
        char data[] = {0x12, 0x34, 0x56};
        ASSERT_EQ_U32("ping esc none", readPingEscaped(data), 0x123456);
    }

    // Escape after first byte: {0x12, 0x1A, 0x34, 0x56}
    // First byte: 0x12 -> res = 0x12 << 16
    // Skip 0x1A, second byte: 0x34 -> res += 0x34 << 8
    // Third byte: 0x56 -> res += 0x56
    {
        char data[] = {0x12, 0x1A, 0x34, 0x56};
        ASSERT_EQ_U32("ping esc mid", readPingEscaped(data), 0x123456);
    }

    // First byte is 0x1A: {0x1A, 0x1A, 0x34, 0x1A, 0x56}
    // First byte: 0x1A -> res = 0x1A << 16; skip next 0x1A
    // Second byte: 0x34 -> res += 0x34 << 8
    // Skip 0x1A, Third byte: 0x56 -> res += 0x56
    {
        char data[] = {0x1A, 0x1A, 0x34, 0x1A, 0x56};
        ASSERT_EQ_U32("ping esc multi", readPingEscaped(data), 0x1A3456);
    }

    fprintf(stderr, "testReadPingEscaped: done\n\n");
}

// ---- testCharToAis ----

static void testCharToAis(void) {
    fprintf(stderr, "=== testCharToAis ===\n");

    // '@' is index 0
    ASSERT_EQ_UINT8("ais @", char_to_ais('@'), 0);

    // 'A' is index 1
    ASSERT_EQ_UINT8("ais A", char_to_ais('A'), 1);

    // 'Z' is index 26
    ASSERT_EQ_UINT8("ais Z", char_to_ais('Z'), 26);

    // Space ' ' is index 32
    ASSERT_EQ_UINT8("ais space", char_to_ais(' '), 32);

    // '0' is index 48
    ASSERT_EQ_UINT8("ais 0", char_to_ais('0'), 48);

    // '?' is index 63 (last char)
    ASSERT_EQ_UINT8("ais ?", char_to_ais('?'), 63);

    // Unmapped: lowercase 'a' -> 32 (default)
    ASSERT_EQ_UINT8("ais lowercase", char_to_ais('a'), 32);

    // Null char -> 32 (explicit early return)
    ASSERT_EQ_UINT8("ais null", char_to_ais(0), 32);

    fprintf(stderr, "testCharToAis: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testIeee754Binary32LeToFloat();
    testAirgroundEnumString();
    testHexDigitVal();
    testPrintHexDigit();
    testNetTimestamp();
    testReadPing();
    testReadPingEscaped();
    testCharToAis();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
