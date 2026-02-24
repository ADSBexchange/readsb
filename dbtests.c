// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// dbtests.c - tests for DB hash table and UAV address handling (AX-688)
//
// Standalone test binary with no framework dependencies, following the
// cprtests.c pattern. Self-contained to avoid the readsb.h dependency chain.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fasthash.h"

// ---- Copied from aircraft.h (avoids readsb.h dependency chain) ----

static inline uint32_t addrHash(uint32_t addr, uint32_t bits) {
    const uint64_t m = 0x880355f21e6d1965ULL;
    const uint64_t seed = 0x30732349f7810465ULL;
    uint64_t h = seed ^ (4 * m);

    uint64_t v = addr;
    h ^= mix_fasthash(v);
    h *= m;
    h = mix_fasthash(h);

    uint64_t res = h ^ (h >> 32);

    if (bits < 16)
        res ^= (res >> 16);

    res ^= (res >> bits);

    res &= (((uint64_t) 1) << bits) - 1;

    return (uint32_t) res;
}

typedef struct dbEntry {
    struct dbEntry *next;
    uint32_t addr;
    uint16_t dbFlags;
    char typeCode[4];
    char registration[12];
    char typeLong[64];
    char ownOp[64];
    char year[4];
} dbEntry;

// ---- Copied from readsb.h ----

#define MODES_NON_ICAO_ADDRESS  (1 << 24)
#define MODES_UAV_ADDRESS       (1 << 25)
#define DB_HASH_BITS 20
#define DB_BUCKETS (1 << DB_HASH_BITS)

// ---- Copied from aircraft.c (stable 3-5 line functions) ----

static inline uint32_t dbHash(uint32_t addr) {
    return addrHash(addr, DB_HASH_BITS);
}

static dbEntry *dbGet(uint32_t addr, dbEntry **index) {
    if (!index)
        return NULL;
    dbEntry *d = index[dbHash(addr)];
    while (d && d->addr != addr) {
        d = d->next;
    }
    return d;
}

static void dbPut(uint32_t addr, dbEntry **index, dbEntry *d) {
    uint32_t hash = dbHash(addr);
    d->next = index[hash];
    index[hash] = d;
}

// ---- Test helpers ----

static dbEntry **makeIndex(void) {
    dbEntry **index = calloc(DB_BUCKETS, sizeof(dbEntry *));
    if (!index) {
        fprintf(stderr, "FATAL: calloc failed for DB index\n");
        exit(1);
    }
    return index;
}

// ---- Tests ----

static int testDbPutGet(void) {
    int ok = 1;
    dbEntry **index = makeIndex();

    // Put an entry and get it back
    dbEntry e1 = { .addr = 0xABCDEF };
    memcpy(e1.registration, "N12345", 6);
    dbPut(e1.addr, index, &e1);

    dbEntry *got = dbGet(0xABCDEF, index);
    if (got != &e1) {
        ok = 0;
        fprintf(stderr, "testDbPutGet[put-get]: FAIL: returned wrong pointer\n");
    } else {
        fprintf(stderr, "testDbPutGet[put-get]: PASS\n");
    }

    // Get non-existent address
    got = dbGet(0x123456, index);
    if (got != NULL) {
        ok = 0;
        fprintf(stderr, "testDbPutGet[miss]: FAIL: non-existent returned non-NULL\n");
    } else {
        fprintf(stderr, "testDbPutGet[miss]: PASS\n");
    }

    // Multiple entries survive in the table
    dbEntry e2 = { .addr = 0xFEDCBA };
    memcpy(e2.registration, "G-ABCD", 6);
    dbPut(e2.addr, index, &e2);

    dbEntry e3 = { .addr = 0x112233 };
    dbPut(e3.addr, index, &e3);

    if (dbGet(0xABCDEF, index) != &e1 ||
        dbGet(0xFEDCBA, index) != &e2 ||
        dbGet(0x112233, index) != &e3) {
        ok = 0;
        fprintf(stderr, "testDbPutGet[multiple]: FAIL: not all entries retrievable\n");
    } else {
        fprintf(stderr, "testDbPutGet[multiple]: PASS\n");
    }

    free(index);
    return ok;
}

static int testAddressFlags(void) {
    int ok = 1;
    dbEntry **index = makeIndex();

    // ICAO, TIS-B, and UAV entries for the same base address
    uint32_t base = 0x000001;
    uint32_t icao_addr = base;
    uint32_t tisb_addr = base | MODES_NON_ICAO_ADDRESS;
    uint32_t uav_addr  = base | MODES_NON_ICAO_ADDRESS | MODES_UAV_ADDRESS;

    dbEntry icao_e = { .addr = icao_addr };
    dbEntry tisb_e = { .addr = tisb_addr };
    dbEntry uav_e  = { .addr = uav_addr };

    dbPut(icao_addr, index, &icao_e);
    dbPut(tisb_addr, index, &tisb_e);
    dbPut(uav_addr,  index, &uav_e);

    // All three coexist
    if (dbGet(icao_addr, index) != &icao_e) {
        ok = 0;
        fprintf(stderr, "testAddressFlags[icao-get]: FAIL\n");
    } else {
        fprintf(stderr, "testAddressFlags[icao-get]: PASS\n");
    }

    if (dbGet(tisb_addr, index) != &tisb_e) {
        ok = 0;
        fprintf(stderr, "testAddressFlags[tisb-get]: FAIL\n");
    } else {
        fprintf(stderr, "testAddressFlags[tisb-get]: PASS\n");
    }

    if (dbGet(uav_addr, index) != &uav_e) {
        ok = 0;
        fprintf(stderr, "testAddressFlags[uav-get]: FAIL\n");
    } else {
        fprintf(stderr, "testAddressFlags[uav-get]: PASS\n");
    }

    // Cross-isolation: ICAO lookup must not return TIS-B or UAV
    if (dbGet(icao_addr, index) == &tisb_e || dbGet(icao_addr, index) == &uav_e) {
        ok = 0;
        fprintf(stderr, "testAddressFlags[icao-isolation]: FAIL\n");
    } else {
        fprintf(stderr, "testAddressFlags[icao-isolation]: PASS\n");
    }

    // UAV lookup must not return ICAO or TIS-B
    if (dbGet(uav_addr, index) == &icao_e || dbGet(uav_addr, index) == &tisb_e) {
        ok = 0;
        fprintf(stderr, "testAddressFlags[uav-isolation]: FAIL\n");
    } else {
        fprintf(stderr, "testAddressFlags[uav-isolation]: PASS\n");
    }

    // 0xFFFFFF mask strips both flag bits for display output
    if ((icao_addr & 0xFFFFFF) != base ||
        (tisb_addr & 0xFFFFFF) != base ||
        (uav_addr  & 0xFFFFFF) != base) {
        ok = 0;
        fprintf(stderr, "testAddressFlags[mask]: FAIL\n");
    } else {
        fprintf(stderr, "testAddressFlags[mask]: PASS\n");
    }

    free(index);
    return ok;
}

// Inline reimplementation of CSV $ prefix detection logic (aircraft.c)
static uint32_t parseCsvAddress(const char *s) {
    int is_uav = (s[0] == '$');
    if (is_uav)
        s++;
    uint32_t addr = strtol(s, NULL, 16);
    if (addr == 0)
        return 0;
    if (is_uav)
        addr |= MODES_NON_ICAO_ADDRESS | MODES_UAV_ADDRESS;
    return addr;
}

static int testCsvAddressParsing(void) {
    int ok = 1;

    struct { const char *input; uint32_t expected; } cases[] = {
        { "000001",  0x000001 },                                             // plain ICAO
        { "$000001", MODES_NON_ICAO_ADDRESS | MODES_UAV_ADDRESS | 0x000001 }, // UAV, both flags
        { "$000000", 0 },                                                     // addr==0 after strip → skip
        { "$FFFFFF", MODES_NON_ICAO_ADDRESS | MODES_UAV_ADDRESS | 0xFFFFFF }, // UAV max addr
        { "ABCDEF",  0x00ABCDEF },                                           // plain ICAO
    };
    int n = sizeof(cases) / sizeof(cases[0]);

    for (int i = 0; i < n; i++) {
        uint32_t got = parseCsvAddress(cases[i].input);
        if (got != cases[i].expected) {
            ok = 0;
            fprintf(stderr, "testCsvAddressParsing[%d]: FAIL: \"%s\" → 0x%08x (expected 0x%08x)\n",
                    i, cases[i].input, got, cases[i].expected);
        } else {
            fprintf(stderr, "testCsvAddressParsing[%d]: PASS\n", i);
        }
    }

    return ok;
}

// Inline reimplementation of the prefix ternary from sprintDB (aircraft.c)
static const char *dbPrefix(uint32_t addr) {
    return (addr & MODES_UAV_ADDRESS) ? "$" :
           (addr & MODES_NON_ICAO_ADDRESS) ? "~" : "";
}

static int testSprintDBPrefix(void) {
    int ok = 1;

    struct { uint32_t addr; const char *expected; } cases[] = {
        { 0x000001,                                              "" },  // ICAO
        { 0x000001 | MODES_NON_ICAO_ADDRESS,                    "~" }, // TIS-B
        { 0x000001 | MODES_NON_ICAO_ADDRESS | MODES_UAV_ADDRESS, "$" }, // UAV
    };
    int n = sizeof(cases) / sizeof(cases[0]);

    for (int i = 0; i < n; i++) {
        const char *got = dbPrefix(cases[i].addr);
        if (strcmp(got, cases[i].expected) != 0) {
            ok = 0;
            fprintf(stderr, "testSprintDBPrefix[%d]: FAIL: 0x%08x → \"%s\" (expected \"%s\")\n",
                    i, cases[i].addr, got, cases[i].expected);
        } else {
            fprintf(stderr, "testSprintDBPrefix[%d]: PASS\n", i);
        }
    }

    return ok;
}

int main(int __attribute__((unused)) argc, char __attribute__((unused)) **argv) {
    int ok = 1;
    ok = testDbPutGet() && ok;
    ok = testAddressFlags() && ok;
    ok = testCsvAddressParsing() && ok;
    ok = testSprintDBPrefix() && ok;
    return ok ? 0 : 1;
}
