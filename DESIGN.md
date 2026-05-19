# Design: WiFi Beacon Price Advertising via Vendor IE

## Problem

TollGate client mode (`tollgate_client.c`) can only discover upstream TollGate pricing **after** connecting to its WiFi and hitting `GET :2121/`. There is no way to compare prices of nearby TollGates before choosing which one to connect to. The Nostr-based wifistr approach requires internet access (expensive WebSocket connections to relays), creating a chicken-and-egg problem.

## Solution

Use IEEE 802.11 **Vendor-Specific Information Elements (IEs)** injected into WiFi beacon frames to broadcast price data over the air. Any nearby device can passively receive this data during a standard WiFi scan — no connection, no internet, no WebSocket required.

This approach is cross-platform compatible:
- **ESP32**: `esp_wifi_set_vendor_ie()` + `esp_wifi_set_vendor_ie_cb()` + `esp_wifi_scan_start()`
- **OpenWRT**: hostapd `vendor_elements` config parameter
- **Ubuntu/Linux**: `iw scan` / `nl80211` / `scapy`

## Architecture

### Wire Format (Phase 1: Binary Struct)

The vendor IE payload uses a fixed binary struct for maximum simplicity and portability.

```
Vendor IE (Element ID = 0xDD):
  vendor_oui[3]     = 0xC0, 0xFF, 0xEE   (OpenTollGate OUI, placeholder)
  vendor_oui_type   = 0x01               (Price Advertisement v1)
  payload:
    uint8_t  version            = 1       (protocol version)
    uint8_t  metric             = 0=milliseconds, 1=bytes
    uint16_t price_per_step               (sats, little-endian)
    uint32_t step_size                    (ms or bytes, little-endian)
    uint8_t  mint_hash[4]                (first 4 bytes of SHA-256(mint_url))
    uint8_t  geohash_len                  (0-9)
    char     geohash[9]                  (null-padded to 9 bytes)
    uint8_t  npub_hash[4]               (first 4 bytes of SHA-256(npub_hex))
```

Total payload: 1+1+2+4+4+1+9+4 = **26 bytes**. Well under the 255-byte vendor IE limit.

### Sender Flow

```
tollgate_main.c: start_services()
  → beacon_price_start()
    → Build tollgate_price_payload_t from config (price, step_size, metric, mint_url hash, geohash, npub hash)
    → Wrap in vendor_ie_data_t (element_id=0xDD, oui=0xC0FFEE, oui_type=0x01)
    → esp_wifi_set_vendor_ie(true, BEACON|PROBE_RESP, ID_0, &ie)
```

Every beacon frame (typically every 100ms) now carries the price data.

### Receiver Flow

```
tollgate_main.c: main loop
  → market_tick()
    → Every 30s: esp_wifi_scan_start(NULL, false)  // non-blocking all-channel scan
    → During scan, vendor IE callback fires for each received beacon:
      → Check OUI == 0xC0FFEE && oui_type == 0x01
      → Parse tollgate_price_payload_t
      → Store in market_entry_t indexed by BSSID
    → On scan complete event: esp_wifi_scan_get_ap_records() → correlate SSID/RSSI by BSSID
    → Sort entries by effective price
```

### Future Phases

- **Phase 2**: CBOR-encoded Nostr events in vendor IEs for cryptographic verification (BIP-340 Schnorr signatures on price data)
- **Phase 3**: Nostr relay subscription for wide-area market discovery (requires internet)
- **Phase 4**: `client_auto_switch` — automatically disconnect from expensive upstream and reconnect to cheapest

## Cross-Platform Reference

### OpenWRT / hostapd (Sender)

```uci
config wifi-iface 'tollgate'
    option vendor_elements 'dd20c0ffee0101001...hex...'
```

Where the hex is the binary payload encoded as hex string.

### Linux (Receiver)

```bash
iw dev wlan0 scan | grep -A1 "Vendor specific"
# Or: scapy/python with Dot11Beacon parsing
```

---

## Implementation Checklist

### Phase 1: Vendor IE Transmitter
- [x] Create `main/beacon_price.h` — payload struct, API declarations
- [x] Create `main/beacon_price.c` — `beacon_price_start()`, `beacon_price_stop()`
- [x] Compute `mint_hash` and `npub_hash` using SHA-256

### Phase 2: Vendor IE Receiver + Market Scanner
- [x] Create `main/market.h` — `market_entry_t`, `market_t`, API declarations
- [x] Create `main/market.c` — vendor IE callback, scan trigger, entry storage, ranking
- [x] BSSID correlation between vendor IE callback and scan results

### Phase 3: Config Additions
- [x] Add `market_enabled`, `market_scan_interval_s`, `client_auto_switch` to `config.h`
- [x] Parse new fields from config.json in `config.c`

### Phase 4: Main Loop Integration
- [x] Call `beacon_price_start()` / `beacon_price_stop()` in `tollgate_main.c`
- [x] Call `market_init()` in `start_services()`
- [x] Call `market_tick()` in main loop
- [x] Add `beacon_price.c` and `market.c` to `CMakeLists.txt`

### Phase 5: Client Market Consultation
- [x] In `tollgate_client.c`, log price comparison when connecting to upstream
- [x] Warn if cheaper alternative exists in market snapshot

### Phase 6: API Endpoint
- [x] Add `GET /market` handler in `tollgate_api.c`
- [x] Return JSON array of discovered TollGates with prices

### Phase 7: Unit Tests
- [x] `tests/unit/test_beacon_price.c` — encode/decode roundtrip, struct packing
- [x] `tests/unit/test_market.c` — ranking, geohash filtering, entry management

### Phase 8: Integration Tests
- [x] `tests/integration/test-market.mjs` — GET /market endpoint validation
- [x] `tests/integration/test-price-discovery.mjs` — two-board price discovery
- [x] Add Makefile targets for new tests

### Phase 9: ESP-IDF Build
- [x] Fix format specifiers (`%u` → `%lu` + cast for `uint32_t` on xtensa)
- [x] Copy local-only files (`display.c/h`, `font.c/h`) to worktree
- [x] Apply nucula `save_proofs()` private→public patch
- [x] `idf.py build` succeeds
- [x] Symlink missing components (`axs15231b`, `qrcode`) from main repo

### Phase 10: Hardware Mutex (per-board, shared across worktrees)
- [x] Rewrite Makefile with per-board locks (`lock-a`, `lock-b`, `unlock-a`, `unlock-b`)
- [x] Shared `LOCK_DIR` at `/home/c03rad0r/physical-router-test-automation/locks`
- [x] `require_lock_a` / `require_lock_b` macros
- [x] `acquire_lock` function macro (same pattern as `physical-router-test-automation/esp32/Makefile`)
- [x] Per-board flash/monitor/reset/serial-log/erase-nvs targets
- [x] `connect-a` / `connect-b` / `disconnect` WiFi targets
- [x] `_connect-a-if-needed` / `_connect-b-if-needed` auto-connect helpers
- [x] All integration tests require `lock-a` + `_connect-a-if-needed`
- [x] `test-price-discovery` requires both `lock-a` AND `lock-b`
- [x] Board port mapping matches `boards.env` (A=ACM1, B=ACM2)
- [x] Remove old single-lock `hardware.lock` from `.gitignore`

### Phase 11: Debugging & Hardening
- [x] Add WiFi disconnect reason code to log output (`tollgate_main.c:58`)
- [x] Add `esp_wifi_set_country_code("DE")` — was missing, defaults to CN
- [x] Commit both fixes to `feature/price-discovery`
- [x] Document findings in `SESSION_NOTES.md`

### Final
- [x] `make test-unit` passes (all 13 test suites, 45 new assertions)
- [x] `idf.py build` passes (ESP32-S3 firmware)
- [x] Commit to `feature/price-discovery` branch
- [x] Hardware flash + integration test on Board B (`test-market`: 4 passed, 0 failed)
- [x] Hardware flash + integration test on Board A (`test-market`: 4 passed, 0 failed)
- [ ] Two-board price discovery test (`test-price-discovery`) — blocked by WiFi STA issue (reason=211 NO_AP_FOUND)
- [ ] Merge to master

## Blockers

### WiFi STA Connectivity (reason=211)
Both boards fail to find `EnterSSID-2.4GHz` during STA scan despite the router being visible from the laptop at 100% signal. Root cause unclear — may be RF/environmental, APSTA co-channel limitation, or ESP32 scan sensitivity issue. Board B obtained STA IP once during testing, proving the firmware code is correct. See `SESSION_NOTES.md` for detailed analysis.

### Multi-Session Hardware Conflict
Other LLM sessions (`esp32-tollgate`, `esp32-tollgate-arch`, `esp32-tollgate-display`) flash boards concurrently without respecting the lock system, overwriting our firmware within seconds of flashing.
