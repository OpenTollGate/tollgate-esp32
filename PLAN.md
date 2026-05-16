# TollGate ESP32 — Test-Driven Development Plan

## Overview

Build a TollGate firmware for two ESP32 devices, following the [TollGate protocol spec](https://github.com/OpenTollGate/tollgate) (TIP-01, TIP-02, HTTP-01/02/03). The implementation uses ESP-IDF (C/C++) with an on-device Cashu wallet using mbedTLS secp256k1.

## Architecture Decision: C/C++ (ESP-IDF)

- Existing working captive portal is in C (ESP-IDF)
- On-device Cashu wallet uses nucula library (libsecp256k1)
- ESP-IDF is already installed at `~/esp/esp-idf`
- No Rust/ESP32 toolchain installed
- Nostr keypair as root identity — derive AP MAC, SSID, IP from nsec

## Technology Stack

| Layer | Technology |
|-------|-----------|
| Framework | ESP-IDF v5.4.1 (C/C++) |
| Identity | Nostr nsec → HMAC-SHA512 derivation → MAC/SSID/IP; Schnorr signing for Nostr events |
| Cashu wallet | nucula library (libsecp256k1, NVS persistence) |
| Service discovery | wifistr (Nostr kind 38787) via WebSocket to relays |
| HTTP server | `esp_http_server` (port 80 captive portal, port 2121 TollGate API + wallet) |
| DNS | Custom UDP task (hijack unauthenticated, forward authenticated) |
| NAT | lwIP NAPT |
| Persistence | NVS (nucula built-in) for wallet; SPIFFS for config.json |
| Testing | Playwright + curl + nutshell CLI |
| Build | Makefile |

## Four-Phase Plan

### Phase 1: Captive Portal + Firewall (No Payments) — COMPLETE

**Goal:** WiFi repeater with captive portal that gates internet access. Validates DNS hijack, NAT, DHCP, firewall.

**Endpoints:**
- `GET /whoami` — returns client MAC
- `GET /usage` — returns `-1/-1`
- Captive portal HTML on port 80

**14 Test Cases:**
| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 1 | Boot and AP appears | Serial + nmcli | SSID visible in scan | PASS |
| 2 | DHCP lease | nmcli connect | Gets IP in 192.168.4.0/24 | PASS |
| 3 | Captive portal serves HTML | GET / | 200, contains "TollGate" | PASS |
| 4 | Captive detection URIs work | GET /generate_204 etc. | All return portal HTML | PASS |
| 5 | DNS hijack before auth | nslookup google.com | Resolves to 192.168.4.1 | PASS |
| 6 | No internet before auth | ping 8.8.8.8 | Fails | PASS |
| 7 | /whoami returns MAC | GET /whoami | Returns mac=XX:XX:... | PASS |
| 8 | /usage returns no session | GET /usage | Returns -1/-1 | PASS |
| 9 | Grant access via API | GET /grant_access | 200, status granted | PASS |
| 10 | DNS forward after auth | nslookup google.com | Resolves to real IP | PASS |
| 11 | Internet after auth | ping 8.8.8.8 | Succeeds | PASS |
| 12 | HTTP browsing works | Playwright | Page loads | PASS |
| 13 | Reset auth | GET /reset_authentication | 200 | PASS |
| 14 | Internet blocked after reset | ping 8.8.8.8 | Fails | PASS |

### Phase 2: E-Cash Payments — COMPLETE

**Goal:** Replace free access with Cashu payment. ESP32 parses token, checks proof state via mint API, grants time-based session.

**Endpoints:**
- `GET /` on :2121 — TollGate advertisement (kind=10021)
- `POST /` on :2121 — Accept Cashu token, validate, return session (kind=1022) or error (kind=21023)
- `GET /usage` on :2121 — Session usage info
- `GET /whoami` on :2121 — Client IP + MAC

**13 Additional Test Cases:**
| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 15 | Advertisement valid | GET :2121/ | kind=10021 with price_per_step | PASS |
| 16 | Valid payment | POST :2121/ with token | kind=1022 session | PASS |
| 17 | Usage tracking | GET :2121/usage | 0/allotment | PASS |
| 18 | Internet after payment | ping | Succeeds | PASS |
| 19 | Invalid token | POST :2121/ garbage | kind=21023 error | PASS |
| 20 | Spent token | Reuse token | kind=21023 spent error | PASS |
| 21 | Wrong mint | Token from unaccepted mint | kind=21023 mint error | PASS |
| 22 | Session expiry | Wait for allotment | Internet blocked | PASS |
| 23 | Session renewal | Second payment | Allotment extended | PASS |
| 24 | Portal payment form | Playwright paste token | Checkmark shown | PASS |
| 25 | Two clients pay independently | Two POSTs | Both authenticated | Phase 3 |
| 26 | Client isolation | Only payer gets internet | Non-payer blocked | Phase 3 |
| 27 | Full e2e: portal→pay→browse | Playwright | Complete flow | Phase 3 |

**Captive Portal Detection:** DoT reject server on port 853, NXDOMAIN for non-A queries, 302 redirects for captive URIs. Verified working on GrapheneOS (commit `236b61d`).

### Phase 3: On-Device Wallet + Nostr Identity + Wifistr — IN PROGRESS

**Goal:** On-device Cashu wallet using [nucula](https://github.com/zeugmaster/nucula) library (libsecp256k1). Nostr keypair as root identity — derive AP MAC, SSID, IP deterministically. Publish service via wifistr (Nostr kind 38787).

#### Wallet Architecture — nucula Integration

**Decision: Use nucula as a git submodule instead of custom mbedTLS wallet.**

Why nucula over our custom mbedTLS wallet:
- **libsecp256k1** vs mbedTLS ECP: purpose-built C library with precomputed tables, ~10x less stack usage, no stack overflow
- **Production-quality**: NUT-00 through NUT-13, DLEQ verification, P2PK, deterministic secrets (BIP-39)
- **No maintenance burden**: upstream at `zeugmaster/nucula`, pull updates via `git submodule update`
- **NVS persistence**: more reliable than SPIFFS, no wear-leveling concerns

Integration structure:
```
esp32-tollgate/
├── components/
│   ├── nucula_src/          # git submodule → zeugmaster/nucula
│   ├── secp256k1/           # copied from nucula_src/components/secp256k1/
│   └── nucula_lib/          # wrapper component
│       ├── CMakeLists.txt   # compiles nucula sources from ../nucula_src/main/
│       ├── nucula_wallet.h  # C API for TollGate
│       └── nucula_wallet.cpp # C++ bridge → nucula::Wallet
├── main/
│   ├── wallet.c             # REMOVED
│   ├── wallet_persist.c     # REMOVED
│   ├── cashu.c              # simplified (token decode delegates to nucula)
│   ├── tollgate_api.c       # updated to use nucula_wallet.h
```

Files compiled from nucula (via `../nucula_src/main/`):
- `crypto.c` — hash_to_curve, blind_message, unblind, DLEQ verification
- `wallet.cpp` — full Cashu wallet (swap, receive, send, mint, melt)
- `cashu_json.cpp` — JSON serialization (cJSON-based)
- `nut10.cpp` — NUT-10 structured secret parsing
- `hex.c` — hex encode/decode
- `http.c` — HTTP client wrapper (uses esp_http_client)

NOT compiled (TollGate doesn't need them):
- `nucula.cpp` — nucula's own app_main
- `cashu_cbor.cpp` — CBOR/V4 token support (we only use V3/cashuA)
- `console.cpp`, `display.cpp`, `nfc.cpp`, `ndef.cpp`, `keypad.c` — hardware UI
- `bip39.c` — mnemonic generation (we use random secrets)
- `wifi.c` — nucula's own WiFi manager
- `crypto_test.c` — test code

#### Wallet Endpoints (on :2121)

| Method | Path | Description |
|--------|------|-------------|
| GET | /wallet | Balance, proof count, keyset count |
| POST | /wallet/swap | Swap all proofs for fresh ones via mint |
| POST | /wallet/send | Create cashuA token for given amount (body = sat count) |

#### Payment Integration

Received payment proofs are automatically added to wallet after session creation in `tollgate_api.c`.

#### Persistence Threshold

Config parameter `persist_threshold_sats` (default: 1) controls when wallet state is written to flash:
- `balance >= persist_threshold_sats` → write wallet.json
- `balance < threshold` → skip write (or delete existing file)
- Rationale: flash has finite write cycles (~100K erase per sector); only persist when e-cash value justifies the wear cost
- SPIFFS wear-leveling spreads writes across the 960K partition

#### C API Bridge (`nucula_wallet.h`)

The TollGate firmware is C; nucula is C++. A thin C bridge exposes the wallet operations needed:

```c
// Initialize wallet with secp256k1 context and mint URL
esp_err_t nucula_wallet_init(const char *mint_url);

// Receive a cashuA token string into wallet (swap + store proofs)
esp_err_t nucula_wallet_receive(const char *token_str);

// Create a cashuA token for the given amount
esp_err_t nucula_wallet_send(uint64_t amount_sat, char *token_out, size_t token_out_size);

// Get current balance in sats
uint64_t nucula_wallet_balance(void);

// Get proof count
int nucula_wallet_proof_count(void);

// Get JSON array of proofs (for /wallet endpoint)
char *nucula_wallet_proofs_json(void);

// Swap all proofs for fresh ones
esp_err_t nucula_wallet_swap_all(void);

// Print wallet status to log
void nucula_wallet_print_status(void);
```

#### Persistence

nucula uses NVS (Non-Volatile Storage) for persistence — proofs stored as JSON blobs in flash, keysets stored individually. This is more reliable than SPIFFS:
- No filesystem overhead
- Atomic writes via NVS key-value API
- Wear leveling handled by NVS internally
- No `persist_threshold_sats` needed — NVS handles flash wear automatically

#### Nostr Identity Derivation

**Root of trust:** A Nostr private key (`nsec`, 32 bytes hex) stored in `config.json`. All device identifiers are deterministically derived from this single key. Rotating nsec rotates the entire identity (MAC, SSID, IP, Nostr pubkey).

**Derivation function: `tollgate_derive()`**

Simplified HMAC-SHA512 derivation (not full BIP85 — ~50 lines, same security model):
```
tollgate_derive(nsec_bytes, label, index) → bytes
  HMAC-SHA512(key=nsec_bytes, msg=label || uint32_le(index))
  truncate output to needed length
```

**Derived values:**

| Value | Derivation | Output |
|-------|-----------|--------|
| npub | `secp256k1_ec_pubkey_create(nsec)` → x-only pubkey | 32 bytes hex |
| STA MAC | `tollgate_derive(nsec, "sta-mac", 0)` | 6 bytes, `byte[0] \|= 0x02` |
| AP MAC | `tollgate_derive(nsec, "ap-mac", 0)` | 6 bytes, `byte[0] \|= 0x02` |
| SSID | `"TollGate-" + hex(AP_MAC[3:6])` | last 3 bytes = 6 hex chars |
| AP IP | `10.(AP_MAC[3]).((AP_MAC[4]^AP_MAC[5])%200+10).1` | hash-based from AP MAC |

**Implementation: `identity.c/h`**

- Uses `mbedtls/md.h` for HMAC-SHA512 (already linked)
- Uses `secp256k1.h` + `secp256k1_extrakeys.h` from the secp256k1 component
- Creates its own `secp256k1_context` (SIGN only) — destroyed after init
- `identity_init(nsec_hex)` called before WiFi start in `app_main()`
- Sets derived MACs via `esp_wifi_set_mac(WIFI_IF_STA/AP, mac)` after `esp_wifi_init()`

**Boot sequence:**
```
nvs_flash_init()
  → tollgate_config_init()        // loads config.json with nsec
  → identity_init(nsec)           // derives npub, MACs, SSID, IP
  → esp_netif_init()
  → esp_event_loop_create_default()
  → wifi_init_sta()
  → wifi_create_ap_netif()        // uses derived AP IP
  → esp_wifi_init(&cfg)
  → esp_wifi_set_mac(STA/AP)      // sets derived MACs
  → wifi_configure_ap()           // uses derived SSID
  → esp_wifi_start()
```

**Config.json format (new):**
```json
{
  "nsec": "hex_64_chars",
  "wifi_networks": [{"ssid":"...", "password":"..."}],
  "ap_password": "",
  "mint_url": "https://testnut.cashu.space",
  "price_per_step": 21,
  "step_size_ms": 60000,
  "nostr_geohash": "u281w0dfz",
  "nostr_relays": ["wss://relay.damus.io", "wss://nos.lol"],
  "nostr_publish_interval_s": 21600
}
```

Removed from config: `ap_ssid`, `ap_ip`, `ap_channel`, `ap_max_conn` (all derived or hardcoded).

#### Nostr Event Signing (`nostr_event.c/h`)

NIP-01 event serialization and Schnorr signing:
- Canonical JSON: `[0, pubkey, created_at, kind, tags, content]`
- Event ID: SHA-256 of canonical JSON serialization
- Signature: `secp256k1_schnorrsig_sign32()` (BIP-340)
- Uses own `secp256k1_context` (created on demand, destroyed after use)

#### Wifistr Service Discovery (`wifistr.c/h`)

Publishes TollGate node to Nostr as kind 38787 (wifistr):
- Tags: `["d", npub]`, `["ssid", ssid]`, `["h", "cashu-testnut"]`, `["security", "open"]`, `["g", geohash]`, `["c", "cashu"]`
- Content: human-readable description with price info
- Publishes on boot + periodic timer (default 6 hours)
- WebSocket client for relay communication (raw TCP + TLS + HTTP Upgrade)
- Uses `esp_tls.h` for TLS connections to `wss://` relays

#### Test Cases

| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 28 | Wallet boot | Serial | Keysets loaded | TODO |
| 29 | Receive via wallet | POST :2121/ | Balance incremented | TODO |
| 30 | Wallet swap | POST /wallet/swap | Same balance, new proofs | TODO |
| 31 | Wallet send | POST /wallet/send | Valid cashuA token returned | TODO |
| 32 | Persistence survives reboot | Reboot + GET /wallet | Same balance | TODO |
| 33 | Cross-board payment | B sends → A receives | A balance increases | TODO |
| 34 | Two clients pay independently | Two POSTs | Both authenticated | TODO |
| 35 | Client isolation | Only payer gets internet | Non-payer blocked | TODO |
| 36 | Full e2e: portal→pay→browse | Playwright | Complete flow | TODO |
| 37 | 5 consecutive payments | Loop | All authenticated | TODO |
| 38 | Stress: rapid pay/expire | Loop with short sessions | No crash/leak | TODO |

### Phase 4: Mesh Service Discovery + ESP32-to-OpenWRT Interop — NOT STARTED

**Goal:** Two capabilities: (1) Pre-association price discovery between mesh nodes using Wi-Fi Vendor IE beacons, (2) ESP32-to-OpenWRT TollGate interoperability with Cashu tokens.

#### 4A: Pre-Association Service Discovery via Vendor IE Beacons

**Problem:** In a tollgate mesh network, a client router needs to know an upstream gateway's price before investing in Wi-Fi connection setup/teardown. Standard 802.11u ANQP is not supported by ESP-IDF.

**Solution: Vendor-Specific Information Elements in Beacon/Probe Response frames**

ESP-IDF provides `esp_wifi_set_vendor_ie()` to inject custom data into 802.11 management frames. This allows passive price discovery during normal Wi-Fi scanning — no connection required.

```
┌─────────────────────────────────────────────────────────────┐
│                 Layer 2 (Pre-Association)                    │
│                                                              │
│  Gateway AP broadcasts price in every Beacon (~100ms)       │
│  Client STA scans, reads price from beacon before connect   │
│                                                              │
│  ┌─────────────┐                     ┌─────────────┐       │
│  │ Gateway AP  │  Beacon ──────────► │ Client STA  │       │
│  │             │  (with price IE)    │             │       │
│  │ Vendor IE:  │                     │ Scan result │       │
│  │ OUI:TG      │                     │ includes    │       │
│  │ price/sats  │                     │ price data  │       │
│  │ step_ms     │                     └──────┬──────┘       │
│  │ mint_url    │                            │              │
│  └─────────────┘                     Decision: connect?     │
│                                              │              │
└──────────────────────────────────────────────┼──────────────┘
                                               │
                              ┌────────────────▼──────────────┐
                              │       Layer 3+ (Connected)    │
                              │  POST / with Cashu token      │
                              └───────────────────────────────┘
```

**Beacon IE Payload Format (Vendor-Specific, Element ID 0xDD):**

```
┌──────────┬────────┬─────────────┬──────────────┬──────────────────┐
│ element_id│ length │ vendor_oui  │ oui_type     │ payload          │
│ (0xDD)    │        │ (3 bytes)   │ (1 byte)     │ (variable)       │
├──────────┼────────┼─────────────┼──────────────┼──────────────────┤
│ 0xDD     │ N      │ "TG"        │ 0x01 (price) │ See below        │
│          │        │ 0x54:0x47    │              │                  │
└──────────┴────────┴─────────────┴──────────────┴──────────────────┘

Price Payload (oui_type 0x01):
┌─────────────┬─────────────┬──────────────┬───────────────┬────────────┐
│ version (1B)│ price (2B)  │ step_ms (2B) │ fee_ppk (2B)  │ hop_count │
│ = 0x01      │ sat/step    │ ms/step      │ or 0          │ (1B)       │
├─────────────┼─────────────┼──────────────┼───────────────┼────────────┤
│ 0x01        │ uint16_le   │ uint16_le    │ uint16_le     │ uint8      │
└─────────────┴─────────────┴──────────────┴───────────────┴────────────┘
Total payload: 9 bytes (fits easily in beacon, typical budget ~200 bytes)
```

**Implementation:**

**AP Side (Gateway — `beacon_price.c/h`):**
- `beacon_price_start()` — calls `esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, &ie_data)` and also for `WIFI_VND_IE_TYPE_PROBE_RESP`
- `beacon_price_update(uint16_t price_sat, uint16_t step_ms, uint16_t fee_ppk, uint8_t hop_count)` — dynamically updates the IE in-place (no reconnect, no user kick; next beacon frame carries new price)
- Price derived from `tollgate_config_t` fields (`price_per_step`, `step_size_ms`)
- Can be called on-the-fly when market conditions change (e.g., upstream price changes)

**STA Side (Client — `beacon_scan.c/h`):**
- `beacon_scan_prices(wifi_ap_record_t *aps, int count, tollgate_price_t *prices, int *price_count)` — given scan results, extract price IEs
- Uses `esp_wifi_set_vendor_ie_cb()` to register a callback that fires during scan
- Or parses `vendor_ie_data_t` from scan results if available in `wifi_ap_record_t`
- Returns array of `{bssid, ssid, price_sat, step_ms, fee_ppk, hop_count}`
- Client selects cheapest/upstream gateway from scan results before connecting

**Integration with existing config:**
- OUI: `0x54, 0x47` ("TG" in ASCII) — unique to TollGate
- oui_type: `0x01` = price advertisement, `0x02` = mesh routing (future)
- `hop_count`: indicates network depth (0 = directly connected to internet, 1 = one hop away)
- Price updates are rate-limited to once per 5 seconds to avoid beacon churn

**GL-MT3000 (OpenWrt) Compatibility:**
- OpenWrt supports vendor IEs via `hostapd_cli -i wlan0 set vendor_elements <hex>` + `hostapd_cli -i wlan0 update_beacon`
- Client scans via `iw dev wlan0 scan` show vendor elements
- Requires stock OpenWrt 24 firmware (not GL.iNet default) for mac80211 driver access
- Same OUI/payload format ensures ESP32 ↔ OpenWrt interop

**Key Benefits:**
- Zero connection overhead for price discovery
- Works during normal passive/active scanning (no extra frames)
- Prices update live without disconnecting clients
- Supports multi-hop mesh routing via `hop_count`
- Compatible with both ESP32 and Linux (OpenWrt) platforms

#### 4B: ESP32-to-OpenWRT TollGate Interop

**Goal:** ESP32 can pay OpenWRT TollGate using Cashu tokens. Full interoperability with existing OpenWRT-based TollGate infrastructure.

## Total: 38 Tests across 4 phases

## Key Technical Notes

### nucula / libsecp256k1
- nucula uses **libsecp256k1** (Bitcoin Core's C library) for all curve operations
- Stack-efficient: precomputed tables in `precomputed_ecmult.c` (compile-time), small runtime stack
- No `mbedtls_ecp_mul` → no stack overflow — runs fine on default 32K httpd task
- ESP-IDF component at `components/secp256k1/` with `ECMULT_WINDOW_SIZE=8`, `ECMULT_GEN_PREC_BITS=4`
- git submodule at `components/nucula_src/` — pull updates via `git submodule update --remote`

### Token Format
- TollGate uses **cashuA (V3)** tokens — base64url-encoded JSON
- nucula's `deserialize_token_v3()` / `serialize_token_v3()` handle encoding
- cashuB (V4/CBOR) not needed; CBOR dependency excluded from build

### Vendor IE Beacon (Service Discovery)
- ESP-IDF: `esp_wifi_set_vendor_ie(enable, type, idx, data)` — injects into Beacon/ProbeResp
- `esp_wifi_set_vendor_ie_cb(cb, ctx)` — receives vendor IEs during scan
- Element ID 0xDD (Vendor Specific), max ~200 bytes per IE
- Updates are in-place in RAM; next beacon carries new data (~100ms interval)
- No client disconnect or AP restart required for updates
- OUI `0x54:0x47` ("TG") registered for TollGate protocol

### Board Configuration
- Board A: `/dev/ttyACM0`, factory MAC `94:a9:90:2e:37:7c`
- Board B: `/dev/ttyACM1`, factory MAC `fc:01:2c:c5:50:50`
- Both boards run identical firmware; unique identity derived from nsec in config.json
- SSID, AP IP, STA/AP MAC all derived from nsec via HMAC-SHA512

### Test Mint
- `testnut.cashu.space` — auto-pays lightning invoices for testing
- `cashu -h https://testnut.cashu.space invoice <amount>` → auto-paid
- `cashu -h https://testnut.cashu.space send --legacy <amount>` → generates cashuA token
