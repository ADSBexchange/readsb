// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// comm_b_tests.c - unit tests for Comm-B BDS decoder functions
//
// We #include comm_b.c directly to access its static functions.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "readsb.h"
#include "ais_charset.h"

// Linker stubs
struct _Modes Modes;
void setExit(int __attribute__((unused)) arg) { }

// comm_b.c calls icaoFilterTest from checkAcasRaValid
int icaoFilterTest(uint32_t __attribute__((unused)) addr) { return 0; }
void icaoFilterAdd(uint32_t __attribute__((unused)) addr) { }
uint32_t icaoFilterTestFuzzy(uint32_t __attribute__((unused)) partial) { return 0; }

uint32_t modeAC_count[4096];
uint32_t modeAC_match[4096];

// Stubs for displayModesMessage dependencies (never called in tests)
char *sprint_uuid1(uint64_t __attribute__((unused)) id1, char *p) { return p; }
void printACASInfoShort(uint32_t __attribute__((unused)) addr,
                        unsigned char __attribute__((unused)) *MV,
                        struct aircraft __attribute__((unused)) *a,
                        struct modesMessage __attribute__((unused)) *mm,
                        int64_t __attribute__((unused)) now) { }

// Pull in comm_b.c to access static functions
#include "comm_b.c"

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

#define ASSERT_STR_EQ(tag, got, expected) do { \
    if (strcmp((got), (expected)) != 0) { \
        fprintf(stderr, "%s: FAIL: got \"%s\", expected \"%s\"\n", tag, (got), (expected)); \
        failures++; \
    } \
} while(0)

#define ASSERT_FLOAT_NEAR(tag, got, expected, tol) do { \
    float _g = (got), _e = (expected), _t = (tol); \
    if (fabsf(_g - _e) > _t) { \
        fprintf(stderr, "%s: FAIL: got %.4f, expected %.4f (tol %.4f)\n", \
                tag, (double)_g, (double)_e, (double)_t); \
        failures++; \
    } \
} while(0)

// Helper: set a single bit (1-indexed) in a 7-byte MB field
static void setbit(unsigned char *msg, unsigned bitnum) {
    unsigned byte_idx = (bitnum - 1) / 8;
    unsigned bit_in_byte = 7 - ((bitnum - 1) % 8);
    msg[byte_idx] |= (1 << bit_in_byte);
}

// Helper: set a multi-bit value (1-indexed, MSB-first) in a 7-byte MB field
static void setbits_mb(unsigned char *msg, unsigned firstbit, unsigned lastbit, unsigned value) {
    unsigned width = lastbit - firstbit + 1;
    for (unsigned i = 0; i < width; i++) {
        unsigned bitnum = lastbit - i; // from LSB to MSB
        if (value & (1U << i)) {
            setbit(msg, bitnum);
        }
    }
}

// Helper: create a zeroed modesMessage
static struct modesMessage make_mm(void) {
    struct modesMessage mm;
    memset(&mm, 0, sizeof(mm));
    return mm;
}

// ---- testDecodeEmptyResponse ----

static void testDecodeEmptyResponse(void) {
    fprintf(stderr, "=== testDecodeEmptyResponse ===\n");

    // All-zero MB -> score 56
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        int score = decodeEmptyResponse(&mm, false);
        ASSERT_EQ_INT("empty score", score, 56);
    }

    // Store sets COMMB_EMPTY_RESPONSE
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        decodeEmptyResponse(&mm, true);
        ASSERT_EQ_INT("empty store", mm.commb_format, COMMB_EMPTY_RESPONSE);
    }

    // Any non-zero byte -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[3] = 0x01;
        int score = decodeEmptyResponse(&mm, false);
        ASSERT_EQ_INT("non-empty score", score, 0);
    }

    // First byte non-zero
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0xFF;
        int score = decodeEmptyResponse(&mm, false);
        ASSERT_EQ_INT("first byte nonzero", score, 0);
    }

    // Last byte non-zero
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[6] = 0x01;
        int score = decodeEmptyResponse(&mm, false);
        ASSERT_EQ_INT("last byte nonzero", score, 0);
    }

    fprintf(stderr, "testDecodeEmptyResponse: done\n\n");
}

// ---- testDecodeBDS10 ----

static void testDecodeBDS10(void) {
    fprintf(stderr, "=== testDecodeBDS10 ===\n");

    // MB[0]=0x10, reserved bits 10-14 zero -> score 56
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x10;
        int score = decodeBDS10(&mm, false);
        ASSERT_EQ_INT("BDS10 valid score", score, 56);
    }

    // Store sets COMMB_DATALINK_CAPS
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x10;
        decodeBDS10(&mm, true);
        ASSERT_EQ_INT("BDS10 store", mm.commb_format, COMMB_DATALINK_CAPS);
    }

    // MB[0] != 0x10 -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20;
        int score = decodeBDS10(&mm, false);
        ASSERT_EQ_INT("BDS10 wrong id", score, 0);
    }

    // Reserved bits non-zero -> score 0
    // Bits 10-14 are in byte 1 bits [6:2] (1-indexed bit 10 = byte1 bit7, etc.)
    // bit 10 is the 2nd bit of byte 1 (0-indexed), which is 0x40
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x10;
        mm.MB[1] = 0x40; // set bit 10 (reserved)
        int score = decodeBDS10(&mm, false);
        ASSERT_EQ_INT("BDS10 reserved bits", score, 0);
    }

    fprintf(stderr, "testDecodeBDS10: done\n\n");
}

// ---- testDecodeBDS20 ----

static void testDecodeBDS20(void) {
    fprintf(stderr, "=== testDecodeBDS20 ===\n");

    // Encode callsign "TEST1234" into MB payload
    // ais_charset = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_ !\"#$%&'()*+,-./0123456789:;<=>?"
    // 'T'=20, 'E'=5, 'S'=19, 'T'=20, '1'=49, '2'=50, '3'=51, '4'=52
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20; // BDS identifier

        unsigned char indices[8] = {20, 5, 19, 20, 49, 50, 51, 52};
        for (int i = 0; i < 8; i++) {
            setbits_mb(mm.MB, 9 + i * 6, 14 + i * 6, indices[i]);
        }

        int score = decodeBDS20(&mm, false);
        ASSERT_TRUE("BDS20 TEST1234 score > 0", score > 0);
        ASSERT_EQ_INT("BDS20 TEST1234 score", score, 8 + 8 * 6); // 56

        // Verify store populates callsign correctly
        decodeBDS20(&mm, true);
        ASSERT_STR_EQ("BDS20 TEST1234 callsign", mm.callsign, "TEST1234");
        ASSERT_EQ_INT("BDS20 callsign_valid", mm.callsign_valid, 1);
        ASSERT_EQ_INT("BDS20 commb_format", mm.commb_format, COMMB_AIRCRAFT_IDENT);
    }

    // All-spaces callsign -> valid (score > 0)
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20;

        // Space is at index 32 in ais_charset
        for (int i = 0; i < 8; i++) {
            setbits_mb(mm.MB, 9 + i * 6, 14 + i * 6, 32);
        }

        int score = decodeBDS20(&mm, false);
        ASSERT_TRUE("BDS20 all-spaces score > 0", score > 0);
    }

    // Wrong BDS identifier -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x10; // not 0x20
        int score = decodeBDS20(&mm, false);
        ASSERT_EQ_INT("BDS20 wrong id", score, 0);
    }

    fprintf(stderr, "testDecodeBDS20: done\n\n");
}

// ---- testDecodeBDS40 ----

static void testDecodeBDS40(void) {
    fprintf(stderr, "=== testDecodeBDS40 ===\n");

    // Construct valid BDS40: MCP 35008ft, FMS 35008ft, QNH 1013.2mb
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);

        setbit(mm.MB, 1);                  // mcp_valid
        setbits_mb(mm.MB, 2, 13, 2188);    // mcp_raw (35008/16)
        setbit(mm.MB, 14);                 // fms_valid
        setbits_mb(mm.MB, 15, 26, 2188);   // fms_raw
        setbit(mm.MB, 27);                 // baro_valid
        setbits_mb(mm.MB, 28, 39, 2132);   // baro_raw ((1013.2-800)/0.1)
        // bits 40-47: reserved_1 = 0
        setbit(mm.MB, 48);                 // mode_valid
        setbits_mb(mm.MB, 49, 51, 7);      // mode_raw (all modes)
        // bits 52-53: reserved_2 = 0
        setbit(mm.MB, 54);                 // source_valid
        setbits_mb(mm.MB, 55, 56, 1);      // source_raw

        int score = decodeBDS40(&mm, false);
        ASSERT_TRUE("BDS40 valid score > 0", score > 0);

        // Store and check decoded values
        struct modesMessage mm2 = make_mm();
        memcpy(mm2.MB, mm.MB, 7);
        decodeBDS40(&mm2, true);
        ASSERT_EQ_INT("BDS40 commb_format", mm2.commb_format, COMMB_VERTICAL_INTENT);
        ASSERT_EQ_INT("BDS40 mcp_altitude", mm2.nav.mcp_altitude, 2188 * 16);
        ASSERT_EQ_INT("BDS40 fms_altitude", mm2.nav.fms_altitude, 2188 * 16);
        ASSERT_EQ_INT("BDS40 mcp_altitude_valid", mm2.nav.mcp_altitude_valid, 1);
        ASSERT_EQ_INT("BDS40 fms_altitude_valid", mm2.nav.fms_altitude_valid, 1);
        ASSERT_EQ_INT("BDS40 qnh_valid", mm2.nav.qnh_valid, 1);
        ASSERT_FLOAT_NEAR("BDS40 qnh", mm2.nav.qnh, 800 + 2132 * 0.1f, 0.2f);
    }

    // All valid bits clear -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        int score = decodeBDS40(&mm, false);
        ASSERT_EQ_INT("BDS40 all-invalid score", score, 0);
    }

    fprintf(stderr, "testDecodeBDS40: done\n\n");
}

// ---- testDecodeBDS50 ----

static void testDecodeBDS50(void) {
    fprintf(stderr, "=== testDecodeBDS50 ===\n");

    // Construct valid BDS50: roll=+10°, track=90°, gs=450kt, track_rate=0, tas=460kt
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);

        setbit(mm.MB, 1);                  // roll_valid
        // bit 2: roll_sign=0 (positive)
        setbits_mb(mm.MB, 3, 11, 57);      // roll_raw (10/45*256 ≈ 57)
        setbit(mm.MB, 12);                 // track_valid
        // bit 13: track_sign=0
        setbits_mb(mm.MB, 14, 23, 512);    // track_raw (90/90*512 = 512)
        setbit(mm.MB, 24);                 // gs_valid
        setbits_mb(mm.MB, 25, 34, 225);    // gs_raw (450/2 = 225)
        setbit(mm.MB, 35);                 // track_rate_valid
        // bit 36: track_rate_sign=0
        // bits 37-45: track_rate_raw=0
        setbit(mm.MB, 46);                 // tas_valid
        setbits_mb(mm.MB, 47, 56, 230);    // tas_raw (460/2 = 230)

        int score = decodeBDS50(&mm, false);
        ASSERT_TRUE("BDS50 valid score > 0", score > 0);

        // Store and verify
        struct modesMessage mm2 = make_mm();
        memcpy(mm2.MB, mm.MB, 7);
        decodeBDS50(&mm2, true);
        ASSERT_EQ_INT("BDS50 commb_format", mm2.commb_format, COMMB_TRACK_TURN);
        ASSERT_FLOAT_NEAR("BDS50 roll", mm2.roll, 57 * 45.0f / 256.0f, 0.5f);
        ASSERT_FLOAT_NEAR("BDS50 heading", mm2.heading, 512 * 90.0f / 512.0f, 0.5f);
        ASSERT_EQ_INT("BDS50 gs", mm2.gs.v0, 450);
        ASSERT_EQ_INT("BDS50 tas", mm2.tas, 460);
    }

    // Missing required fields -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        int score = decodeBDS50(&mm, false);
        ASSERT_EQ_INT("BDS50 no fields score", score, 0);
    }

    fprintf(stderr, "testDecodeBDS50: done\n\n");
}

// ---- testDecodeBDS60 ----

static void testDecodeBDS60(void) {
    fprintf(stderr, "=== testDecodeBDS60 ===\n");

    // Construct valid BDS60: heading=270°, IAS=300kt, mach=0.78, baro_rate=-512ft/min
    // heading: valid=1, sign=1, raw=512 (90° + 180° = 270°)
    // IAS: valid=1, raw=300
    // Mach: valid=1, raw=195 (195*2.048/512 ≈ 0.78)
    // baro_rate: valid=1, sign=1, raw=496 (496*32 - 16384 = -512)
    // inertial_rate: valid=0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);

        setbit(mm.MB, 1);                  // heading_valid
        setbit(mm.MB, 2);                  // heading_sign
        setbits_mb(mm.MB, 3, 12, 512);     // heading_raw
        setbit(mm.MB, 13);                 // ias_valid
        setbits_mb(mm.MB, 14, 23, 300);    // ias_raw
        setbit(mm.MB, 24);                 // mach_valid
        setbits_mb(mm.MB, 25, 34, 195);    // mach_raw
        setbit(mm.MB, 35);                 // baro_rate_valid
        setbit(mm.MB, 36);                 // baro_rate_sign
        setbits_mb(mm.MB, 37, 45, 496);    // baro_rate_raw

        int score = decodeBDS60(&mm, false);
        ASSERT_TRUE("BDS60 valid score > 0", score > 0);

        struct modesMessage mm2 = make_mm();
        memcpy(mm2.MB, mm.MB, 7);
        decodeBDS60(&mm2, true);
        ASSERT_EQ_INT("BDS60 commb_format", mm2.commb_format, COMMB_HEADING_SPEED);
        ASSERT_FLOAT_NEAR("BDS60 heading", mm2.heading, 270.0f, 0.5f);
        ASSERT_EQ_INT("BDS60 ias", (int)mm2.ias, 300);
        ASSERT_FLOAT_NEAR("BDS60 mach", mm2.mach, 195 * 2.048f / 512.0f, 0.01f);
        ASSERT_EQ_INT("BDS60 baro_rate", mm2.baro_rate, 496 * 32 - 16384);
    }

    // Missing required fields -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        int score = decodeBDS60(&mm, false);
        ASSERT_EQ_INT("BDS60 no fields score", score, 0);
    }

    fprintf(stderr, "testDecodeBDS60: done\n\n");
}

// ---- testDecodeCommB ----

static void testDecodeCommB(void) {
    fprintf(stderr, "=== testDecodeCommB ===\n");

    // Valid BDS20 payload -> COMMB_AIRCRAFT_IDENT
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20;

        // Encode "AAA     " (A=1, space=32)
        unsigned char indices[8] = {1, 1, 1, 32, 32, 32, 32, 32};
        for (int i = 0; i < 8; i++) {
            setbits_mb(mm.MB, 9 + i * 6, 14 + i * 6, indices[i]);
        }

        mm.DR = 0;
        mm.UM = 0;
        mm.correctedbits = 0;

        decodeCommB(&mm);
        ASSERT_EQ_INT("decodeCommB BDS20", mm.commb_format, COMMB_AIRCRAFT_IDENT);
    }

    // DR non-zero -> returns early (COMMB_UNKNOWN)
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20;
        mm.DR = 1;
        mm.UM = 0;
        mm.correctedbits = 0;

        decodeCommB(&mm);
        ASSERT_EQ_INT("decodeCommB DR!=0", mm.commb_format, COMMB_UNKNOWN);
    }

    // UM non-zero -> returns early (COMMB_UNKNOWN)
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20;
        mm.DR = 0;
        mm.UM = 1;
        mm.correctedbits = 0;

        decodeCommB(&mm);
        ASSERT_EQ_INT("decodeCommB UM!=0", mm.commb_format, COMMB_UNKNOWN);
    }

    // correctedbits > 0 -> returns early (COMMB_UNKNOWN)
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20;
        mm.DR = 0;
        mm.UM = 0;
        mm.correctedbits = 1;

        decodeCommB(&mm);
        ASSERT_EQ_INT("decodeCommB corrected", mm.commb_format, COMMB_UNKNOWN);
    }

    // All-zero MB -> COMMB_EMPTY_RESPONSE (score 56, unambiguous)
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.DR = 0;
        mm.UM = 0;
        mm.correctedbits = 0;

        decodeCommB(&mm);
        ASSERT_EQ_INT("decodeCommB empty", mm.commb_format, COMMB_EMPTY_RESPONSE);
    }

    fprintf(stderr, "testDecodeCommB: done\n\n");
}

// ---- testCheckAcasRaValid ----

static void testCheckAcasRaValid(void) {
    fprintf(stderr, "=== testCheckAcasRaValid ===\n");

    // All RA bits zero -> return 0
    {
        struct modesMessage mm = make_mm();
        unsigned char msg[7] = {0};
        ASSERT_EQ_INT("ACAS all zero", checkAcasRaValid(msg, &mm, 0), 0);
    }

    // Complementary bits both set (above+below, bits 23+24) -> return 0
    {
        struct modesMessage mm = make_mm();
        unsigned char msg[7] = {0};
        // Set ARA (bit 9) to make it non-trivially zero
        msg[1] = 0x80; // bit 9
        // Set bits 23 and 24 (complementary pair)
        // bit 23 is byte 2, bit 7 (0x02) ... actually 1-indexed:
        // bit 23 = byte (23-1)/8 = byte 2, bit 8-((23-1)%8) = bit 6 = 0x02
        // bit 24 = byte 2, bit 5 = 0x01
        msg[2] |= 0x02; // bit 23
        msg[2] |= 0x01; // bit 24
        ASSERT_EQ_INT("ACAS complementary above/below", checkAcasRaValid(msg, &mm, 0), 0);
    }

    // TTI=3 (unassigned) -> return 0
    {
        struct modesMessage mm = make_mm();
        mm.msgtype = 0; // not DF16
        unsigned char msg[7] = {0};
        msg[1] = 0x80; // ARA bit 9
        // bits 29-30 = TTI = 3
        // bit 29 = byte 3, bit 4 = 0x08
        // bit 30 = byte 3, bit 3 = 0x04
        msg[3] = 0x0C; // TTI = 11 = 3
        // Need bits 25,26 = 0 (left/right) for non-DF16
        // bits 25,26 are in byte 3: bit 8, bit 7 = 0x80, 0x40
        // Already 0, good
        ASSERT_EQ_INT("ACAS TTI=3", checkAcasRaValid(msg, &mm, 0), 0);
    }

    // Valid RA with TTI=0, DF16, clean reserved bits -> return 1
    {
        struct modesMessage mm = make_mm();
        mm.msgtype = 16;
        unsigned char msg[7] = {0};
        // Set ARA (bit 9)
        msg[1] = 0x80;
        // bits 29-56 must be 0 for DF16 -> they already are
        ASSERT_EQ_INT("ACAS valid DF16", checkAcasRaValid(msg, &mm, 0), 1);
    }

    // Valid RA with TTI=0, non-DF16, bits 31-56 zero -> return 1
    {
        struct modesMessage mm = make_mm();
        mm.msgtype = 0;
        unsigned char msg[7] = {0};
        // Set ARA (bit 9)
        msg[1] = 0x80;
        // TTI=0 (bits 29-30 = 0), bits 31-56 = 0
        ASSERT_EQ_INT("ACAS valid TTI=0", checkAcasRaValid(msg, &mm, 0), 1);
    }

    fprintf(stderr, "testCheckAcasRaValid: done\n\n");
}

// ---- testDecodeBDS17 ----

static void testDecodeBDS17(void) {
    fprintf(stderr, "=== testDecodeBDS17 ===\n");

    // Reserved bits (25-56) non-zero -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        setbit(mm.MB, 25); // reserved bit set
        int score = decodeBDS17(&mm, false);
        ASSERT_EQ_INT("BDS17 reserved", score, 0);
    }

    // ES capable (bits 1-5 set), ES EDI (bit 6), ident (bit 7),
    // track+heading (bits 16,24), vertical intent (bit 9)
    // score = 5(ES) + 1(EDI) + 1(ident) + 2(track/head) + 1(vert intent) = 10
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        for (int b = 1; b <= 7; b++) setbit(mm.MB, b);
        setbit(mm.MB, 9);  // vertical intent
        setbit(mm.MB, 16); // track/turn
        setbit(mm.MB, 24); // heading/speed
        int score = decodeBDS17(&mm, false);
        ASSERT_EQ_INT("BDS17 ES capable", score, 10);
    }

    // Not ES capable (bits 1-6 all zero), no ident (bit 7 zero),
    // neither track/heading (bits 16,24,9 zero)
    // score = 1(not ES) + (-2)(no ident) + 1(neither track/head) = 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        int score = decodeBDS17(&mm, false);
        ASSERT_EQ_INT("BDS17 not ES", score, 0);
    }

    // Partial ES (bits 1,2 set, 3-5 zero) -> -12 penalty
    // Also no ident, neither track/heading
    // score = -12(partial ES) + (-2)(no ident) + 1(neither) = -13
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        setbit(mm.MB, 1);
        setbit(mm.MB, 2);
        int score = decodeBDS17(&mm, false);
        ASSERT_EQ_INT("BDS17 partial ES", score, -13);
    }

    // Store sets COMMB_GICB_CAPS
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        decodeBDS17(&mm, true);
        ASSERT_EQ_INT("BDS17 store", mm.commb_format, COMMB_GICB_CAPS);
    }

    fprintf(stderr, "testDecodeBDS17: done\n\n");
}

// ---- testDecodeBDS30 ----

static void testDecodeBDS30(void) {
    fprintf(stderr, "=== testDecodeBDS30 ===\n");

    // Valid header (0x30) -> score 56
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x30;
        int score = decodeBDS30(&mm, false);
        ASSERT_EQ_INT("BDS30 valid score", score, 56);
    }

    // Store sets COMMB_ACAS_RA and acas_ra_valid
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x30;
        decodeBDS30(&mm, true);
        ASSERT_EQ_INT("BDS30 store fmt", mm.commb_format, COMMB_ACAS_RA);
        ASSERT_EQ_INT("BDS30 store valid", mm.acas_ra_valid, 1);
    }

    // Wrong header (0x20) -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x20;
        int score = decodeBDS30(&mm, false);
        ASSERT_EQ_INT("BDS30 wrong hdr", score, 0);
    }

    // store=false -> commb_format unchanged
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        mm.MB[0] = 0x30;
        decodeBDS30(&mm, false);
        ASSERT_EQ_INT("BDS30 no store", mm.commb_format, 0);
    }

    fprintf(stderr, "testDecodeBDS30: done\n\n");
}

// ---- testDecodeBDS40EdgeCases ----

static void testDecodeBDS40EdgeCases(void) {
    fprintf(stderr, "=== testDecodeBDS40EdgeCases ===\n");

    // MCP valid but extreme altitude (>50000ft) -> score 0 (returns 0)
    // mcp_raw for 60000ft: 60000/16 = 3750
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        setbit(mm.MB, 1);                  // mcp_valid
        setbits_mb(mm.MB, 2, 13, 3750);    // mcp_raw -> 60000ft
        int score = decodeBDS40(&mm, false);
        ASSERT_EQ_INT("BDS40 extreme alt", score, 0);
    }

    // MCP valid=1 but raw=0 -> inconsistency -> score 0
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        setbit(mm.MB, 1); // mcp_valid=1, mcp_raw=0
        int score = decodeBDS40(&mm, false);
        ASSERT_EQ_INT("BDS40 valid no raw", score, 0);
    }

    fprintf(stderr, "testDecodeBDS40EdgeCases: done\n\n");
}

// ---- testDecodeBDS44 ----

static void testDecodeBDS44(void) {
    fprintf(stderr, "=== testDecodeBDS44 ===\n");

    // Case 1: Valid with wind, temp 20°C
    // source=1 (bits 1-4), wind_valid=1 (bit 5), wind_speed=200 (bits 6-14),
    // wind_direction=128 (bits 15-23), temp_sign=0 (bit 24), temp_raw=80 (bits 25-34) -> 20°C
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        setbits_mb(mm.MB, 1, 4, 1);    // source=1 (valid: 0-6)
        setbit(mm.MB, 5);              // wind_valid=1
        setbits_mb(mm.MB, 6, 14, 200);  // wind_speed=200
        setbits_mb(mm.MB, 15, 23, 128); // wind_direction=128
        // bit 24 = 0 (temp_sign positive)
        setbits_mb(mm.MB, 25, 34, 80);  // temp_raw=80 -> 80*0.25=20°C
        // pressure_valid=0, turbulence_valid=0, humidity_valid=0

        int score = decodeBDS44(&mm, false);
        ASSERT_TRUE("BDS44 wind score>0", score > 0);

        // Store and verify
        struct modesMessage mm2 = make_mm();
        memcpy(mm2.MB, mm.MB, 7);
        decodeBDS44(&mm2, true);
        ASSERT_EQ_INT("BDS44 wind_valid", mm2.wind_valid, 1);
        ASSERT_FLOAT_NEAR("BDS44 oat", mm2.oat, 20.0f, 0.5f);
        ASSERT_EQ_INT("BDS44 commb_format", mm2.commb_format, COMMB_METEOROLOGICAL_ROUTINE);
    }

    // Case 2: Invalid source (>6)
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        setbits_mb(mm.MB, 1, 4, 7);  // source=7 (invalid: >6)
        int score = decodeBDS44(&mm, false);
        ASSERT_EQ_INT("BDS44 bad source", score, 0);
    }

    // Case 3: Temp only (no wind), 25°C
    {
        struct modesMessage mm = make_mm();
        memset(mm.MB, 0, 7);
        setbits_mb(mm.MB, 1, 4, 1);     // source=1
        // wind_valid=0 (bit 5 not set)
        // bit 24 = 0 (temp_sign positive)
        setbits_mb(mm.MB, 25, 34, 100);  // temp_raw=100 -> 100*0.25=25°C

        int score = decodeBDS44(&mm, false);
        ASSERT_TRUE("BDS44 temp score>0", score > 0);

        struct modesMessage mm2 = make_mm();
        memcpy(mm2.MB, mm.MB, 7);
        decodeBDS44(&mm2, true);
        ASSERT_FLOAT_NEAR("BDS44 temp oat", mm2.oat, 25.0f, 0.5f);
        ASSERT_EQ_INT("BDS44 temp wind_valid", mm2.wind_valid, 0);
    }

    fprintf(stderr, "testDecodeBDS44: done\n\n");
}

// ---- main ----

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    testDecodeEmptyResponse();
    testDecodeBDS10();
    testDecodeBDS20();
    testDecodeBDS40();
    testDecodeBDS50();
    testDecodeBDS60();
    testDecodeCommB();
    testCheckAcasRaValid();
    testDecodeBDS17();
    testDecodeBDS30();
    testDecodeBDS40EdgeCases();
    testDecodeBDS44();

    if (failures) {
        fprintf(stderr, "\n%d FAILURE(S)\n", failures);
        return 1;
    }

    fprintf(stderr, "\nAll tests passed.\n");
    return 0;
}
