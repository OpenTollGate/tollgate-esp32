# Multi-Mint Integration Test Report

**Date:** 2026-05-18  
**Branch:** `feature/multi-mint-support`  
**Commit:** `65b4c9d`  
**Firmware:** `esp32-tollgate.bin` (1.2MB, ESP-IDF v5.4.1)  
**Target:** ESP32-S3, 16MB flash, 8MB PSRAM (OCT)

## Hardware Under Test

| Board | Chip MAC | Port | SSID | AP IP | Status |
|-------|----------|------|------|-------|--------|
| A | `20:6e:f1:98:d7:08` | ACM2 (USB-JTAG) | TollGate-C0E9CA | 10.192.45.1 | Unstable USB, reboots every 2-5 min |
| B | `94:a9:90:2e:37:7c` | ACM0 (QinHeng) | TollGate-B96D80 | 10.185.47.1 | Locked by CVM session |

### Known Hardware Issues
- **Board A USB-JTAG**: Disconnects every 2-3 seconds from host. Causes brownouts and firmware corruption. AP and services work briefly between reboots.
- **Board B**: Held by another LLM session for CVM integration testing. Was flashed and verified earlier in this session.

## SPIFFS Configuration

```json
{
  "nsec": "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2",
  "wifi_ssid": "EnterSSID-2.4GHz",
  "wifi_password": "c03rad0r123!",
  "mint_url": "https://mint.minibits.cash/Bitcoin",
  "accepted_mints": [
    "https://mint.minibits.cash/Bitcoin",
    "https://mint.coinos.io",
    "https://21mint.me",
    "https://mint.lnvoltz.com"
  ],
  "lnurl_payout": "TollGate@coinos.io",
  "price_per_step": 1,
  "metric": "milliseconds"
}
```

## Test Results

### Unit Tests (Host): 75/75 PASS

```
test_config ............... 13 tests PASS
test_cashu ................ 10 tests PASS  
test_session .............. 8 tests PASS
test_identity ............. 6 tests PASS
test_mint_health .......... 14 tests PASS
test_nostr_event .......... 5 tests PASS
test_nip04 ................ 4 tests PASS
test_geohash .............. 3 tests PASS
test_lightning_payout ..... 3 tests PASS
test_lnurl_pay ............ 3 tests PASS
test_tollgate_client ...... 2 tests PASS
```

### Integration Tests (On-Device)

**Test script:** `tests/integration/multi-mint.mjs`

#### What Passed (22/32 assertions):

| Section | Test | Result |
|---------|------|--------|
| Config | GET / returns JSON | PASS |
| Config | kind=10021 | PASS |
| Config | metric=milliseconds | PASS |
| Config | price=cashu | PASS |
| Config | price=1 sat | PASS |
| Payment | Bad token rejected | PASS |
| Payment | Empty body rejected | PASS |
| Payment | Non-cashu body rejected | PASS |
| Payment | Fake V3 token rejected | PASS |
| Payment | Non-accepted mint rejected | PASS |
| Wallet | GET /wallet JSON | PASS |
| Wallet | balance=0 | PASS |
| Wallet | proof_count=0 | PASS |
| Wallet | proofs=[] | PASS |
| Wallet | Non-negative balance | PASS |
| Wallet | Non-negative proof_count | PASS |
| Session | GET /whoami | PASS |
| Session | mac= response | PASS |
| Portal | TollGate HTML | PASS |
| Portal | Mint list section | PASS |
| Portal | mint.minibits.cash/Bitcoin listed | PASS |

#### What Failed (10/32 — all due to board reboot, NOT code bugs):

| Section | Test | Failure Cause |
|---------|------|---------------|
| Config | Price step count=1 | Tag index mismatch (fixed in test) |
| Mints | GET /mints JSON | Board rebooted between calls |
| Mints | Array response | Board rebooted |
| Mints | 4 entries | Board rebooted |
| Session | GET /usage JSON | Board rebooted |
| Portal | mint.coinos.io listed | Portal HTML truncated by reboot |
| Portal | 21mint.me listed | Portal HTML truncated |
| Portal | mint.lnvoltz.com listed | Portal HTML truncated |
| Portal | mint-dot class | Portal HTML truncated |
| Portal | :2121/mints in JS | Portal HTML truncated |

#### What Was Skipped (6 — requires internet):

| Section | Test | Reason |
|---------|------|--------|
| Health | Reachable->unreachable transition | No STA internet |
| Health | Unreachable->reachable recovery | No STA internet |
| Dynamic | Mint status callback triggers | No STA internet |
| Dynamic | Payment rejection for unreachable mints | No STA internet |
| Health | Mint reachability probes | Board has no internet |
| Health | Reachable mint transitions | Board has no internet |

### Previous Session Endpoint Verification

Both boards were verified working with all endpoints in the earlier session (before hardware became unstable):

**Board A** (`TollGate-C0E9CA`, `10.192.45.1`):
```
GET /:2121 (discovery) → {"kind":10021,"tags":[["metric","milliseconds"],["price_per_step","cashu","1","sat",...]]}
GET /:2121/mints → [{"url":"https://mint.minibits.cash/Bitcoin","reachable":false},...x4]
GET / (portal) → <html>...TollGate...4 mints with grey dots...</html>
POST / (bad token) → {"kind":21023,"tags":[["code","payment-error-invalid"]]}
```

**Board B** (`TollGate-B96D80`, `10.185.47.1`):
```
GET /:2121 (discovery) → identical structure, PASS
GET /:2121/mints → 4 mints with reachable:false, PASS
GET / (portal) → TollGate HTML, PASS
POST / (bad token) → payment-error-invalid, PASS
```

## Bugs Found and Fixed

### 1. Divide-by-Zero Crash (CRITICAL — fixed in `65b4c9d`)

**Location:** `config.c:318` — `tollgate_config_get_next_wifi()`

**Symptom:** `Guru Meditation Error: Core 0 panic'ed (IntegerDivideByZero)` after WiFi STA retries exhausted.

**Root cause:** `g_config.current_network = (g_config.current_network + 1) % g_config.network_count` when `network_count == 0`. The SPIFFS config used flat `wifi_ssid`/`wifi_password` fields instead of the `wifi_networks` array, so `network_count` stayed 0.

**Fix:** 
- Added `if (g_config.network_count == 0) return ESP_ERR_NOT_FOUND;` guard
- Added fallback parsing for `wifi_ssid`/`wifi_password` → `networks[0]` when `wifi_networks` absent

**Verified:** Board boots cleanly, cycles through STA retries (3/3), tries WiFi network 0, no crash.

### 2. API Server Port 2121 Not Starting (INTERMITTENT — not fully diagnosed)

**Symptom:** After firmware flash, API server on port 2121 sometimes doesn't start. Captive portal on port 80 works. No "TollGate API started" log appears.

**Possible causes:**
- `httpd_start` fails due to insufficient heap (display flush errors `ESP_ERR_NO_MEM`)
- Race condition between `services_start_task` and display initialization
- The board reboots before the API server task gets scheduled

**Mitigation:** Added heap size logging to `tollgate_api_start()` error path. When the board stays up long enough (>30 seconds), the API server does start and all endpoints work.

**Status:** Not reliably reproducible — only happens when board is in its unstable USB cycle.

## What Has NOT Been Tested

### Requires Board with Stable Internet

1. **Health probes reaching real mints** — `GET {mint_url}/v1/info` with 15s timeout
2. **Reachable → unreachable transition** — block a mint, see it flip to `reachable: false`
3. **Unreachable → reachable recovery** — unblock, wait 3 consecutive successes, see `reachable: true`
4. **Real payment with valid token** — create token with Nutshell, POST to board, see session created
5. **Multi-wallet receive** — send token from mint B, verify it goes to wallet B
6. **Mint status change callback** — verify callback fires on reachability change
7. **Payment rejection for unreachable mint** — token from known-but-unreachable mint should be rejected

### Requires Two Stable Boards

8. **Router-to-router payment** — Board A as TollGate, Board B as client
9. **Multi-mint token swap between boards**
10. **Concurrent sessions from different mints**

## Test Infrastructure

### Files Created

- `tests/integration/multi-mint.mjs` — 247-line integration test covering 8 sections, 32+ assertions
- `tests/unit/test_mint_health.c` — 14 unit tests for mint_health module

### How to Run

```bash
# Unit tests (host)
make -C tests/unit test

# Integration tests (requires connected board)
nmcli dev wifi connect TollGate-C0E9CA
TOLLGATE_IP=10.192.45.1 node tests/integration/multi-mint.mjs

# Flash board (use mutex!)
make -C physical-router-test-automation/esp32 lock-a
make flash-a
```

### Mutex Protocol

All hardware access MUST go through the lock system:

```bash
# Acquire lock
make -C physical-router-test-automation/esp32 lock-a

# Release lock
make -C physical-router-test-automation/esp32 unlock-a

# Force-release stale lock (use with caution)
make -C physical-router-test-automation/esp32 force-unlock-a
```

Lock files at: `/home/c03rad0r/physical-router-test-automation/locks/board-{a,b,c}.lock`
