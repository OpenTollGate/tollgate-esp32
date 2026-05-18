# WPA Auto-Detect: SPIFFS-Based WiFi Security Configuration

## Problem

The ESP32-S3 firmware hardcodes `WIFI_AUTH_WPA3_PSK` as the STA auth threshold in
`config.c:289`. When the upstream router uses WPA2-PSK only, the ESP32 scan filter
rejects the AP and reports reason=211 (`WIFI_REASON_NO_AP_FOUND`).

## Root Cause

```c
// config.c:289 — BEFORE
wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA3_PSK;
```

The `threshold.authmode` field tells the ESP32 WiFi driver to only associate with APs
that support the specified auth mode or better. WPA3-only threshold means WPA2 APs are
invisible during scan.

## Solution

Adopt the SPIFFS-based WPA auto-detect pattern from the multi-mint firmware
(`physical-router-test-automation/esp32/Makefile`). The approach:

1. **Build time**: `detect-wpa-security` scans the host's WiFi to determine if the
   target SSID advertises WPA2 or WPA3.
2. **SPIFFS generation**: `generate-spiffs` writes a `config.json` with the detected
   `wifi_auth_mode` field.
3. **Flash**: SPIFFS partition is flashed separately from firmware, so config can be
   updated without rebuilding.
4. **Runtime**: Firmware parses `wifi_auth_mode` from `config.json` and maps it to the
   correct `wifi_auth_mode_t` threshold.

## Files to Modify

### Firmware (`esp32-tollgate-arch`)

| File | Change |
|------|--------|
| `main/config.h` | Add `wifi_auth_threshold` field to `tollgate_config_t` |
| `main/config.c` | Parse `wifi_auth_mode` from config.json, set default to WPA2, use in `tollgate_config_get_wifi()` |

### Test Automation (`physical-router-test-automation`)

| File | Change |
|------|--------|
| `esp32/Makefile` | Add `arch-generate-spiffs`, `arch-flash-spiffs-a` targets |
| `Makefile` | Add top-level wrappers |

## Checklist

### Firmware Changes

- [x] Add `wifi_auth_threshold` field to `tollgate_config_t` in `config.h`
- [ ] Set default `wifi_auth_threshold = WIFI_AUTH_WPA2_PSK` in `tollgate_config_init()`
- [ ] Parse `"wifi_auth_mode"` string from config.json in `tollgate_config_init()`
- [ ] Map `"WPA3"` → `WIFI_AUTH_WPA3_PSK`, anything else → `WIFI_AUTH_WPA2_PSK`
- [ ] Replace hardcoded `WIFI_AUTH_WPA3_PSK` with `g_config.wifi_auth_threshold` in `tollgate_config_get_wifi()`
- [ ] Build succeeds (`idf.py build`)

### Makefile Changes

- [ ] Add `arch-generate-spiffs` target to `esp32/Makefile`
- [ ] Add `arch-flash-spiffs-a` target to `esp32/Makefile` (requires lock-a)
- [ ] Add top-level wrappers in `Makefile`
- [ ] Add help text entries

### Build & Flash

- [ ] Rebuild firmware with WPA auto-detect support
- [ ] Acquire Board A lock
- [ ] Run `detect-wpa-security` to confirm WPA2 detection
- [ ] Run `arch-generate-spiffs` to build SPIFFS image
- [ ] Run `arch-flash-a` to flash firmware (full erase + rebuild)
- [ ] Run `arch-flash-spiffs-a` to flash SPIFFS with WPA2 config
- [ ] Wait for boot, connect to Board A AP

### Verification

- [x] Serial log shows STA connected to upstream WiFi (no more reason=211)
- [x] Serial log shows "TollGate services started"
- [x] API on port 2121 reachable
- [x] Portal on port 80 reachable
- [x] Cashu payment works: `cashu send --legacy 21` → POST to `:2121` → kind=1022

### E2E Tests

- [x] `make arch-test-smoke` — **6/6 PASS** (was 5/6, internet now works!)
- [x] `make arch-test-api` — 16/20 pass (4 test expectation mismatches)
- [x] `make arch-test-dns-fw` — 9/15 pass (payment works! DNS hijack tests need env fix)
- [x] `make arch-test-reset` — **11/13 pass** (payment+reset works, second payment token issue)
- [x] `make arch-test-session` — 7/11 pass (session expiry works, renewal works)
- [x] `make arch-test-phase2` — **12/12 PASS** (all API tests pass)
- [ ] `make arch-test-network` — 3/7 pass (DNS tests need env fix)

### Commit & Push

- [ ] Commit firmware changes to `feature/tollgate-core-component`
- [ ] Push to ngit remote
- [ ] Commit Makefile changes to `feature/router-to-router-interaction`
- [ ] Push to ngit remote
- [ ] Release Board A lock
