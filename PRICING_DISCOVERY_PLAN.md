# Price Discovery — Two-Board Cross-Discovery Plan

## Goal

Get `test-price-discovery.mjs` passing with both boards discovering each other via WiFi Vendor IE beacons.

## Root Cause

Cross-discovery never worked because of two compounding issues:

1. **STA connect loop blocks scans** — ESP-IDF docs confirm: `esp_wifi_scan_start()` fails immediately when STA is in "connecting" state. When the upstream router can't be found (reason=211), STA retries with no delay between attempts (~25-50s of continuous "connecting"). Market scanner can never start a scan during this window.

2. **Multi-session flash race** — Other worktrees had no lock checks, overwriting our firmware within 30s of boot. Now fixed.

## Phases

### Phase 1: Firmware fixes (no hardware needed)

- [x] 1a. Add 2s delay before `esp_wifi_connect()` in disconnect handler (creates scan window)
- [x] 1b. Guard `WIFI_EVENT_STA_START` against double-connect during retry loop
- [x] 1c. Add `write-config-ap-only-a/b` Makefile targets (no `wifi_networks` in config)
- [x] 1d. Update `test-price-discovery.mjs` with correct Board B IP
- [x] 1e. Run `make test-unit` — must pass

### Phase 2: Flash and verify both boards (hardware needed)

- [x] 2a. Acquire locks on both boards (`make lock-a`, `make lock-b`)
- [x] 2b. Build firmware (`make build`)
- [x] 2c. Flash Board A with AP-only config (`make write-config-ap-only-a && make flash-a`)
- [x] 2d. Flash Board B with AP-only config (`make write-config-ap-only-b && make flash-b`)
- [x] 2e. Verify serial: both boards show AP services started, beacon IE injected, market initialized
- [x] 2f. Wait 60s for scan cycle, verify `GET /market` returns valid JSON on both boards

### Phase 3: Cross-discovery test (hardware needed)

- [x] 3a. Verify Board A `/market` shows Board B entry (BSSID `3A:2A:EB:C0:E9:CA`, SSID TollGate-C0E9CA)
- [x] 3b. Verify Board B `/market` shows Board A entry (BSSID `FE:08:F7:B9:6D:80`, SSID TollGate-B96D80)
- [x] 3c. Run `test-price-discovery.mjs` — 7/7 passed (per board, sequential due to single WiFi adapter)
- [x] 3d. Run `test-market.mjs` on both boards — 9/9 passed on both

### Phase 4: STA-connected test (stretch, hardware + upstream router needed)

- [ ] 4a. Flash with STA config (`write-config-a/b`)
- [ ] 4b. Verify STA connects to upstream router
- [ ] 4c. Verify cross-discovery still works with STA connected (background scan)
- [ ] 4d. Run full test suite

## Risk: vendor_ie_cb may not fire during scan

**CONFIRMED: vendor_ie_cb fires during passive scan.** No fallback needed.

Both boards successfully receive Vendor IEs from the other board's beacon frames during passive WiFi scan. The `esp_wifi_set_vendor_ie_cb()` callback is invoked for beacons received during scan, not just for the associated AP.

## Results

| Test | Result |
|------|--------|
| `make test-unit` | 17/17 passed |
| `idf.py build` | Pass |
| `test-market.mjs` Board A | 9/9 passed |
| `test-market.mjs` Board B | 9/9 passed |
| `test-price-discovery.mjs` Board A | 7/7 passed |
| `test-price-discovery.mjs` Board B | 7/7 passed |

### Cross-Discovery Data

| Board | Sees | BSSID | SSID | RSSI | Price |
|-------|------|-------|------|------|-------|
| A (10.185.47.1) | Board B | `3A:2A:EB:C0:E9:CA` | TollGate-C0E9CA | -30 | 21 sats/step |
| B (10.192.45.1) | Board A | `FE:08:F7:B9:6D:80` | TollGate-B96D80 | -25 | 21 sats/step |

## Key Technical Details

- **Vendor IE OUI:** `0xC0, 0xFF, 0xEE`
- **IE type:** `0x01`
- **Payload:** 26-byte packed struct (version, metric, price, step, mint_hash, geohash, npub_hash)
- **Scan config:** passive, 120ms/channel, all channels
- **Self-filter:** `npub_hash` comparison avoids processing own beacons
- **AP-only mode:** No `wifi_networks` in config → STA never connects → scans always work
