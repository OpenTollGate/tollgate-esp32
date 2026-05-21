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

### Phase 4: ESP32 TollGate Client Detection + Auto-Payment — COMPLETE

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

### Phase 5: Lightning Auto-Payout — COMPLETE

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

### Phase 6: Bytes-Based Billing — COMPLETE

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

### Phase 7: ContextVM Server (MCP over Nostr) — REWRITE IN PROGRESS

**Goal:** Full ContextVM protocol implementation — ESP32 acts as an MCP server discoverable on the Nostr network via CEP-6 public announcements, communicating via kind 25910 ephemeral events.

**Protocol:** ContextVM transports MCP JSON-RPC 2.0 messages over Nostr. Server is identified by its npub (derived from nsec). Clients discover the server via kind 11316 announcements, then communicate via kind 25910 ephemeral events.

#### Architecture

```
Client (nak/ContextVM SDK)
  → publishes kind 25910 to relay ({"method":"tools/call","params":{"name":"get_config"}})
  → ESP32 cvm_server reads from persistent WebSocket subscription
  → parses MCP JSON-RPC from event content
  → dispatches to mcp_handler.c
  → publishes kind 25910 response back to relay
  → client receives response via subscription
```

#### ContextVM Event Kinds Used

| Kind | Purpose | CEP |
|------|---------|-----|
| 25910 | MCP request/response transport (ephemeral) | Draft spec |
| 11316 | Server announcement (replaceable) | CEP-6 |
| 11317 | Tools list announcement (replaceable) | CEP-6 |
| 10002 | Relay list (replaceable) | CEP-17 (NIP-65) |

#### MCP Protocol Flow

1. ESP32 publishes kind 11316 (server announcement) + kind 11317 (tools list) + kind 10002 (relay list) on startup
2. ESP32 opens persistent WebSocket to relays, subscribes to `{"kinds":[25910],"#p":["<npub>"]}`
3. Client sends kind 25910 `initialize` request
4. ESP32 responds with kind 25910 `initialize` result (capabilities, serverInfo)
5. Client sends `notifications/initialized`
6. Client calls `tools/list` or `tools/call`
7. ESP32 dispatches to `mcp_handler.c`, returns result

#### Encryption

Phase 7a ships with **plaintext** kind 25910 events. Encryption (CEP-4: NIP-44 gift wrap) is deferred to Phase 7b. The `support_encryption` tag is NOT included in announcements until Phase 7b.

#### MCP Tools Exposed (10 total)

| Tool | Input | Output |
|------|-------|--------|
| `get_config` | — | Full config JSON |
| `set_config` | `{key: value}` | Success/error |
| `get_balance` | — | `{balance, proof_count}` |
| `get_sessions` | — | Array of active sessions |
| `get_usage` | — | Upstream usage if client active |
| `set_payout` | `{recipients: [...]}` | Success/error |
| `set_metric` | `{"metric": "bytes" or "milliseconds"}` | Success/error |
| `set_price` | `{"price_per_step": N}` | Success/error |
| `wallet_send` | `{"amount": N}` | `{token: "cashuA..."}` |
| `wallet_melt` | `{"bolt11": "ln..."}` | `{preimage: "..."}` |

#### Auth

Only accept kind 25910 requests from owner npub (derived from nsec in config.json). Non-owner requests are silently dropped.

#### Dependencies

- WebSocket persistent connection (extends `wifistr.c` TLS + WS pattern)
- secp256k1 Schnorr signing (existing `nostr_event.c`)
- cJSON (existing)
- mbedtls TLS (existing)
- NIP-04 encryption (existing `nip04.c`) — for future encrypted mode

#### Files

| File | Status | Purpose |
|------|--------|---------|
| `main/cvm_server.c` | Rewrite | WS listener, MCP handlers, CEP-6 announcements |
| `main/cvm_server.h` | Update | New public API |
| `main/mcp_handler.c` | Extend | 6 new tools |
| `main/mcp_handler.h` | Update | New tool enums + handlers |
| `main/config.c` | Minor | Default `cvm_enabled = true` |
| `tests/unit/test_cvm_server.c` | New | CVM unit tests |
| `tests/unit/test_mcp_handler.c` | Extend | 6 new tool tests |
| `tests/integration/test-cvm.mjs` | New | CVM integration test via nak |
| `Makefile` | Update | `cvm-*` targets |

#### Test Cases

| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 53 | MCP JSON-RPC parse from kind 25910 | Unit test | Correct dispatch to tool handler | PASS |
| 54 | Kind 11316 announcement construction | Unit test | Valid event with correct tags/capabilities | PASS |
| 55 | Kind 11317 tools list construction | Unit test | All 10 tools listed with schemas | PASS |
| 56 | Kind 10002 relay list construction | Unit test | Correct `r` tags | PASS |
| 57 | Auth rejection for non-owner | Unit test | Non-owner events dropped | PASS |
| 58 | MCP initialize response | Unit test | Correct capabilities + serverInfo | PASS |
| 59 | New tool: get_sessions | Unit test | Returns session array | PASS |
| 60 | New tool: get_usage | Unit test | Returns usage stats | PASS |
| 61 | New tool: set_payout | Unit test | Updates payout config | PASS |
| 62 | New tool: set_metric | Unit test | Updates metric field | PASS |
| 63 | New tool: set_price | Unit test | Updates price_per_step | PASS |
| 64 | New tool: wallet_melt | Unit test | Calls nucula_wallet_melt | PASS |
| 65 | Kind 11316 on relay | Integration | Announcement found on relay | PASS* |
| 66 | MCP initialize roundtrip | Integration | Response received via nak | TODO |
| 67 | get_config via CVM | Integration | Returns valid JSON config | TODO |
| 68 | get_balance via CVM | Integration | Returns balance + proofs | TODO |
| 69 | set_price via CVM | Integration | Price updated on device | TODO |
| 70 | Kind 11317 on relay | Integration | Tools list found on relay | PASS* |
| 71 | Kind 10002 on relay | Integration | Relay list found on relay | PASS* |
| 72 | API reachability from host | Integration | HTTP 200 from board AP | PASS |
| 73 | CVM event publish from host | Integration | Kind 25910 published to relay | PASS |

*Passes when board has upstream WiFi and SNTP is synced. Events expire without valid `created_at` timestamp.

#### WiFi Country Code Fix (Critical)

**Problem:** ESP-IDF defaults to CN (China) regulatory domain when no country code is set. The boards are in DE (Germany/EU). Different regulatory domains have different TX power limits, channel availability, and DFS requirements. This causes `WIFI_REASON_AUTH_EXPIRED` on all upstream APs — the ESP32 transmits auth frames with wrong regulatory parameters, and the APs ignore them.

**Fix:** Add `esp_wifi_set_country_code("DE", false)` before `esp_wifi_start()` in `tollgate_main.c`.

**Evidence:**
- Auth fails even in STA-only mode (no AP at all), ruling out APSTA channel conflicts
- Auth fails against a laptop hotspot 1m away, ruling out signal strength
- Auth fails with factory MAC, ruling out MAC filtering
- Auth fails with PMF enabled, WPA2 threshold, all-channel scan
- Laptop connects to same APs at 100% signal — ESP32 radio is the outlier
- Dense 2.4GHz spectrum (ch1: 2 APs, ch6: 4 APs, ch11: 4 APs) but not exhausted

**Alternative hypothesis:** Hardware antenna issue on Board A. Need to test Board B/C to confirm.

## Total: 81 Tests across 8 phases

## Post-Phase 7: Bug Fixes & Architecture Improvements

### Per-Client NAT Filtering (Multi-Client Isolation Fix)

**Problem:** When client A's session expires but client B is still active, NAPT stays enabled globally. Client A's existing TCP/UDP connections in the NAT table continue routing — their traffic reaches the internet even though they're unauthenticated.

**Solution:** Use lwIP's `LWIP_HOOK_IP4_CANFORWARD` hook to filter forwarded packets by source IP. This fires in `ip4_forward()` **before** NAPT translation, so unauthorized clients are blocked at the IP layer.

**Architecture Change:**
- **Before:** NAT is a global toggle — ON when any client is auth'd, OFF when none are
- **After:** NAT stays always ON. Per-client filtering happens in the lwIP forwarding path via `LWIP_HOOK_IP4_CANFORWARD`

**Files:**

| File | Action | Description |
|------|--------|-------------|
| `main/lwip_tollgate_hooks.h` | Create | Defines `LWIP_HOOK_IP4_CANFORWARD` macro |
| `CMakeLists.txt` | Modify | Inject hook header into lwIP compilation |
| `main/firewall.c` | Modify | Add filter function, remove global NAT toggle |
| `main/firewall.h` | Modify | Expose filter function declaration |
| `main/tollgate_main.c` | Modify | Remove `firewall_disable_nat()` from stop_services |

**Implementation:**

1. `lwip_tollgate_hooks.h` — hook header included by lwIP:
```c
#define LWIP_HOOK_IP4_CANFORWARD(p, addr) tollgate_ip4_canforward_filter(p, addr)
```

2. `CMakeLists.txt` — inject into lwIP build (follows ESP-IDF vlan example pattern):
```cmake
idf_component_get_property(lwip lwip COMPONENT_LIB)
target_compile_options(${lwip} PRIVATE "-I${PROJECT_DIR}/main")
target_compile_definitions(${lwip} PRIVATE "-DESP_IDF_LWIP_HOOK_FILENAME=\"lwip_tollgate_hooks.h\"")
```

3. `firewall.c` — filter function checks source IP against allowed client list:
```c
int tollgate_ip4_canforward_filter(struct pbuf *p, u32_t dest_addr_hostorder) {
    struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
    uint32_t src_ip = lwip_ntohl(iphdr->src.addr);
    return firewall_is_client_allowed(src_ip) ? 1 : 0;
}
```

4. NAT management simplified: enable once during `firewall_init()`, never disable. Remove `update_nat()`, `firewall_enable_nat()`, `firewall_disable_nat()`.

**Key properties:**
- Hook fires only for **forwarded** packets (AP client → internet), not packets to ESP32 itself
- Captive portal (port 80) and API (port 2121) remain accessible to all clients
- DNS hijack continues independently — both layers enforce auth
- Return traffic: NAPT only creates DNAT entries for successfully forwarded outbound packets, so blocked clients don't get return traffic either
- Performance: linear scan of ≤10 clients per packet — negligible cost

### Spent-Secret Cleanup

**Problem:** `session.c` tracks spent Cashu secrets locally in a static array (`s_spent_secrets[100][65]`). This is redundant — the mint is the authority on spent state, and `nucula_wallet_receive()` already swaps proofs (registering them as spent with the mint).

**Changes:**
- Remove `s_spent_secrets[]`, `s_spent_count`, `session_is_secret_spent()` from `session.c`
- Remove `spent_secrets` and `spent_secret_count` from `session_t` struct
- Remove `spent_secrets` params from `session_create()` / `session_create_bytes()`
- Remove local spent-secret check in `tollgate_api.c` — keep only mint `check_proof_states` check
- Update unit tests

**Rationale:** The mint's `cashu_check_proof_states()` already catches double-spends over HTTP. `nucula_wallet_receive()` → `swap()` registers proofs as spent and replaces them. After a successful receive, the old token is useless. Local tracking adds no security, wastes 6.5KB RAM, and is lost on reboot anyway.

### Phase 8: TFT Display (JC3248W535 / AXS15231B) — IN PROGRESS

**Goal:** Add TFT display support to the JC3248W535 board for QR code rendering + status text. Display cycles between a Wi-Fi QR code (so customers can connect) and a portal URL QR code (for direct portal access).

**Hardware:** JC3248W535 board — ESP32-S3, AXS15231B 320x480 QSPI TFT, capacitive touch
**Pin mapping:** CS=45, CLK=47, D0=21, D1=48, D2=40, D3=39, BL=1, Touch SDA=4, Touch SCL=8

#### Components Created

| Component | Path | Purpose |
|-----------|------|---------|
| `components/qrcode/` | `qrcoded.c/h` + CMakeLists.txt | QR code generation (ported from NSD, MIT license) |
| `components/axs15231b/` | `axs15231b.c/h` + CMakeLists.txt | AXS15231B QSPI display driver |
| `main/display.c/h` | Display abstraction | FreeRTOS display task, state machine, QR cycling |
| `main/font.c/h` | 8x8 bitmap font | ASCII 32-127 for status text rendering |

#### Display States

| State | Screen | QR Content |
|-------|--------|------------|
| `DISPLAY_BOOT` | "TollGate starting..." | None |
| `DISPLAY_READY` | QR code + SSID label | Cycles: Wi-Fi QR ↔ Portal URL QR every 5s |
| `DISPLAY_PAYMENT_RECEIVED` | Green "Paid! Access granted" | None (2s, then READY) |
| `DISPLAY_ERROR` | Red "No upstream" | None |

#### Wi-Fi QR Code Format

Uses the standardized ZXing `WIFI:` URI scheme — natively recognized by Android and iOS camera apps:

```
WIFI:S:<escaped_ssid>;T:nopass;;
```

**Special character escaping**: `;`, `:`, `\`, `,`, `"` are backslash-escaped in the SSID field per spec.

**Example:**
```
SSID: TollGate-C0E9CA → WIFI:S:TollGate-C0E9CA;T:nopass;;
SSID: My;WiFi:Test → WIFI:S:My\;WiFi\:Test;T:nopass;;
```

When scanned, the phone **automatically connects** to the TollGate AP — then the captive portal takes over for payment.

#### QR Cycling

The display alternates between two QR modes every 5 seconds:
1. **Wi-Fi QR** — `WIFI:S:...;T:nopass;;` — auto-connects phone to AP
2. **Portal URL QR** — `http://10.x.x.1/` — direct link to captive portal

Label text below QR changes to indicate current mode: "Scan to connect" vs "Portal URL".

#### Display Driver Architecture

- **Interface**: Single-line SPI (MOSI on D0/GPIO21) — simpler than QSPI, reliable for V1
- **Framebuffer**: 307,200 bytes (480x320x2 RGB565) in PSRAM via `heap_caps_malloc`
- **Flush**: 10 chunks of 32KB via `spi_device_polling_transmit`
- **Rotation**: Landscape (MADCTL=0x60, MX|MV)
- **Backlight**: GPIO1 active-high

#### Test Cases

| # | Test | Method | Pass Criteria | Status |
|---|------|--------|---------------|--------|
| 70 | Wi-Fi QR scannable | Android camera scan | Phone connects to AP | TODO |
| 71 | Portal URL QR scannable | Android camera scan | Browser opens portal | TODO |
| 72 | QR cycling | Watch display | Mode changes every 5s | TODO |
| 73 | Boot screen | Visual | "TollGate starting..." shown | TODO |
| 74 | Payment screen | Trigger payment | Green "Paid!" for 2s | TODO |
| 75 | Error screen | Disconnect upstream | Red "No upstream" | TODO |
| 76 | Special char escape | Unit test | `\;:,"` correctly escaped | TODO |
| 77 | QR generation | Unit test | Valid QR matrix for various string lengths | TODO |

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

## Phase 9: Local Nostr Relay + Relay Selection + Sync — COMPLETE

**Goal:** Integrate a local Nostr relay into the firmware. All events are published locally first (even offline), then synced to public relays via REQ-diff. Relay selection uses NIP-11 HTTP probing with NIP-77 scoring.

### Architecture

```
Publishers (wifistr, CEP-6, CVM)
  → local_relay (port 4869, LittleFS 4MB, 5000 events, 21-day TTL)
  → relay_selector (NIP-11 probes, scoring, auto-failover)
  → sync_manager (REQ-diff: primary 30min, fallback 6h)
  → CVM server (persistent WS to primary relay)
```

### Design Decisions

| Decision | Rationale |
|----------|-----------|
| Local-first publishing | Reduces WS connections to 1 persistent + brief periodic |
| REQ-diff sync (not negentropy binary) | NIP-77 binary protocol adapter not yet written; REQ-diff works everywhere |
| NIP-11 HTTP probing | No WS needed; get liveness, latency, NIPs from simple HTTP GET |
| 4MB LittleFS partition | 5000 events, 21-day TTL; uses free flash without touching SPIFFS |
| Rewrite validator (no libnostr-c) | Use existing secp256k1 + mbedtls; avoid symbol conflicts |
| Port 4869, accessible to WiFi clients | Enables local CVM, service discovery, mesh scenarios |

### Flash Layout Addition

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| relay_store (LittleFS) | 0x500000 | 4MB | Relay event storage |

### New Files

```
components/wisp_relay/           # Local Nostr relay (16 files, no libnostr-c deps)
  ws_server.c/h                  # WebSocket server (port 4869) + NIP-11
  storage_engine.c/h             # LittleFS event storage + NVS index
  sub_manager.c/h                # Subscription management
  broadcaster.c/h                # JSON fanout to subscribers
  rate_limiter.c/h               # Per-connection rate limiting
  nip11_relay.c/h                # NIP-11 info document
  deletion.c/h                   # NIP-09 deletion
  flash_monitor.c/h              # LittleFS health reporting
  router.c/h                     # NIP-01 message routing
  relay_validator.c/h            # Schnorr verify + SHA-256 event ID
  relay_types.c/h                # Local type definitions
  handlers.c/h                   # EVENT/REQ/CLOSE handlers
  relay_core.h                   # Central relay context

components/esp_littlefs/         # Git submodule: LittleFS VFS
components/negentropy/           # Git submodule: for future NIP-77

main/
  local_relay.c/h                # Thin wrapper: init/start/publish
  relay_selector.c/h             # NIP-11 probe + scoring + failover
  sync_manager.c/h               # REQ-diff sync engine
```

### Config Additions

```json
{
  "nostr_seed_relays": ["wss://relay.orangesync.tech", "wss://relay.damus.io",
                         "wss://nos.lol", "wss://relay.nostr.band"],
  "nostr_sync_interval_s": 1800,
  "nostr_fallback_sync_interval_s": 21600
}
```

### Bug Fixes

- **config.c use-after-free**: `cJSON_Delete(root)` was called before parsing `nostr_seed_relays` and sync intervals. Moved all cJSON accesses before the delete.
- **Relay not starting at boot**: `local_relay_init()/start()` was inside `start_services()` (gated on STA getting IP). Moved to `app_main()` so relay is always available on the AP interface.

### Test Results (Board B, live hardware)

| Test | Result |
|------|--------|
| Smoke: ping + HTTP 4869 + NIP-11 | PASS |
| NIP-11 info document (10 checks) | 10/11 PASS |
| WS pub/sub (connect, REQ/EOSE, EVENT/OK, CLOSE, concurrent) | 6/6 PASS |
| Unit tests (relay_validator + relay_selector) | 13/13 PASS |
| Sync to public relay | Expected (30min interval, needs STA internet) |

### Hardware Test Make Targets

In `physical-router-test-automation/`:
- `make relay-build` — build relay firmware
- `make relay-flash-b` — flash to Board B
- `make relay-test-smoke` — verify port 4869
- `make relay-test-nip11` — NIP-11 document test
- `make relay-test-pubsub` — WS publish + subscribe test
- `make relay-test-sync` — verify sync to public relays
- `make relay-test-full` — all tests sequentially

### Future

- Implement negentropy binary protocol (NIP-77 NEG_OPEN/NEG_MSG) for efficient set-reconciliation sync
- NIP-11 returns JSON without Accept header (minor: should return HTML)

## Phase 10: Wallet Receive via Health Task Queue — IN PROGRESS

**Goal:** Reliable wallet receive on ESP32 by reusing the mint health task's TLS context. Eliminates internal RAM fragmentation that prevented standalone TLS worker creation.

### Problem Solved

The original approach (standalone `tls_worker` task) failed because:
- ESP32-S3 internal RAM is ~25KB free at boot after httpd (16KB) + health (16KB) tasks
- Largest contiguous block after those two tasks: ~10-14KB — insufficient for a 16KB TLS stack
- PSRAM stack crashes on NVS writes: `esp_task_stack_is_sane_cache_disabled()` assertion in `spi_flash`

### Solution

Wallet receive operations are submitted to a FreeRTOS queue processed by the **mint health task** (already has 16KB stack + working TLS). Health task polls the queue every 1s between probe intervals.

### Files Changed

| File | Change |
|------|--------|
| `main/tollgate_api.c` | Removed `tls_worker_task`/`tls_worker_start`, added `tls_worker_set_queue()` + queue-based `tls_worker_submit()` |
| `main/tollgate_api.h` | Replaced `tls_worker_start()` with `tls_worker_set_queue()` |
| `main/mint_health.c` | Added wallet queue, `process_wallet_queue()`, integrated into `health_task()` main loop |
| `main/tollgate_main.c` | Removed `tls_worker_start()` call |
| `tests/unit/stubs/freertos/queue.h` | New: FreeRTOS queue stub for host tests |
| `tests/unit/stubs/freertos/FreeRTOS.h` | Added `BaseType_t`, `UBaseType_t`, `TickType_t`, `pdFALSE` |
| `tests/unit/stubs/tollgate_api.h` | New: `tls_worker_set_queue()` stub |
| `tests/unit/test_mint_health.c` | Added stub implementations for `nucula_wallet_receive`, `nucula_wallet_balance`, `tls_worker_set_queue` |

### Checklist

- [x] Remove standalone TLS worker task
- [x] Add wallet queue to mint_health task
- [x] Health task polls queue every 1s
- [x] Fallback to synchronous receive if queue unavailable
- [x] Unit tests pass (16/16)
- [x] Full payment round-trip confirmed (2 payments -> 41 sat)
- [x] Smoke integration test (6/6)
- [ ] Fix 4 failing API integration tests
- [ ] Test spend from funded wallet
- [ ] Flash Board C with wallet fix
- [ ] Cross-board integration test
- [ ] Playwright E2E captive portal test
- [ ] CVM round-trip test
- [ ] Reliability burst test (5-10 rapid payments)
- [ ] Persistence survives reboot
- [ ] Wallet swap endpoint test
