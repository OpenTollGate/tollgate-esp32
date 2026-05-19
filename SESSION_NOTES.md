# Session Notes — Price Discovery Hardware Integration

## Date: 2026-05-18

## Summary

The price discovery feature (WiFi Vendor IE beacon advertising) is fully implemented and unit-tested. Hardware integration testing is blocked by two issues: (1) ESP32 boards failing to associate with the upstream WiFi router (reason 211 = NO_AP_FOUND), and (2) competing LLM sessions in other worktrees continuously reflashing the boards.

## Commits on `feature/price-discovery`

| Hash | Description |
|------|-------------|
| `ba8af3a` | Initial price discovery implementation |
| `5f69aaa` | Integration tests + Makefile targets |
| `a68fc46` | Unit test fixes |
| `dd253f0` | ESP-IDF build fixes (format specifiers, symlinks) |
| `c99deaa` | Per-board hardware mutex in Makefile |
| `4e4576c` | write-config targets, SPIFFS image generation |
| `5b36dba` | WiFi disconnect reason code logging |
| `bc57c4e` | WiFi country code set to DE for EU regulatory compliance |

## Key Findings

### 1. WiFi STA Connectivity Failure (reason=211)

**Symptom:** Both ESP32 boards report `WIFI_REASON_NO_AP_FOUND` (reason 211) when scanning for `EnterSSID-2.4GHz`. The laptop sees the router at 100% signal strength on channel 10.

**Observations:**
- Board B successfully obtained STA IP once during this session (CVM relay connections logged)
- After the other session reflashed the board, STA connectivity was lost again
- The same firmware that worked earlier stopped working after a reflash cycle
- Both boards' APs (TollGate-B96D80, TollGate-C0E9CA) are visible to the laptop at 99-100% signal

**Potential causes:**
- **APSTA co-channel limitation:** ESP32 AP is on channel 1, router on channel 10. In APSTA mode, the ESP32 may have reduced scan sensitivity on non-AP channels
- **USB power instability:** CH340 USB-serial adapters cause unreliable flashing (frequent "chip stopped responding" errors on Board A)
- **esptool stub leaving board in download mode:** After flash, `--after hard_reset` via RTS pin doesn't always boot the app. USB device authorized toggle needed
- **Multiple LLM sessions competing for hardware:** Other worktrees (`esp32-tollgate-arch`, `esp32-tollgate-display`, main `esp32-tollgate`) flash boards concurrently, overwriting our firmware within seconds

**What we fixed:**
- Added `esp_wifi_set_country_code("DE")` — was missing, defaults to CN which limits EU channels/power
- Added disconnect reason code to log output for debugging

### 2. Board A Hardware Issues

Board A (MAC `94:a9:90:2e:37:7c`) has persistent problems:
- Flash operations frequently fail with "chip stopped responding" or "StopIteration"
- After esptool flash, board enters download mode (`boot:0x0 DOWNLOAD`) instead of app mode
- Requires USB device authorized toggle to recover
- The AGENTS.md in the main repo confirms: *"Board A WiFi is broken — hardware issue confirmed: WIFI_REASON_AUTH_EXPIRED on all APs"*

### 3. Port Instability

Board ports re-enumerate after every USB reset:
- Typical mapping: Board A=ACM0, Board B=ACM1, Board C=ACM2
- After USB reset: ports shift unpredictably (ACM0→ACM3, ACM1→ACM0, etc.)
- The Makefile defaults (`PORT_A ?= /dev/ttyACM1`, `PORT_B ?= /dev/ttyACM2`) are often wrong
- Must always verify with `esptool.py --port <port> chip_id` before flashing
- The `boards.env` file uses stable `/dev/serial/by-id/` paths but Makefile uses raw `/dev/ttyACM*`

### 4. Multi-Session Hardware Conflict

Three other LLM sessions operate simultaneously:
- `esp32-tollgate` (main repo) — flashes both boards with main-branch firmware
- `esp32-tollgate-arch` — flashes Board A with architecture branch firmware
- `esp32-tollgate-display` — flashes Board C with display branch firmware

These sessions do not coordinate via the lock system. Even with locks held, other sessions bypass them by calling `esptool.py` directly. Our firmware was overwritten multiple times during testing — confirmed by seeing `mint.minibits.cash` (other session's default) instead of `testnut.cashu.space` (our config).

### 5. SPIFFS Config Verification

SPIFFS partition survives firmware flashes (different partition offsets):
- Firmware: `0x0` (bootloader), `0x8000` (partition table), `0x10000` (app)
- SPIFFS: `0x410000` (storage partition)
- Read-back confirmed our config is correctly written (`testnut.cashu.space`, `price_per_step: 21`)
- But the other session's firmware may also write its own SPIFFS, overwriting ours

## Successful Tests

### Single-board market test (Board B)
- `GET /market` returns valid JSON with `entries: []` (no neighbors discovered)
- `GET /` returns correct TollGate event with our config values
- 4/4 tests passed

### Single-board market test (Board A)
- Same as above, 4/4 tests passed
- Required workaround for Board A's flash issues (USB reset between flash and boot)

### Unit tests
- All 13 test suites pass (45 new assertions across `test_beacon_price` + `test_market`)
- `make test-unit` passes cleanly

## Recommendations for Next Session

1. **Coordinate with other sessions** — agree on exclusive hardware windows or add lock-checking to all flash paths
2. **Use `/dev/serial/by-id/` paths** — update Makefile `PORT_A`/`PORT_B` to use stable by-id symlinks
3. **Test with boards physically closer to router** — eliminate RF as a variable
4. **Consider starting services without STA** — modify `start_services()` to start beacon + market + API even without STA IP, so price discovery can be tested in isolation
5. **Use `--no-stub` esptool mode** — the stub leaves boards in download mode; direct flash without stub may be more reliable

## Additional Finding: CVM set_config Overwrites Runtime Config

The CVM (ContextVM) server receives `set_config` MCP commands via Nostr relay. When a `set_config` command arrives (e.g., changing `mint_url` or `price_per_step`), it modifies the in-memory config. This explains why the API returns `mint.minibits.cash` and `price_per_step: 1` even though our SPIFFS has `testnut.cashu.space` and `price_per_step: 21`. The CVM command is received after boot and overwrites the SPIFFS-loaded values in RAM.

This also means **our firmware IS running on the board** (confirmed by market scan log messages), but the CVM is changing the visible config values. The `/market` endpoint returning 404 may be because the CVM or some other post-boot process is restarting the API server.

## AP-Only Services

Added `start_ap_services()` which starts tollgate_api, beacon_price, and market scanner on `WIFI_EVENT_AP_START` — independent of STA connectivity. This allows testing price discovery without internet access. Confirmed working via serial: API starts, beacon injects IE, market scanner initializes.

### /market 404 Investigation

The `/market` endpoint returns 404 even when our firmware is confirmed running via serial. Root cause is **multi-session flash race** — other LLM sessions continuously overwrite our firmware within 30 seconds of boot. Key evidence:
- Serial shows `TollGate API started on port 2121` and `Market scanner initialized` (our firmware)
- HTTP `/whoami` and `/usage` work (return correct data)
- HTTP `/market` returns "Nothing matches the given URI" (handler not registered)
- HTTP `/` returns `mint.minibits.cash` instead of `testnut.cashu.space`
- Debug `>>>` log markers added to handlers never appear in serial

The discrepancy between serial (our firmware) and HTTP (other firmware) is explained by a **reflash during the 15-30 second wait** between boot verification and HTTP testing. The board reboots with the other session's firmware silently.

### Confirmed via Serial (Our Firmware)
```
I (1874) tollgate_api: TollGate API started on port 2121
I (1878) beacon_price: Built IE: price=21 sats, step=60000, metric=milliseconds
I (1886) beacon_price: Price advertising started (beacon + probe response)
I (1893) market: Market scanner initialized
I (1896) tollgate_main: === AP-only services started (no STA) ===
```

### ESP32 APSTA Channel Behavior (Confirmed from ESP-IDF docs)
From `esp-idf/docs/en/api-guides/wifi.rst:1684`:
> In station/AP-coexistence mode, the home channel of AP and station must be the same. The station's home channel is always in priority. The AP switches using Channel Switch Announcement (CSA).

AP channel mismatch (AP=0/auto, router=10) is **NOT** the cause of reason=211. The ESP32 scans all channels regardless.

### Fixes Applied
1. `market_tick()`: Update `last_scan_ms` on scan failure — prevents 1-second retry spam
2. `market_tick()`: Log failure count, suppress after 3 failures (every 30th thereafter)
3. `config.c`: AP channel default changed from 1 to 0 (auto-select)
4. `tollgate_api.c`: Debug logging on `/market` and `/` handlers, check registration return
