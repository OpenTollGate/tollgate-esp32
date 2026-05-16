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

### Phase 4: ESP32 TollGate Client Detection + Auto-Payment — IN PROGRESS

**Goal:** ESP32 detects upstream TollGate when connected as STA, automatically pays for internet access using on-device wallet. Enables ESP32→OpenWRT (Scenario 4) and ESP32→ESP32 (Scenario 5) auto-payment.

**New files:** `main/tollgate_client.c`, `main/tollgate_client.h`

#### Architecture

The ESP32 already runs `WIFI_MODE_APSTA` — STA connects to upstream WiFi. When STA gets an IP, the client module:
1. Extracts gateway IP from DHCP info
2. HTTP GET `http://{gw}:2121/` — check for TollGate (kind=10021)
3. Parse price/mint/metric from advertisement tags
4. Check wallet balance ≥ price
5. `nucula_wallet_send(price_sats)` → cashuA V3 token
6. POST token to `http://{gw}:2121/`
7. Parse kind=1022 response — session granted
8. Monitor: periodic GET `/usage`, auto-renew at 20% remaining

#### Client State Machine

```
IDLE → [STA got IP] → DETECTING → [kind=10021 found] → NEEDS_PAY
                     ↓ [no TollGate]        ↓ [wallet has funds]
                   NO_TOLLGATE         PAYING → [kind=1022] → PAID
                                                       ↓ [expiry near]
                                                  RENEWING → PAID
```

#### Design Decisions
- **Blocking**: upstream payment must succeed before local services start
- **1 step per payment** (21 sats / 60s) — minimal, renew frequently
- **No budget cap** — keep paying as long as wallet has balance
- **Renew at 20% remaining** — re-pay when 80% of session consumed
- **Wallet init synchronous** — must complete before client can create tokens

#### Config Addition

```json
{
  "client_enabled": true,
  "client_steps_to_buy": 1,
  "client_renewal_threshold_pct": 20,
  "client_retry_interval_ms": 30000
}
```

#### Integration with `tollgate_main.c`

| Event | Action |
|-------|--------|
| `IP_EVENT_STA_GOT_IP` | Wallet init (sync) → `tollgate_client_on_sta_connected()` → start local services |
| `WIFI_EVENT_STA_DISCONNECTED` | `tollgate_client_on_sta_disconnected()` — reset state |
| Main loop (every 1s) | `tollgate_client_tick()` — check usage, renew if needed |

#### Test Cases

| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 39 | Client detection (kind=10021) | Unit test parse | Correct price/mint/metric extracted | TODO |
| 40 | Client payment flow | Mock HTTP | Token POSTed, kind=1022 parsed | TODO |
| 41 | Session renewal | Mock usage < 20% | Re-payment triggered | TODO |
| 42 | ESP32→OpenWRT auto-pay | Integration | NAT works after payment | TODO |
| 43 | ESP32→ESP32 auto-pay | Cross-board | Board B pays Board A | TODO |

#### Vendor IE Beacon (Pre-Association Discovery) — DEFERRED

Pre-association price discovery via Wi-Fi Vendor IE beacons (OUI `0x54:0x47`) is deferred to a future phase. The client currently uses HTTP-based discovery after connection.

### Phase 5: Lightning Auto-Payout — NOT STARTED

**Goal:** When wallet balance exceeds a configurable threshold, automatically pay out to Lightning addresses via LNURL-pay + Cashu NUT-05 melt.

**New files:** `main/lnurl_pay.c`, `main/lnurl_pay.h`, `main/lightning_payout.c`, `main/lightning_payout.h`
**Modified files:** `components/nucula_lib/nucula_wallet.h`, `components/nucula_lib/nucula_wallet.cpp`

#### Architecture

Mirrors the Go implementation in `tollgate-module-basic-go/src/merchant/` and `src/lightning/`:

```
Every 60s (per mint):
  balance = nucula_wallet_balance()
  balance >= min_payout_amount? No → skip
  Yes:
    payout_pool = balance - min_balance
    For each recipient (factor):
      share = payout_pool * factor
      bolt11 = lnurl_get_invoice(lightning_address, share)
      nucula_wallet_melt(bolt11, share + fee_tolerance%)
```

#### LNURL-pay Protocol (`lnurl_pay.c/h`)

Pure HTTP implementation (2 GETs):
1. `GET https://{domain}/.well-known/lnurlp/{username}` → parse callback URL, min/max amounts
2. `GET {callback}?amount={millisats}` → extract BOLT11 invoice from response

#### nucula Bridge Extension

Add to `nucula_wallet.h`:
```c
esp_err_t nucula_wallet_melt(const char *bolt11_invoice, uint64_t max_fee_sats);
```

Wraps `Wallet::request_melt_quote()` + `Wallet::melt_tokens()` (NUT-05).

#### Config Addition

```json
{
  "payout": {
    "enabled": true,
    "min_payout_amount": 128,
    "min_balance": 64,
    "fee_tolerance_pct": 10,
    "check_interval_s": 60,
    "recipients": [
      {"lightning_address": "user@domain.com", "factor": 0.79},
      {"lightning_address": "dev@domain.com", "factor": 0.21}
    ]
  }
}
```

#### Test Cases

| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 44 | LNURL-pay flow | Unit test HTTP parse | Correct BOLT11 extracted | TODO |
| 45 | Payout threshold | Unit test | Skip when below, trigger when above | TODO |
| 46 | Multi-recipient split | Unit test | Factors sum to 1.0 | TODO |
| 47 | Melt with fee tolerance | Integration | Invoice paid, change received | TODO |
| 48 | Full payout cycle | E2E | Wallet drains to min_balance | TODO |

### Phase 6: Bytes-Based Billing — NOT STARTED

**Goal:** Support both time-based (milliseconds) and data-based (bytes) billing metrics. Mirrors the Go implementation's dual-metric system.

#### lwIP NAPT Byte Counting (Managed Component)

**New component:** `components/lwip_napt_stats/` — patched copy of ESP-IDF's `ip4_napt.c` with per-entry byte counters.

Patch adds to `struct ip_napt_entry`:
```c
uint64_t bytes_up;     // bytes uploaded (client → internet)
uint64_t bytes_down;   // bytes downloaded (internet → client)
```

Increment in `ip_napt_forward()` (upload) and `ip_napt_recv()` (download).

New public API:
```c
void ip_napt_get_client_bytes(uint32_t client_ip, uint64_t *bytes_up, uint64_t *bytes_down);
```

~30 line patch. Lives in the project repo as a managed component, survives ESP-IDF updates.

#### Session Changes

`session_t` gains dual-metric support:
```c
uint64_t allotment_bytes;
uint64_t bytes_consumed;
```

`session_is_expired()` dispatches on metric type:
- `"milliseconds"`: elapsed time ≥ allotment_ms
- `"bytes"`: bytes_consumed ≥ allotment_bytes

#### Config Addition

```json
{
  "metric": "milliseconds",
  "step_size_bytes": 22020096
}
```

#### Test Cases

| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 49 | Byte allotment calc | Unit test | Correct bytes per step | TODO |
| 50 | Byte session expiry | Unit test | Expired when consumed ≥ allotment | TODO |
| 51 | NAPT byte counting | Integration | Counters match actual traffic | TODO |
| 52 | Bytes metric end-to-end | E2E | Client disconnected after data cap | TODO |

### Phase 7: ContextVM Server (MCP over Nostr) — NOT STARTED

**Goal:** Remote configuration of ESP32 TollGate via ContextVM — services communicate over Nostr using public keys as addresses. Exposes configuration as MCP tools accessible by both humans and AI agents.

**New files:** `main/cvm_server.c`, `main/cvm_server.h`, `main/nip44.c`, `main/nip44.h`, `main/mcp_handler.c`, `main/mcp_handler.h`

#### Architecture

ContextVM uses MCP (JSON-RPC 2.0) over NIP-44 encrypted Nostr DMs:
1. ESP32 subscribes to Nostr relays for DMs addressed to its npub
2. Incoming DMs are NIP-44 decrypted, parsed as MCP JSON-RPC requests
3. Dispatched to registered tool handlers
4. Responses sent back via NIP-44 encrypted DM

#### MCP Tools Exposed

| Tool | Input | Output |
|------|-------|--------|
| `get_config` | — | Full config JSON |
| `set_config` | `{key: value}` | Success/error |
| `get_balance` | — | `{balance, proof_count}` |
| `get_sessions` | — | Array of active sessions |
| `get_usage` | — | Upstream usage if client active |
| `set_payout` | `{recipients: [...]}` | Success/error |
| `set_metric` | `{"bytes" or "milliseconds"}` | Success/error |
| `set_price` | `{price_per_step: N}` | Success/error |
| `wallet_send` | `{amount_sats: N}` | `{token: "cashuA..."}` |
| `wallet_melt` | `{bolt11: "ln..."}` | `{preimage: "..."}` |

#### Auth

Only accept commands from owner npub (derived from nsec in config.json).

#### Dependencies

- XChaCha20-Poly1305 (from mbedtls or libsodium)
- Base64url encoding (already in cashu code)
- WebSocket listener (extends existing wifistr infrastructure)
- NIP-44 v2 encryption/decryption

#### Test Cases

| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 53 | NIP-44 encrypt/decrypt | Unit test | Roundtrip matches | TODO |
| 54 | MCP JSON-RPC parse | Unit test | Correct dispatch | TODO |
| 55 | Config change via DM | Integration | ESP32 applies new config | TODO |
| 56 | Balance query via CVM | Integration | Returns correct balance | TODO |

## Total: 56 Tests across 7 phases

## Testing Infrastructure

### Three-Layer Test Architecture

| Layer | Location | What | Runs on | Requires |
|-------|----------|------|---------|----------|
| **Unit** | `tests/unit/` | Host-compiled C tests for pure-logic functions | Dev machine (gcc) | `libmbedtls-dev`, `libcjson-dev` |
| **Integration** | `tests/integration/` | Node.js curl/ping against live board | Dev machine + Board A | Board flashed + connected |
| **E2E** | `tests/e2e/` | Playwright browser tests | Dev machine + Board A | Board + browser |

### Unit Tests (`tests/unit/`)

Host-compiled C tests that verify pure-logic functions with known input/output vectors. No hardware needed. ESP-IDF types provided by stubs in `tests/unit/stubs/`. Source files are **never modified** for testing.

**System deps:** `sudo apt install libmbedtls-dev libcjson-dev`

| Test file | Module | What's tested |
|-----------|--------|---------------|
| `test_geohash.c` | `geohash.c` | `geohash_encode()` against reference vectors (Munich, NYC, origin, boundaries) |
| `test_identity.c` | `identity.c` | `tollgate_derive()` HMAC-SHA512 determinism, MAC locally-administered bit, multicast bit cleared, SSID/IP derivation |
| `test_nostr_event.c` | `nostr_event.c` | NIP-01 event ID (SHA-256 of canonical JSON), Schnorr signature generation + verification, JSON serialization |
| `test_cashu.c` | `cashu.c` | `cashu_decode_token()`, `cashu_calculate_allotment_ms()`, `cashu_is_mint_accepted()` |
| `test_session.c` | `session.c` | Session lifecycle: create/find/extend/expire/revoke, spent-secret dedup |
| `test_tollgate_client.c` | `tollgate_client.c` | Discovery parsing, payment flow, renewal logic, state machine |

**Run:** `make test-unit`

### Integration Tests (`tests/integration/`)

Node.js scripts that test against a live ESP32 board via HTTP, ping, nmcli. Require `TOLLGATE_IP` env var.

| Test file | Phase | What's tested |
|-----------|-------|---------------|
| `phase1_api.mjs` | 1 | Portal HTML, captive URIs, whoami, usage, grant/reset, DNS hijack/forward |
| `phase1_network.mjs` | 1 | AP scan, DHCP, DNS, NAT, ping before/after auth |
| `phase2.mjs` | 2 | API advertisement, payment flow, invalid/spent/wrong-mint tokens, session expiry/renewal |
| `phase3.mjs` | 3 | Wallet endpoints, identity-derived SSID/IP, wifistr on relay, send/receive roundtrip |
| `smoke.mjs` | all | Quick 30s smoke: AP visible, portal, grant, internet, reset |

**Run:** `TOLLGATE_IP=10.192.45.1 make test-integration`

### E2E Tests (`tests/e2e/`)

Playwright browser tests for the captive portal UI and payment flow.

| Test file | What's tested |
|-----------|---------------|
| `captive-portal.spec.mjs` | Portal branding, price, mint URL, template substitution, captive URIs, catch-all, API structure |
| `payment.spec.mjs` | Paste token → click Pay → success/error, empty submit, full payment flow |

**Run:** `TOLLGATE_IP=10.192.45.1 make test-e2e`

### Test Coverage Rules

- Every new `.c/.h` file MUST have unit tests in `tests/unit/`
- Every new HTTP endpoint MUST have integration tests in `tests/integration/`
- Every new browser-visible feature MUST have Playwright tests in `tests/e2e/`
- All tests must pass before commit
- Commit + push every time a test passes that previously didn't pass
- Never hardcode IP addresses — always use `process.env.TOLLGATE_IP`
- See `AGENTS.md` for full rules

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
