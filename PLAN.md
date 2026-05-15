# TollGate ESP32 — Test-Driven Development Plan

## Overview

Build a TollGate firmware for two ESP32 devices, following the [TollGate protocol spec](https://github.com/OpenTollGate/tollgate) (TIP-01, TIP-02, HTTP-01/02/03). The implementation uses ESP-IDF (C/C++) and integrates the nucula Cashu wallet.

## Architecture Decision: C/C++ (ESP-IDF)

- Existing working captive portal is in C (ESP-IDF)
- Nucula Cashu wallet is in C/C++ (ESP-IDF)
- ESP-IDF is already installed at `~/esp/esp-idf`
- No Rust/ESP32 toolchain installed

## Technology Stack

| Layer | Technology |
|-------|-----------|
| Framework | ESP-IDF v5.4.1 (C/C++) |
| Cashu wallet | nucula `Wallet` class (Phase 3) |
| HTTP server | `esp_http_server` (port 80 captive portal, port 2121 TollGate API) |
| DNS | Custom UDP task (hijack unauthenticated, forward authenticated) |
| NAT | lwIP NAPT |
| Testing | Playwright + curl + pyserial |
| Build | Makefile |

## Three-Phase Plan

### Phase 1: Captive Portal + Firewall (No Payments)

**Goal:** WiFi repeater with captive portal that gates internet access. Validates DNS hijack, NAT, DHCP, firewall.

**Endpoints:**
- `GET /whoami` — returns client MAC
- `GET /usage` — returns `-1/-1`
- Captive portal HTML on port 80

**14 Test Cases:**
| # | Test | Method | Pass Criteria |
|---|------|--------|---------------|
| 1 | Boot and AP appears | Serial + nmcli | SSID visible in scan |
| 2 | DHCP lease | nmcli connect | Gets IP in 192.168.4.0/24 |
| 3 | Captive portal serves HTML | GET / | 200, contains "TollGate" |
| 4 | Captive detection URIs work | GET /generate_204 etc. | All return portal HTML |
| 5 | DNS hijack before auth | nslookup google.com | Resolves to 192.168.4.1 |
| 6 | No internet before auth | ping 8.8.8.8 | Fails |
| 7 | /whoami returns MAC | GET /whoami | Returns mac=XX:XX:... |
| 8 | /usage returns no session | GET /usage | Returns -1/-1 |
| 9 | Grant access via API | GET /grant_access | 200, status granted |
| 10 | DNS forward after auth | nslookup google.com | Resolves to real IP |
| 11 | Internet after auth | ping 8.8.8.8 | Succeeds |
| 12 | HTTP browsing works | Playwright | Page loads |
| 13 | Reset auth | GET /reset_authentication | 200 |
| 14 | Internet blocked after reset | ping 8.8.8.8 | Fails |

### Phase 2: E-Cash Payments (Simple Melt-to-LNURL)

**Goal:** Replace free access with Cashu payment. ESP32 parses token, melts via mint API to operator's LNURL.

**New Endpoints:**
- `GET /` on :2121 — TollGate advertisement (kind=10021)
- `POST /` on :2121 — Accept Cashu token, melt, return session (kind=1022) or notice (kind=21023)

**13 Additional Test Cases:**
| # | Test | Method | Pass Criteria |
|---|------|--------|---------------|
| 15 | Advertisement valid | GET :2121/ | kind=10021 with price_per_step |
| 16 | Valid payment | POST :2121/ with token | kind=1022 session |
| 17 | Usage tracking | GET :2121/usage | 0/allotment |
| 18 | Internet after payment | ping | Succeeds |
| 19 | Invalid token | POST :2121/ garbage | kind=21023 error |
| 20 | Spent token | Reuse token | kind=21023 spent error |
| 21 | Wrong mint | Token from unaccepted mint | kind=21023 mint error |
| 22 | Session expiry | Wait for allotment | Internet blocked |
| 23 | Session renewal | Second payment | Allotment extended |
| 24 | Portal payment form | Playwright paste token | Checkmark shown |
| 25 | Two clients pay independently | Two POSTs | Both authenticated |
| 26 | Client isolation | Only payer gets internet | Non-payer blocked |
| 27 | Full e2e: portal→pay→browse | Playwright | Complete flow |

### Phase 3: nucula Wallet Integration + Reseller

**Goal:** Integrate nucula's full Cashu wallet. ESP32 holds balance, can be a reseller.

**11 Additional Test Cases:**
| # | Test | Method | Pass Criteria |
|---|------|--------|---------------|
| 28 | Wallet boot | Serial | Keysets loaded |
| 29 | Receive via wallet | POST :2121/ | Balance incremented |
| 30 | Balance persists | Reboot | Same balance |
| 31 | Payout routine | Wait + serial | Tokens melted to LN |
| 32 | Reseller discover | Serial | Upstream TollGate found |
| 33 | Reseller pay | Serial + API | Token POSTed upstream |
| 34 | Multi-hop internet | Ping from laptop | laptop→A→B→internet |
| 35 | P2PK receive | Post P2PK token | Auto-signed, accepted |
| 36 | DLEQ verified | Post token with DLEQ | Verified, accepted |
| 37 | 5 consecutive payments | Loop | All authenticated |
| 38 | Stress: rapid pay/expire | Loop with short sessions | No crash/leak |

## Total: 38 Tests across 3 phases
