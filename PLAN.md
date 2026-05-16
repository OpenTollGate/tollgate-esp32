# TollGate ESP32 — Test-Driven Development Plan

## Overview

Build a TollGate firmware for two ESP32 devices, following the [TollGate protocol spec](https://github.com/OpenTollGate/tollgate) (TIP-01, TIP-02, HTTP-01/02/03). The implementation uses ESP-IDF (C/C++) with an on-device Cashu wallet using mbedTLS secp256k1.

## Architecture Decision: C/C++ (ESP-IDF)

- Existing working captive portal is in C (ESP-IDF)
- On-device Cashu wallet uses mbedTLS secp256k1 (hardware RNG, software ECP)
- ESP-IDF is already installed at `~/esp/esp-idf`
- No Rust/ESP32 toolchain installed

## Technology Stack

| Layer | Technology |
|-------|-----------|
| Framework | ESP-IDF v5.4.1 (C/C++) |
| Cashu wallet | Custom mbedTLS secp256k1 wallet (hash_to_curve, blind signing, swap, send) |
| HTTP server | `esp_http_server` (port 80 captive portal, port 2121 TollGate API + wallet) |
| DNS | Custom UDP task (hijack unauthenticated, forward authenticated) |
| NAT | lwIP NAPT |
| Persistence | SPIFFS (960K partition) with threshold-based write protection |
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

### Phase 3: On-Device Wallet + ESP32-to-ESP32 Payments — IN PROGRESS

**Goal:** On-device Cashu wallet using mbedTLS secp256k1. ESP32 holds balance, can swap proofs, create tokens for P2P payments. Proof persistence via SPIFFS with threshold-based write protection.

#### Wallet Architecture

- **Crypto**: mbedTLS secp256k1 (software ECP, hardware RNG via `esp_fill_random`)
- **Blind signing**: `hash_to_curve()` (SHA256 try-and-increment), `scalar_mul()`, `point_add()`
- **Unblinding**: `C = C_ + (order - r) * G` — avoids needing mint's public key K, avoids point negation
- **Proof storage**: In-memory array (50 max), persisted to SPIFFS JSON
- **Persistence**: SPIFFS `/spiffs/wallet.json`, only written when `balance >= persist_threshold_sats`
- **Keyset fetch**: GET /v1/keys from mint on boot
- **Swap**: POST /v1/swap — reissues proofs with new secrets
- **Token creation**: Encode proofs as `cashuA` base64url token

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

### Phase 4: ESP32-to-OpenWRT TollGate Interop — NOT STARTED

**Goal:** ESP32 can pay OpenWRT TollGate using Cashu tokens. Full interoperability with existing OpenWRT-based TollGate infrastructure.

## Total: 38 Tests across 4 phases

## Key Technical Notes

### mbedTLS 3.x Compatibility
- `mbedtls_ecp_point` is opaque — cannot access `.X`, `.Y`, `.Z` directly
- Use `mbedtls_ecp_muladd`, `mbedtls_ecp_mul`, `mbedtls_ecp_point_read/write_binary`
- No point negation needed with `C = C_ + (order - r) * G` unblinding approach

### Board Configuration
- Board A: `/dev/ttyACM0`, MAC `94:a9:90:2e:37:7c`, SSID `TollGate-377C`, AP IP `10.55.85.1`
- Board B: `/dev/ttyACM1`, MAC `fc:01:2c:c5:50:50`, unique SSID/IP derived from MAC
- Both boards run identical firmware, unique config derived at boot from factory MAC

### Test Mint
- `testnut.cashu.space` — auto-pays lightning invoices for testing
- `cashu -h https://testnut.cashu.space invoice <amount>` → auto-paid
- `cashu -h https://testnut.cashu.space send --legacy <amount>` → generates cashuA token
