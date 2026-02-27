# readsb Test Coverage Reference

Last updated: 2026-02-26

## Summary

| Category | Count |
|----------|-------|
| Unit test binaries | 22 |
| Unit test functions | ~200 |
| Integration test files | 13 |
| Integration test functions | 87 |

## Unit Tests

### Test Pattern Legend

| Pattern | Description |
|---------|-------------|
| `#include "source.c"` | Includes source directly to test static functions. Requires stubs for external deps. |
| `link .o` | Links against compiled object file. Can only test exported (non-static) functions. |
| `copy` | Copies function code into test file. **Deprecated** — should be converted to `#include`. |
| `standalone` | Self-contained test, no external deps. |

### Unit Test Files

| File | Source | Pattern | Tests | Stubs | Libs |
|------|--------|---------|-------|-------|------|
| `cprtests.c` | `cpr.c` | link .o | 3 | 0 | -lm |
| `cpr_tests.c` | `cpr.c` | #include | 9 | 0 | -lm |
| `crctests.c` | `crc.c` | standalone (CRCDEBUG) | 3 | 0 | — |
| `crc_tests.c` | `crc.c` | #include | 6 | 3 | -lm |
| `dbtests.c` | `aircraft.c` | #include | 4 | 2 | — |
| `unittests.c` | multiple | link .o | 8 | 3 | -lm |
| `mode_s_tests.c` | `mode_s.c` | #include | 22 | 5 | -lm |
| `mode_ac_tests.c` | `mode_ac.c` | #include | 7 | 3 | -lm |
| `comm_b_tests.c` | `comm_b.c` | #include | 12 | 3 | -lm |
| `icao_filter_tests.c` | `icao_filter.c` | link .o | 7 | 3 | -lm |
| `convert_tests.c` | `convert.c` | #include | 9 | 3 | -lm |
| `util_tests.c` | `util.c` | link .o | 10 | 4 | -lm -lzstd -lz -lpthread -lrt |
| `geomag_tests.c` | `geomag.c` | link .o | 6 | 0 | -lm |
| `track_tests.c` | `track.c` | #include | 19 | 8 | -lm -lpthread |
| `stats_tests.c` | `stats.c` | #include | 7 | 5 | -lm |
| `net_io_tests.c` | `net_io.c` | #include | 10 | 12 | -lm -lzstd -lz -lpthread -lrt |
| `json_out_tests.c` | `json_out.c` | #include | 8 | 14 | -lm -lz |
| `api_tests.c` | `api.c` | #include | 15 | 10 | -lm -lzstd -lz -lpthread -lrt |
| `aircraft_tests.c` | `aircraft.c` | #include | 10 | 8 | -lm -lz |
| `demod_tests.c` | `demod_2400.c` | #include | 9 | 5 | -lm |
| `globe_index_tests.c` | `globe_index.c` | copy | 4 | 0 | -lm |
| `receiver_tests.c` | `receiver.c` | #include | 9 | 5 | -lm |
| `ais_charset_tests.c` | `ais_charset.c` | link .o | 3 | 0 | — |

### Makefile Test Targets

Each unit test has a build target and a run target:

| Build target | Run target | Binary |
|-------------|-----------|--------|
| `cprtests` | `cprtest` | cprtests |
| `cpr_tests` | `cptest` | cpr_tests |
| `crctests` | — | crctests |
| `crc_tests` | `crtest2` | crc_tests |
| `dbtests` | `dbtest` | dbtests |
| `unittests` | `unittest` | unittests |
| `mode_s_tests` | `mstest` | mode_s_tests |
| `mode_ac_tests` | `mctest` | mode_ac_tests |
| `comm_b_tests` | `cbtest` | comm_b_tests |
| `icao_filter_tests` | `iftest` | icao_filter_tests |
| `convert_tests` | `cvtest` | convert_tests |
| `util_tests` | `uttest` | util_tests |
| `geomag_tests` | `gmtest` | geomag_tests |
| `track_tests` | `tktest` | track_tests |
| `stats_tests` | `sttest` | stats_tests |
| `net_io_tests` | `nittest` | net_io_tests |
| `json_out_tests` | `jotest` | json_out_tests |
| `api_tests` | `aptest` | api_tests |
| `aircraft_tests` | `attest` | aircraft_tests |
| `demod_tests` | `dmtest` | demod_tests |
| `globe_index_tests` | `gitest` | globe_index_tests |
| `receiver_tests` | `rctest` | receiver_tests |
| `ais_charset_tests` | `actest` | ais_charset_tests |

Run all unit tests: `make test`

## Integration Tests

Integration tests use Python 3 unittest, spawning a real readsb process per test class.

| File | Classes | Tests | What it covers |
|------|---------|-------|----------------|
| `test_sbs.py` | TestSbsToJson, TestSbsPassthrough | 8 | SBS input → JSON output, SBS passthrough |
| `test_beast.py` | TestBeastInput, TestMultiOutput, TestBeastReduceOutput, TestBeastOutputFormat, TestBeastReduceFilterAlt | 9 | Beast binary input, multi-format output, reduce, escaping, altitude filter |
| `test_raw_hex.py` | TestRawHexInput | 4 | Raw hex input (DF17, @-prefix, malformed, short) |
| `test_api.py` | TestApi, TestApiFindCallsign, TestApiHexList | 9 | HTTP API: /?all, find_callsign, find_hex |
| `test_api_filters.py` | TestApiCallsignFilters, TestApiRegAndType | 7 | Callsign prefix/exact filters, reg/type lookup |
| `test_api_geo.py` | TestApiGeoBox, TestApiGeoCircle, TestApiFilters | 10 | Geographic box/circle queries, altitude/squawk/pos filters |
| `test_api_combined.py` | TestApiCombinedQueries, TestApiBinaryOutput | 6 | Combined query params, bincraft/zstd output |
| `test_network.py` | TestConnectionResilience, TestMultipleClients, TestNetConnector, TestNetConnectorBeastOut | 7 | Reconnection, multi-client, net-connector SBS/Beast |
| `test_output_formats.py` | TestStatsJson, TestVrsJsonOutput, TestJsonNetworkOutput | 3 | stats.json, VRS JSON, JSON-over-TCP |
| `test_lifecycle.py` | TestShutdown, TestAircraftStaleness | 2 | SIGTERM shutdown, seen-time staleness |
| `test_uav.py` | TestUav, TestUavApi, TestUavRejected | 8 | UAV/drone support, API queries, --enable-uav gate |
| `test_net_connector.py` | TestBeastDF17Input, TestMultiSource | 5 | Beast DF17 position, multi-source (SBS+Beast) |
| `test_stats_and_history.py` | TestReceiverJson, TestJsonFileWriting | 9 | receiver.json, aircraft.json, file updates, message counts |

Run all integration tests: `make inttest`
Run everything: `make fulltest`

## Source Files Without Dedicated Tests

| Source | Reason |
|--------|--------|
| `readsb.c` | Main entry point, startup/shutdown — tested via integration tests |
| `argp.c` | Argument parsing — tested implicitly by integration tests |
| `anet.c` | Networking primitives — tested via net_io integration |
| `interactive.c` | Terminal UI (ncurses) — requires TTY, not testable in CI |
| `sdr.c`, `sdr_*.c` | Hardware SDR interfaces — require physical devices |
| `uat2esnt/*.c` | UAT→ES/NT translation — niche, low ROI |
| `minilzo.c` | Third-party compression library |
| `threadpool.c` | Threading infrastructure — tested indirectly |

## Known Issues

1. **`globe_index_tests.c` uses copy pattern** — 30+ stubs needed for `#include` conversion, poor ROI for 4 tests. Deferred until globe_index.c is refactored.
2. **Callsign filter case-sensitivity** — `/?find_callsign` is case-insensitive in the API but test_api_filters.py documents this as working behavior.

## How to Add New Tests

### Unit Test (recommended: `#include` pattern)

1. Create `<name>_tests.c`
2. Include standard headers and `readsb.h`
3. Add linker stubs (check with `nm -u <source>.o | grep -v __`)
4. Add `#include "<source>.c"` after stubs
5. Write test functions using the `ASSERT_*` macros
6. Add to Makefile:
   ```makefile
   <name>_tests: <name>_tests.o fasthash.o
   	$(CC) $(CFLAGS) -o $@ $^ -lm [additional libs]

   <short>test: <name>_tests
   	./<name>_tests
   ```
7. Add to `test:` target and `clean:` target

### Integration Test

1. Create `tests/test_<name>.py`
2. Import from `conftest` (ReadsbInstance, protocol helpers)
3. Use `setUpClass`/`tearDownClass` to manage readsb instance
4. Feed data via SBS/Beast, verify via JSON/API
5. Tests are auto-discovered by `make inttest`
