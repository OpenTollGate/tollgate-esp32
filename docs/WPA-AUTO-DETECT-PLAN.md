# WPA Auto-Detect + STA Connectivity Fix

## Problem

`config.c:322` hardcodes `WIFI_AUTH_WPA3_PSK` as the STA auth threshold. The home
router (`EnterSSID-2.4GHz`) uses **WPA2**, so the ESP32 silently refuses
association and never gets internet. This blocks health probes, real payments,
and all downstream testing.

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

## Files Changed

| File | Change |
|------|--------|
| `main/config.h` | Add `wifi_auth_mode` field to `tollgate_config_t` |
| `main/config.c` | Parse `wifi_auth_mode` from config.json; use it in `tollgate_config_get_wifi()` |
| `main/mint_health.h` | Reduce probe interval 300 → 30 |
| `physical-router-test-automation/esp32/Makefile` | Add `_detect-wpa-security`, `generate-spiffs`, `flash-spiffs-{a,b,c}` targets |

## Checklist

### Firmware Changes
- [ ] Add `wifi_auth_mode` string field (16 bytes) to `tollgate_config_t` in `config.h`
- [ ] Parse `wifi_auth_mode` from `config.json` in `config.c` with default `"WPA2"`
- [ ] Map `wifi_auth_mode` string to `wifi_auth_mode_t` in `tollgate_config_get_wifi()`
- [ ] Remove hardcoded `WIFI_AUTH_WPA3_PSK` at `config.c:322`
- [ ] Reduce `MINT_HEALTH_PROBE_INTERVAL_S` from 300 to 30 in `mint_health.h`

### Makefile Auto-Detect
- [ ] Add `_detect-wpa-security` target (nmcli scan → extract WPA mode for SSID)
- [ ] Add `generate-spiffs` target (create config.json → spiffsgen.py)
- [ ] Add `flash-spiffs-a`, `flash-spiffs-b`, `flash-spiffs-c` targets
- [ ] Wire `flash-{a,b,c}` to auto-generate SPIFFS before flashing

### Build & Test
- [ ] Build firmware — `idf.py build` passes
- [ ] Unit tests pass — `make test-unit`
- [ ] Wait for board unlock (no force-unlock)
- [ ] Lock board, flash firmware + SPIFFS
- [ ] Verify STA connects via serial (`Got IP:192.168.x.x`)
- [ ] Verify health probes fire and mints show `reachable: true`
- [ ] Run integration test suite
- [ ] Test 6 previously-skipped scenarios

### Commit
- [ ] Commit all changes with descriptive message
- [ ] Push when Nostr relay recovers
