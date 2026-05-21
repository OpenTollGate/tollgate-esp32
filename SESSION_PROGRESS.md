# Session Progress — TLS Fix & Payment Flow

## Completed

- [x] **TLS allocation fix**: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (was 16384) — SSL buffers now allocate from PSRAM
- [x] **Dynamic SSL buffers**: `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`
- [x] **Stack overflow fix**: `start_services()` moved from `esp_timer` (2KB stack) to FreeRTOS task (16KB)
- [x] **DNS server bind fix**: Binds to AP IP only, prevents self-hijacking
- [x] **Mint reachable**: `testnut-nutshell.mints.orangesync.tech` — status 200
- [x] **Payment accepted**: 42 sats → 120s session (POST raw token body)
- [x] **Unit tests**: 407/407 pass
- [x] **Committed + pushed**: `716daaf`

## Remaining Checklist

### Phase 1: Stabilization

- [x] **1.1 Fix payment body format**: Portal already sends raw token body — no fix needed. Curl tests just needed `-d "$TOKEN"` without `token=` prefix.
- [x] **1.2 Clean up debug code**: Removed DNS resolve test from debug endpoint, reduced mint_health logging to DEBUG level.
- [x] **1.3 Align mint URL everywhere**: Updated to `testnut-nutshell.mints.orangesync.tech` in config.c, tollgate_platform.c, .env, AGENTS.md, pytest boards.py, test_captive_portal.py.
- [x] **1.4 Flash Board A**: Flashed with TLS/stack/AP-gateway fixes + new SPIFFS config. Mint reachable, internet routing works.
- [x] **1.5 Run pytest suite on Board C**: 27/28 pass. `test_spend_from_funded_wallet` needs redesign (wallet doesn't accumulate from external payments — proofs are consumed).
- [x] **1.6 Fix AP gateway for DHCP**: Restored `ap_gw = ap_ip` so clients get default route. STA default route still handles outbound traffic.

### Phase 2: Remaining Work

- [ ] **2.1 Fix `test_spend_from_funded_wallet`**: Test assumes wallet accumulates balance from token payments, but the current flow consumes proofs during verification. Needs wallet receive endpoint or test redesign.
- [ ] **2.2 Playwright E2E**: Test captive portal payment flow in browser.
- [ ] **2.3 Board A full pytest**: Run full suite on Board A.

## Key Technical Context

- **TLS root cause**: mbedtls `SSL_IN_CONTENT_LEN=16384` couldn't allocate from internal RAM (largest block 8KB). Fix: lower `SPIRAM_MALLOC_ALWAYSINTERNAL` to 4KB.
- **Stack overflow**: `esp_timer` task has ~2KB stack. `start_services()` needs much more (TLS init, wallet, DNS, etc). Fix: dedicated `xTaskCreate` with 16KB stack.
- **Board C**: nsec `71bf3f4dab5eb791c35bbc84d86c0418d3a8a646284c1c309a0009ab8245be1d`, port `/dev/ttyACM2`, SSID `TollGate-4A2510`, IP `10.74.63.1`
- **Board A**: nsec `9af47906b45aca5e238390f3d03c8274e154198e81aa2095065627d1e61ca968`, port `/dev/ttyACM0`, SSID `TollGate-B96D80`
- **Test mint**: `testnut-nutshell.mints.orangesync.tech` (Nutshell/0.20.0, works with cashu CLI)
- **Payment format**: Raw token body (`Content-Type: text/plain`), NOT form-encoded
