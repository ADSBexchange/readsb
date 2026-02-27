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

// messageBuffer infrastructure for protocol parser tests
static struct modesMessage test_mm_storage[16];
static struct messageBuffer test_mb;

static void resetMessageBuffer(void) {
    memset(test_mm_storage, 0, sizeof(test_mm_storage));
    test_mb.msg = test_mm_storage;
    test_mb.len = 0;
    test_mb.alloc = 16;
    test_mb.id = 0;
    test_mb.activeClient = NULL;
}

static struct net_service test_service = { .descr = "test" };
static struct client test_client;

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

// ---- testBam32ToDouble ----

static void testBam32ToDouble(void) {
    fprintf(stderr, "=== testBam32ToDouble ===\n");

    // Zero -> 0.0
    {
        uint32_t bam = 0;
        double result = bam32ToDouble(bam);
        ASSERT_FLOAT_NEAR("bam zero", result, 0.0, 0.001);
    }

    // +90° BAM value -> ~90.0
    // bam32ToDouble does: (double)((int32_t)ntohl(bam) * 8.38190317153931E-08)
    // For 90°: ntohl(bam) should be (int32_t)(90.0 / 8.38190317153931E-08)
    // = 90.0 / 8.38190317153931E-08 = 1073741824 = 0x40000000
    // bam input needs to be htonl(0x40000000)
    {
        uint32_t bam = htonl((int32_t)(90.0 / 8.38190317153931E-08));
        double result = bam32ToDouble(bam);
        ASSERT_FLOAT_NEAR("bam +90", result, 90.0, 0.001);
    }

    // -90° BAM value -> ~-90.0
    {
        uint32_t bam = htonl((int32_t)(-90.0 / 8.38190317153931E-08));
        double result = bam32ToDouble(bam);
        ASSERT_FLOAT_NEAR("bam -90", result, -90.0, 0.001);
    }

    fprintf(stderr, "testBam32ToDouble: done\n\n");
}

#define ASSERT_EQ_U64(tag, got, expected) do { \
    uint64_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %llu (0x%llx), expected %llu (0x%llx)\n", tag, \
                (unsigned long long)_g, (unsigned long long)_g, \
                (unsigned long long)_e, (unsigned long long)_e); \
        failures++; \
    } \
} while(0)

#define ASSERT_EQ_I64(tag, got, expected) do { \
    int64_t _g = (got), _e = (expected); \
    if (_g != _e) { \
        fprintf(stderr, "%s: FAIL: got %lld, expected %lld\n", tag, \
                (long long)_g, (long long)_e); \
        failures++; \
    } \
} while(0)

// ---- testHexDumpString ----

static void testHexDumpString(void) {
    fprintf(stderr, "=== testHexDumpString ===\n");

    // Simple ASCII "Hi" -> "48 69 |Hi|"
    {
        char buf[256];
        memset(buf, 0, sizeof(buf));
        const char *result = hexDumpString("Hi", 2, buf, sizeof(buf));
        ASSERT_STR_EQ("hexdump Hi", result, "48 69 |Hi|");
    }

    // Non-printable bytes (0x01, 0x7F) -> dots in ASCII sidebar
    {
        char buf[256];
        memset(buf, 0, sizeof(buf));
        char input[] = {0x01, 0x7F};
        const char *result = hexDumpString(input, 2, buf, sizeof(buf));
        ASSERT_STR_EQ("hexdump nonprint", result, "01 7f |..|");
    }

    // Buffer too small (buflen such that max <= 0) -> empty string
    {
        char buf[4];
        memset(buf, 'X', sizeof(buf));
        const char *result = hexDumpString("Hi", 2, buf, 4);
        // max = 4/4 - 4 = -3, which is <= 0, so buf[0] = 0
        ASSERT_STR_EQ("hexdump small", result, "");
    }

    fprintf(stderr, "testHexDumpString: done\n\n");
}

// ---- testReadFspec ----

static void testReadFspec(void) {
    fprintf(stderr, "=== testReadFspec ===\n");

    // Single byte, no continuation (bit 0 = 0)
    {
        char data[] = {(char)0x80};
        char *p = data;
        uint8_t *fspec = readFspec(&p);
        ASSERT_EQ_UINT8("fspec single byte0", fspec[0], 0x80);
        ASSERT_EQ_UINT8("fspec single byte1", fspec[1], 0);
        ASSERT_INT_EQ("fspec single advance", (int)(p - data), 1);
        free(fspec);
    }

    // Two bytes (byte0 bit0=1 → continuation, byte1 bit0=0 → stop)
    {
        char data[] = {(char)0x81, (char)0x40};
        char *p = data;
        uint8_t *fspec = readFspec(&p);
        ASSERT_EQ_UINT8("fspec two byte0", fspec[0], 0x81);
        ASSERT_EQ_UINT8("fspec two byte1", fspec[1], 0x40);
        ASSERT_INT_EQ("fspec two advance", (int)(p - data), 2);
        free(fspec);
    }

    // Three bytes (byte0 bit0=1, byte1 bit0=1, byte2 bit0=0)
    {
        char data[] = {(char)0x03, (char)0x05, (char)0x80};
        char *p = data;
        uint8_t *fspec = readFspec(&p);
        ASSERT_EQ_UINT8("fspec three byte0", fspec[0], 0x03);
        ASSERT_EQ_UINT8("fspec three byte1", fspec[1], 0x05);
        ASSERT_EQ_UINT8("fspec three byte2", fspec[2], 0x80);
        ASSERT_INT_EQ("fspec three advance", (int)(p - data), 3);
        free(fspec);
    }

    fprintf(stderr, "testReadFspec: done\n\n");
}

// ---- testReadAsterixTime ----

static void testReadAsterixTime(void) {
    fprintf(stderr, "=== testReadAsterixTime ===\n");

    // mstime() returns 1700000000000LL
    // midnight = (1700000000000 / 86400000) * 86400000 = 1699920000000
    // current offset = 1700000000000 - 1699920000000 = 80000000 ms
    int64_t midnight = 1699920000000LL;

    // Encode ~80000 seconds = 80000000 ms
    // rawtime = mssm * 0.128 = 80000000 * 0.128 = 10240000
    // 10240000 = 0x9C4000 → bytes {0x9C, 0x40, 0x00}
    {
        char data[] = {(char)0x9C, (char)0x40, (char)0x00};
        char *p = data;
        uint64_t result = readAsterixTime(&p);
        // mssm = 10240000 / 0.128 = 80000000
        ASSERT_EQ_U64("asterix time current", result, (uint64_t)(midnight + 80000000));
        ASSERT_INT_EQ("asterix time advance", (int)(p - data), 3);
    }

    // Midnight (zero)
    {
        char data[] = {0x00, 0x00, 0x00};
        char *p = data;
        uint64_t result = readAsterixTime(&p);
        // rawtime=0, mssm=0, diff = midnight + 0 - mstime() = -80000000
        // abs(-80000000) < 43200000? No, 80000000 > 43200000 → return midnight - 86400000 + 0
        // That means previous day midnight
        ASSERT_EQ_U64("asterix time zero", result, (uint64_t)(midnight - 86400000));
    }

    // Encode ~80001 seconds = 80001000 ms (slightly after current time)
    // rawtime = 80001000 * 0.128 = 10240128 = 0x9C4080
    {
        char data[] = {(char)0x9C, (char)0x40, (char)0x80};
        char *p = data;
        uint64_t result = readAsterixTime(&p);
        int rawtime = (0x9C << 16) + (0x40 << 8) + 0x80; // 10240128
        int mssm = (int)(rawtime / .128);
        ASSERT_EQ_U64("asterix time near", result, (uint64_t)(midnight + mssm));
    }

    fprintf(stderr, "testReadAsterixTime: done\n\n");
}

// ---- testReadAsterixHighPrecisionTime ----

static void testReadAsterixHighPrecisionTime(void) {
    fprintf(stderr, "=== testReadAsterixHighPrecisionTime ===\n");

    // FSI=0, zero offset: timestamp 5500 → wholesecond=5000, result=5000
    {
        // FSI=0b00, offset=0 → byte0=0x00, bytes 1-3=0x00
        char data[] = {0x00, 0x00, 0x00, 0x00};
        char *p = data;
        uint64_t ts = 5500;
        readAsterixHighPrecisionTime(&ts, &p);
        ASSERT_EQ_U64("hpt fsi0 zero", ts, 5000);
        ASSERT_INT_EQ("hpt fsi0 advance", (int)(p - data), 4);
    }

    // FSI=1 (+1 to wholesecond), zero offset
    // Code does: wholesecond += 1 (adds 1 unit, not 1 second)
    // wholesecond=5000 → 5001, result=5001
    {
        char data[] = {0x40, 0x00, 0x00, 0x00};
        char *p = data;
        uint64_t ts = 5500;
        readAsterixHighPrecisionTime(&ts, &p);
        ASSERT_EQ_U64("hpt fsi1 zero", ts, 5001);
    }

    // FSI=2 (-1 from wholesecond), with non-zero offset
    // wholesecond=5000 → 4999
    // offset raw = 0x04000000 (2^26), offset = 2^26 * 2^-27 = 0.5
    // result = 4999 + 0 = 4999 (offset 0.5 truncated in uint64_t addition)
    {
        char data[] = {(char)0x84, 0x00, 0x00, 0x00};
        char *p = data;
        uint64_t ts = 5500;
        readAsterixHighPrecisionTime(&ts, &p);
        ASSERT_EQ_U64("hpt fsi2 half", ts, 4999);
    }

    fprintf(stderr, "testReadAsterixHighPrecisionTime: done\n\n");
}

// ---- testGetNextPfUnstuffedByte ----

static void testGetNextPfUnstuffedByte(void) {
    fprintf(stderr, "=== testGetNextPfUnstuffedByte ===\n");

    // Normal byte (no DLE)
    {
        char data[] = {0x42, 0x00};
        char *p = data;
        unsigned char result = getNextPfUnstuffedByte(&p);
        ASSERT_EQ_UINT8("pf normal", result, 0x42);
        ASSERT_INT_EQ("pf normal advance", (int)(p - data), 1);
    }

    // DLE-escaped byte: {0x10, 0x10} → returns 0x10, advances 2
    {
        char data[] = {0x10, 0x10, 0x00};
        char *p = data;
        unsigned char result = getNextPfUnstuffedByte(&p);
        ASSERT_EQ_UINT8("pf dle esc", result, 0x10);
        ASSERT_INT_EQ("pf dle advance", (int)(p - data), 2);
    }

    // DLE before normal byte: {0x10, 0x42} → returns 0x42, advances 2
    {
        char data[] = {0x10, 0x42, 0x00};
        char *p = data;
        unsigned char result = getNextPfUnstuffedByte(&p);
        ASSERT_EQ_UINT8("pf dle normal", result, 0x42);
        ASSERT_INT_EQ("pf dle normal advance", (int)(p - data), 2);
    }

    // Sequential calls: {0x41, 0x10, 0x43}
    {
        char data[] = {0x41, 0x10, 0x43, 0x00};
        char *p = data;
        unsigned char r1 = getNextPfUnstuffedByte(&p);
        ASSERT_EQ_UINT8("pf seq first", r1, 0x41);
        unsigned char r2 = getNextPfUnstuffedByte(&p);
        ASSERT_EQ_UINT8("pf seq second", r2, 0x43);
    }

    fprintf(stderr, "testGetNextPfUnstuffedByte: done\n\n");
}

// ---- testDecodeSbsLine ----

static void testDecodeSbsLine(void) {
    fprintf(stderr, "=== testDecodeSbsLine ===\n");

    int64_t now = mstime();
    memset(&test_client, 0, sizeof(test_client));
    test_client.service = &test_service;

    // Valid MSG,3 with position and altitude
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        char line[] = "MSG,3,1,1,4AC8B3,1,2019/12/10,19:10:46.320,2019/12/10,19:10:47.789,,36017,,,51.1001,10.1915,,,,,,";
        int ret = decodeSbsLine(&test_client, line, 0, now, &test_mb);
        ASSERT_INT_EQ("sbs msg3 ret", ret, 0);
        struct modesMessage *mm = &test_mm_storage[0];
        ASSERT_EQ_U32("sbs msg3 addr", mm->addr, 0x4AC8B3);
        ASSERT_INT_EQ("sbs msg3 baro_alt", mm->baro_alt, 36017);
        ASSERT_TRUE("sbs msg3 baro_valid", mm->baro_alt_valid == 1);
        ASSERT_FLOAT_NEAR("sbs msg3 lat", mm->decoded_lat, 51.1001, 0.001);
        ASSERT_FLOAT_NEAR("sbs msg3 lon", mm->decoded_lon, 10.1915, 0.001);
        ASSERT_TRUE("sbs msg3 pos_valid", mm->sbs_pos_valid == 1);
        ASSERT_TRUE("sbs msg3 sbs_in", mm->sbs_in == 1);
    }

    // Valid MSG,1 with callsign
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        char line[] = "MSG,1,1,1,A12345,1,2019/12/10,19:10:46.320,2019/12/10,19:10:47.789,UAL123  ,,,,,,,,,,,";
        decodeSbsLine(&test_client, line, 0, now, &test_mb);
        struct modesMessage *mm = &test_mm_storage[0];
        ASSERT_EQ_U32("sbs msg1 addr", mm->addr, 0xA12345);
        ASSERT_TRUE("sbs msg1 call_valid", mm->callsign_valid == 1);
        ASSERT_TRUE("sbs msg1 callsign", strncmp(mm->callsign, "UAL123", 6) == 0);
    }

    // Heartbeat (too short — line_len < 2)
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        char line[] = "\n";
        int ret = decodeSbsLine(&test_client, line, 0, now, &test_mb);
        ASSERT_INT_EQ("sbs heartbeat ret", ret, 0);
        // No message consumed
        ASSERT_INT_EQ("sbs heartbeat mb_len", test_mb.len, 0);
    }

    // Invalid (missing fields) — too short
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        char line[] = "MSG,3,1";
        decodeSbsLine(&test_client, line, 0, now, &test_mb);
        ASSERT_TRUE("sbs invalid stats", Modes.stats_current.remote_received_basestation_invalid > 0);
    }

    // UAV with $ prefix
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        Modes.enable_uav = 1;
        char line[] = "MSG,3,1,1,$000001,1,2019/12/10,19:10:46.320,2019/12/10,19:10:47.789,,1000,,,40.0,-74.0,,,,,,";
        decodeSbsLine(&test_client, line, 0, now, &test_mb);
        struct modesMessage *mm = &test_mm_storage[0];
        ASSERT_TRUE("sbs uav non_icao", (mm->addr & MODES_NON_ICAO_ADDRESS) != 0);
        ASSERT_TRUE("sbs uav uav_addr", (mm->addr & MODES_UAV_ADDRESS) != 0);
        ASSERT_INT_EQ("sbs uav addrtype", mm->addrtype, ADDR_UAV);
        ASSERT_EQ_UINT8("sbs uav category", mm->category, 0xB6);
        ASSERT_TRUE("sbs uav cat_valid", mm->category_valid == 1);
    }

    fprintf(stderr, "testDecodeSbsLine: done\n\n");
}

// ---- testDecodeHexMessage ----

static void testDecodeHexMessage(void) {
    fprintf(stderr, "=== testDecodeHexMessage ===\n");

    int64_t now = mstime();
    memset(&test_client, 0, sizeof(test_client));
    test_client.service = &test_service;

    // *-AVR raw (14-byte Mode-S long message)
    {
        memset(&Modes, 0, sizeof(Modes));
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        char hex[] = "*8D4B969699155600E87406F5B69F;";
        int ret = decodeHexMessage(&test_client, hex, now, &mm);
        ASSERT_INT_EQ("hex star ret", ret, 1);
        ASSERT_EQ_UINT8("hex star msg0", mm.msg[0], 0x8D);
        ASSERT_EQ_UINT8("hex star msg1", mm.msg[1], 0x4B);
        ASSERT_EQ_I64("hex star sysTs", mm.sysTimestamp, now);
    }

    // @-AVR with 12-hex-digit timestamp
    {
        memset(&Modes, 0, sizeof(Modes));
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        // @<12 hex ts><14 hex msg>;
        // timestamp = 03BA2A7C1DD1, msg = 5D4CA7F9A0B84B (7 bytes = short)
        char hex[] = "@03BA2A7C1DD15D4CA7F9A0B84B;";
        int ret = decodeHexMessage(&test_client, hex, now, &mm);
        ASSERT_INT_EQ("hex at ret", ret, 1);
        ASSERT_EQ_U64("hex at timestamp", mm.timestamp, 0x03BA2A7C1DD1ULL);
    }

    // Missing semicolon → returns 0
    {
        memset(&Modes, 0, sizeof(Modes));
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        char hex[] = "*8D4B969699155600E87406F5B69F";
        int ret = decodeHexMessage(&test_client, hex, now, &mm);
        ASSERT_INT_EQ("hex no semi", ret, 0);
    }

    // Too short → returns 0
    {
        memset(&Modes, 0, sizeof(Modes));
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        char hex[] = "*AB;";
        int ret = decodeHexMessage(&test_client, hex, now, &mm);
        ASSERT_INT_EQ("hex too short", ret, 0);
    }

    // Leading/trailing whitespace stripped
    {
        memset(&Modes, 0, sizeof(Modes));
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));
        char hex[] = "  *8D4B969699155600E87406F5B69F;  ";
        int ret = decodeHexMessage(&test_client, hex, now, &mm);
        ASSERT_INT_EQ("hex whitespace", ret, 1);
    }

    fprintf(stderr, "testDecodeHexMessage: done\n\n");
}

// ---- testDecodeBinMessage ----

static void testDecodeBinMessage(void) {
    fprintf(stderr, "=== testDecodeBinMessage ===\n");

    int64_t now = mstime();
    memset(&test_client, 0, sizeof(test_client));
    test_client.service = &test_service;

    // Type '3' (Mode-S long, 14 bytes): type + 6 ts + 1 signal + 14 msg
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        char buf[32];
        memset(buf, 0, sizeof(buf));
        buf[0] = '3'; // type
        // 6-byte timestamp: 0x000102030405
        buf[1] = 0x00; buf[2] = 0x01; buf[3] = 0x02;
        buf[4] = 0x03; buf[5] = 0x04; buf[6] = 0x05;
        buf[7] = (char)0x80; // signal level = 128
        // 14 bytes of message data (0x8D followed by zeros)
        buf[8] = (char)0x8D;
        int ret = decodeBinMessage(&test_client, buf, 1, now, &test_mb);
        ASSERT_INT_EQ("bin type3 ret", ret, 0);
        struct modesMessage *mm = &test_mm_storage[0];
        ASSERT_EQ_U64("bin type3 ts", mm->timestamp, 0x000102030405ULL);
        // signalLevel = (128/255.0)^2 ≈ 0.2519
        ASSERT_FLOAT_NEAR("bin type3 signal", mm->signalLevel, (128.0/255.0)*(128.0/255.0), 0.001);
        ASSERT_EQ_UINT8("bin type3 msg0", mm->msg[0], 0x8D);
    }

    // Type '2' (Mode-S short, 7 bytes)
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        char buf[32];
        memset(buf, 0, sizeof(buf));
        buf[0] = '2'; // type
        buf[1] = 0x00; buf[2] = 0x00; buf[3] = 0x00;
        buf[4] = 0x00; buf[5] = 0x00; buf[6] = 0x00; // ts=0
        buf[7] = (char)0xFF; // signal = 255 → max
        buf[8] = (char)0x5D; // first msg byte
        int ret = decodeBinMessage(&test_client, buf, 1, now, &test_mb);
        ASSERT_INT_EQ("bin type2 ret", ret, 0);
        struct modesMessage *mm = &test_mm_storage[0];
        ASSERT_EQ_UINT8("bin type2 msg0", mm->msg[0], 0x5D);
        // signal = (255/255)^2 = 1.0
        ASSERT_FLOAT_NEAR("bin type2 signal", mm->signalLevel, 1.0, 0.001);
    }

    // Type '1' (Mode-A/C) with mode_ac disabled → discard
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        Modes.mode_ac = 0;
        char buf[16];
        memset(buf, 0, sizeof(buf));
        buf[0] = '1';
        int ret = decodeBinMessage(&test_client, buf, 1, now, &test_mb);
        ASSERT_INT_EQ("bin type1 disabled", ret, 0);
        // No message consumed
        ASSERT_INT_EQ("bin type1 mb_len", test_mb.len, 0);
    }

    // Type '5' (Radarcape position) — 21 bytes of position data
    {
        resetMessageBuffer();
        memset(&Modes, 0, sizeof(Modes));
        char buf[32];
        memset(buf, 0, sizeof(buf));
        buf[0] = '5';
        // IEEE754 LE float values at offsets 4, 8, 12 within the 21-byte payload
        // buf[1..21] = 21 bytes, position data starts at buf[1]
        // lat at buf[1+4]=buf[5], lon at buf[1+8]=buf[9], alt at buf[1+12]=buf[13]
        // 51.5f LE = {0x00, 0x00, 0x4E, 0x42}
        float lat = 51.5f, lon = -0.1f, alt = 30.0f;
        memcpy(&buf[5], &lat, 4);
        memcpy(&buf[9], &lon, 4);
        memcpy(&buf[13], &alt, 4);
        int ret = decodeBinMessage(&test_client, buf, 1, now, &test_mb);
        ASSERT_INT_EQ("bin type5 ret", ret, 0);
        ASSERT_FLOAT_NEAR("bin type5 lat", Modes.fUserLat, 51.5, 0.1);
        ASSERT_FLOAT_NEAR("bin type5 lon", Modes.fUserLon, -0.1, 0.1);
    }

    fprintf(stderr, "testDecodeBinMessage: done\n\n");
}

// ---- testHandleGpsd ----

static void testHandleGpsd(void) {
    fprintf(stderr, "=== testHandleGpsd ===\n");

    int64_t now = mstime();
    memset(&test_client, 0, sizeof(test_client));
    test_client.service = &test_service;

    // Valid lat/lon/alt
    {
        memset(&Modes, 0, sizeof(Modes));
        char json[] = "{\"class\":\"TPV\",\"lat\":51.5,\"lon\":-0.1,\"alt\":30.0}";
        handle_gpsd(&test_client, json, 0, now, &test_mb);
        ASSERT_FLOAT_NEAR("gpsd valid lat", Modes.fUserLat, 51.5, 0.001);
        ASSERT_FLOAT_NEAR("gpsd valid lon", Modes.fUserLon, -0.1, 0.001);
        ASSERT_FLOAT_NEAR("gpsd valid alt", Modes.fUserAlt, 30.0, 0.001);
        ASSERT_TRUE("gpsd valid location", Modes.userLocationValid == 1);
    }

    // Missing lat → no location update
    {
        memset(&Modes, 0, sizeof(Modes));
        char json[] = "{\"class\":\"TPV\",\"lon\":-0.1}";
        handle_gpsd(&test_client, json, 0, now, &test_mb);
        ASSERT_TRUE("gpsd no lat", Modes.userLocationValid == 0);
    }

    // Implausible lat (>89.9) → rejected
    {
        memset(&Modes, 0, sizeof(Modes));
        char json[] = "{\"class\":\"TPV\",\"lat\":91.0,\"lon\":0.0}";
        handle_gpsd(&test_client, json, 0, now, &test_mb);
        ASSERT_TRUE("gpsd implausible", Modes.userLocationValid == 0);
    }

    // Near-zero rejection (lat<0.1 && lon<0.1)
    {
        memset(&Modes, 0, sizeof(Modes));
        char json[] = "{\"class\":\"TPV\",\"lat\":0.01,\"lon\":0.01}";
        handle_gpsd(&test_client, json, 0, now, &test_mb);
        ASSERT_TRUE("gpsd near zero", Modes.userLocationValid == 0);
    }

    fprintf(stderr, "testHandleGpsd: done\n\n");
}

// ---- testHandleCommandSocket ----

static void testHandleCommandSocket(void) {
    fprintf(stderr, "=== testHandleCommandSocket ===\n");

    int64_t now = mstime();
    memset(&test_client, 0, sizeof(test_client));
    test_client.service = &test_service;

    // Valid deleteTrace command
    {
        memset(&Modes, 0, sizeof(Modes));
        char cmd[] = "deleteTrace 4AC8B3 1000 2000";
        handleCommandSocket(&test_client, cmd, 0, now, &test_mb);
        ASSERT_TRUE("cmd delete not null", Modes.deleteTrace != NULL);
        ASSERT_EQ_U32("cmd delete hex", Modes.deleteTrace->hex, 0x4AC8B3);
        ASSERT_EQ_I64("cmd delete from", Modes.deleteTrace->from, 1000);
        ASSERT_EQ_I64("cmd delete to", Modes.deleteTrace->to, 2000);
        // cleanup
        free(Modes.deleteTrace);
        Modes.deleteTrace = NULL;
    }

    // Too few tokens → no change
    {
        memset(&Modes, 0, sizeof(Modes));
        char cmd[] = "deleteTrace 4AC8B3";
        handleCommandSocket(&test_client, cmd, 0, now, &test_mb);
        ASSERT_TRUE("cmd too few", Modes.deleteTrace == NULL);
    }

    // Unknown command → no change
    {
        memset(&Modes, 0, sizeof(Modes));
        char cmd[] = "unknownCmd arg1";
        handleCommandSocket(&test_client, cmd, 0, now, &test_mb);
        ASSERT_TRUE("cmd unknown", Modes.deleteTrace == NULL);
    }

    fprintf(stderr, "testHandleCommandSocket: done\n\n");
}

// ---- testHandleBeastCommand ----

static void testHandleBeastCommand(void) {
    fprintf(stderr, "=== testHandleBeastCommand ===\n");

    int64_t now = mstime();
    memset(&test_client, 0, sizeof(test_client));
    test_client.service = &test_service;

    // Ping command: 'P' + 3-byte ping value
    {
        memset(&Modes, 0, sizeof(Modes));
        test_client.ping = 0;
        test_client.pingReceived = 0;
        test_client.pingEnabled = 0;
        char cmd[] = {'P', 0x12, 0x34, 0x56, 0x00};
        handleBeastCommand(&test_client, cmd, 0, now, &test_mb);
        ASSERT_EQ_U32("beast ping val", test_client.ping, 0x123456);
        ASSERT_EQ_I64("beast ping recv", test_client.pingReceived, now);
        ASSERT_TRUE("beast ping enabled", test_client.pingEnabled == 1);
    }

    // Enable Mode-AC: "1J"
    {
        test_client.modeac_requested = 0;
        char cmd[] = "1J";
        handleBeastCommand(&test_client, cmd, 0, now, &test_mb);
        ASSERT_TRUE("beast modeac on", test_client.modeac_requested == 1);
    }

    // Disable Mode-AC: "1j"
    {
        test_client.modeac_requested = 1;
        char cmd[] = "1j";
        handleBeastCommand(&test_client, cmd, 0, now, &test_mb);
        ASSERT_TRUE("beast modeac off", test_client.modeac_requested == 0);
    }

    // Reduce rate: "WS"
    {
        memset(&Modes, 0, sizeof(Modes));
        char cmd[] = "WS";
        handleBeastCommand(&test_client, cmd, 0, now, &test_mb);
        ASSERT_EQ_I64("beast reduce", Modes.doubleBeastReduceIntervalUntil, now + PING_REDUCE_DURATION);
    }

    fprintf(stderr, "testHandleBeastCommand: done\n\n");
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
    testBam32ToDouble();
    testHexDumpString();
    testReadFspec();
    testReadAsterixTime();
    testReadAsterixHighPrecisionTime();
    testGetNextPfUnstuffedByte();
    testDecodeSbsLine();
    testDecodeHexMessage();
    testDecodeBinMessage();
    testHandleGpsd();
    testHandleCommandSocket();
    testHandleBeastCommand();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
