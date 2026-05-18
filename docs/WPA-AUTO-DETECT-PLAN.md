# WPA Auto-Detect + STA Connectivity Fix

## Problem

`config.c:322` hardcodes `WIFI_AUTH_WPA3_PSK` as the STA auth threshold. The home
router (`EnterSSID-2.4GHz`) uses **WPA2**, so the ESP32 silently refuses
association and never gets internet. This blocks health probes, real payments,
and all downstream testing.

Additionally, concurrent HTTP client connections at boot (wallet init + health probes
+ CVM + wifistr) caused an lwip `mem_free` assertion crash.

## Solution

### 1. Runtime WPA Threshold (Firmware)

Add `wifi_auth_mode` field to `tollgate_config_t`. Parse it from `config.json`
as a string (`"WPA2"`, `"WPA3"`, `"WPA2_WPA3"`). Map to ESP-IDF
`wifi_auth_mode_t` enum at runtime. Default to `WIFI_AUTH_WPA2_PSK` which
accepts both WPA2 and WPA3 networks.

### 2. Makefile Auto-Detect (Build Time)

Add Makefile targets that scan WiFi with `nmcli`, detect WPA2 vs WPA3, and
generate a SPIFFS image with the correct `wifi_auth_mode` baked into
`config.json`.

### 3. Reduced Probe Interval (Testing)

Temporarily reduce `MINT_HEALTH_PROBE_INTERVAL_S` from 300 to 30 so health
probes actually fire during short board uptime windows.

### 4. Boot Sequence Stabilization

- 3-second delay before starting services after IP obtained (DNS stabilization)
- 5-second delay before initial health probes (DNS resolution readiness)

## Files Changed

| File | Change |
|------|--------|
| `main/config.h` | Add `wifi_auth_mode` field to `tollgate_config_t` |
| `main/config.c` | Parse `wifi_auth_mode` from config.json; use it in `tollgate_config_get_wifi()` |
| `main/mint_health.h` | Reduce probe interval 300 → 30 |
| `main/mint_health.c` | Add 5s DNS stabilization delay before initial probes |
| `main/tollgate_main.c` | Add 3s delay in services_start_task before starting services |
| `physical-router-test-automation/esp32/Makefile` | Add `detect-wpa-security`, `generate-spiffs`, `flash-spiffs-{a,b,c}` targets |

## Hardware Verification (Board A, 2026-05-19)

### STA Connectivity
- `STA auth threshold: WPA2 → 3` confirmed in serial log
- `Got IP:192.168.2.16, GW:192.168.2.1` — connected to home router via WPA2
- SNTP time sync started
- No lwip crashes

### Health Probes
- `Initial probe OK: https://mint.minibits.cash/Bitcoin (reachable)`
- `Initial probe OK: https://mint.coinos.io (reachable)`
- `Initial probe OK: https://21mint.me (reachable)`
- `Initial probe OK: https://mint.lnvoltz.com (reachable)`
- All 4 accepted mints confirmed reachable via `GET /v1/info`

### API Endpoints
- `GET /:2121` (discovery) — kind=10021, metric=milliseconds, only reachable mint in price_per_step tag
- `GET /mints` — 4 mints with boolean `reachable` field (3 false, 1 true initially)
- `GET /wallet` — balance=0, proof_count=0
- `GET /usage` — returns data
- `GET /whoami` — ip + mac

### Multi-Wallet
- 4/4 wallets initialized with real keysets from live mints
- Keyset load confirmed for minibits, coinos, 21mint, lnvoltz
- NVS save errors for some keysets (ESP_ERR_NVS_NOT_ENOUGH_SPACE) — non-critical

## Checklist

### Firmware Changes
- [x] Add `wifi_auth_mode` string field (16 bytes) to `tollgate_config_t` in `config.h`
- [x] Parse `wifi_auth_mode` from `config.json` in `config.c` with default `"WPA2"`
- [x] Map `wifi_auth_mode` string to `wifi_auth_mode_t` in `tollgate_config_get_wifi()`
- [x] Remove hardcoded `WIFI_AUTH_WPA3_PSK` at `config.c:322`
- [x] Reduce `MINT_HEALTH_PROBE_INTERVAL_S` from 300 to 30 in `mint_health.h`
- [x] Add boot sequence delays to prevent lwip crash

### Makefile Auto-Detect
- [x] Add `detect-wpa-security` target (nmcli scan → extract WPA mode for SSID)
- [x] Add `generate-spiffs` target (create config.json → spiffsgen.py)
- [x] Add `flash-spiffs-a`, `flash-spiffs-b`, `flash-spiffs-c` targets
- [ ] Wire `flash-{a,b,c}` to auto-generate SPIFFS before flashing (optional)

### Build & Test
- [x] Build firmware — `idf.py build` passes
- [x] Unit tests pass — 75/75 (61 + 14 mint_health)
- [x] Wait for board unlock (no force-unlock) — Board A was available
- [x] Lock board, flash firmware + SPIFFS
- [x] Verify STA connects via serial (`Got IP:192.168.2.16`)
- [x] Verify health probes fire and mints show `reachable: true`
- [x] Run API endpoint tests (discovery, mints, wallet, usage, whoami)
- [x] Run `make test-discovery-b`, `make test-mints-b`, `make test-multi-mint-b` — all pass
- [x] All 4 mints confirmed reachable via health probes on Board B
- [x] Discovery shows 4 `price_per_step` tags (one per reachable mint)
- [x] Wallet has 40 sats balance from previous payment (proofs stored in NVS)
- [ ] Test 6 previously-skipped scenarios (real payment, unreachable transition, etc.)

### Commit
- [x] Commit all changes with descriptive message (`2ad2ed4`)
- [ ] Push when Nostr relay recovers (relay.ngit.dev still down)

## Commits
- `b387982` wip: disable display for stability testing
- `d21fc93` docs: update WPA auto-detect plan with hardware verification results
- `2ad2ed4` feat: WPA auto-detect, STA connectivity fix, lwip crash fix (feature/multi-mint-support)
- `64e81b5` feat: WPA auto-detect SPIFFS generation + per-board flash targets (physical-router-test-automation)

## Remaining Work
1. Push commits when Nostr relay recovers
2. Test 6 skipped scenarios with stable board (reachable↔unreachable transitions, real payment, etc.)
3. Revert `MINT_HEALTH_PROBE_INTERVAL_S` from 30 to 300 before production
4. Address NVS `ESP_ERR_NVS_NOT_ENOUGH_SPACE` errors for keyset storage
5. Investigate display `ESP_ERR_NO_MEM` errors (307KB PSRAM framebuffer)
