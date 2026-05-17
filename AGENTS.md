# AGENTS.md — Instructions for AI Coding Agents

## Project Overview

TollGate ESP32 firmware: captive portal WiFi hotspot with Cashu e-cash payments, on-device wallet, Nostr identity derivation, and wifistr service discovery. Runs on two ESP32-S3 boards.

## Technology Stack

- **Framework:** ESP-IDF v5.4.1 (C/C++)
- **Target:** ESP32-S3, 16MB flash, 8MB PSRAM (OCT mode)
- **Wallet:** nucula library (libsecp256k1) via git submodule
- **Identity:** Nostr nsec → HMAC-SHA512 → deterministic MAC/SSID/IP
- **Service discovery:** wifistr (Nostr kind 38787) via WebSocket
- **Testing:** Host C unit tests (gcc), Node.js integration tests (live board), Playwright E2E

## Board Configuration

| Board | Port | Factory MAC | Notes |
|-------|------|-------------|-------|
| A | `/dev/ttyACM0` | `94:a9:90:2e:37:7c` | Primary test target |
| B | `/dev/ttyACM1` | `fc:01:2c:c5:50:50` | Secondary |

Identity (SSID, IP, MAC) is derived from `nsec` in config.json. Each board gets a unique nsec.

## Boot Sequence

```
nvs_flash_init()
  → tollgate_config_init()          // loads config.json with nsec from SPIFFS
  → identity_init(nsec)             // derives npub, STA/AP MAC, SSID, IP via HMAC-SHA512
  → tollgate_config_derive_unique() // copies derived values into config struct
  → esp_netif_init() + esp_event_loop_create_default()
  → wifi_init_sta() + wifi_create_ap_netif()  // AP netif with derived IP
  → esp_wifi_init()
  → esp_wifi_set_mac(STA/AP)        // sets derived MACs
  → esp_wifi_set_mode(APSTA)
  → wifi_configure_ap()             // uses derived SSID
  → esp_wifi_start()
  → [on STA got IP] start_services():
      firewall_init, session_init, wallet_init, dns_server, captive_portal, api, wifistr_publish
```

## Key Files

### Source (main/)
- `tollgate_main.c` — entry point, WiFi AP+STA, event loop, service lifecycle
- `config.c/h` — SPIFFS config.json parsing, nsec/nostr/wifi/mint settings
- `identity.c/h` — HMAC-SHA512 derivation from nsec, npub/MAC/SSID/IP
- `nostr_event.c/h` — NIP-01 event serialization + BIP-340 Schnorr signing
- `geohash.c/h` — lat/lon to geohash encoding
- `wifistr.c/h` — kind 38787 event builder + WebSocket relay publish
- `captive_portal.c/h` — HTTP :80 portal, captive detection, grant/reset
- `dns_server.c/h` — DNS hijack/forward per-client, DoT reject
- `firewall.c/h` — per-client NAT filter via LWIP_HOOK_IP4_CANFORWARD, MAC resolution
- `session.c/h` — time-based sessions, MAC tracking
- `cashu.c/h` — Cashu token decode, checkstate, allotment calc
- `tollgate_api.c/h` — HTTP :2121, payment endpoints, wallet endpoints

### Components
- `nucula_lib/` — C++ bridge to nucula::Wallet (C API in nucula_wallet.h)
- `secp256k1/` — symlink to nucula_src/components/secp256k1/

### Config Format (config.json on SPIFFS)
```json
{
  "nsec": "<64-char hex>",
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

## Testing Rules — MANDATORY

### Rule 1: Every new C source file MUST have unit tests
- Place test in `tests/unit/test_<module>.c`
- Test pure-logic functions with known input/output vectors
- Compile with host gcc via `make -C tests/unit`
- Source files remain untouched — stubs in `tests/unit/stubs/` provide ESP-IDF types
- **Run `make test-unit` after any code change. Must pass before commit.**

### Rule 2: Every new HTTP endpoint MUST have integration tests
- Place in `tests/integration/phase<N>.mjs`
- Test against live board using curl + `TOLLGATE_IP` env var
- Never hardcode IP addresses — always use `process.env.TOLLGATE_IP`

### Rule 3: Every new browser-visible feature MUST have Playwright E2E tests
- Place in `tests/e2e/<feature>.spec.mjs`
- Test the full user-visible flow in a browser

### Rule 4: All tests must pass before commit
- `make test-unit` — host unit tests (no hardware needed)
- `make test-integration` — against live Board A (needs hardware)
- `make test-e2e` — Playwright browser tests (needs hardware)

### Rule 5: Test naming conventions
| Test type | Location | Naming | Run command |
|-----------|----------|--------|-------------|
| Host unit | `tests/unit/` | `test_<module>.c` | `make test-unit` |
| Integration | `tests/integration/` | `phase<N>.mjs` or `<feature>.mjs` | `make test-integration` |
| E2E | `tests/e2e/` | `<feature>.spec.mjs` | `make test-e2e` |

### Rule 6: Coverage requirements by code type
| Code type | Required test type | Examples |
|-----------|-------------------|----------|
| Pure math/logic | Unit test | geohash, allotment calc, derivation |
| Crypto operations | Unit test with known vectors | HMAC derivation, Schnorr signing, SHA-256 |
| Token parsing | Unit test with known tokens | Cashu token decode |
| State management | Unit test with mocks | Session lifecycle, firewall client list |
| HTTP endpoints | Integration test | GET /wallet, POST /, POST /wallet/send |
| HTML pages | Playwright E2E | Portal rendering, payment flow |
| Network behavior | Integration test | DNS hijack, NAT, connectivity |

## How to Run Tests

```bash
# Host unit tests (no hardware needed)
make test-unit

# Integration tests (needs Board A connected and flashed)
export TOLLGATE_IP=10.192.45.1
export TOLLGATE_SSID=TollGate-C0E9CA
make test-integration

# E2E tests (needs Board A + browser)
make test-e2e

# All tests
make test-all

# Quick smoke (30s, needs hardware)
make smoke
```

## Build & Flash

```bash
source ~/esp/esp-idf/export.sh
make flash          # build + flash to Board A
make flash-a        # same
make flash-b        # flash to Board B
```

## Test Infrastructure

### Host Unit Tests (`tests/unit/`)
- Compile with system gcc, link against `libmbedcrypto` + `libcjson` + secp256k1
- ESP-IDF types provided by stubs in `tests/unit/stubs/`
- Each test file is a standalone binary that returns 0 on success, 1 on failure
- Uses a minimal assert macro: `ASSERT(cond, msg)`
- Golden test vectors: known nsec → expected npub/MAC/SSID/IP

### Integration Tests (`tests/integration/`)
- Node.js scripts that run curl/ping/nmcli against a live ESP32 board
- Require `TOLLGATE_IP` env var (default: auto-detect or error)
- Token generation via nutshell CLI: `cashu -h https://testnut.cashu.space send --legacy 21`

### E2E Tests (`tests/e2e/`)
- Playwright browser tests
- Config in `tests/e2e/playwright.config.mjs`
- Test the captive portal UI and payment flow

## Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `TOLLGATE_IP` | (none, must set) | Board A's AP IP (e.g., `10.192.45.1`) |
| `TOLLGATE_SSID` | `TollGate-C0E9CA` | Board A's AP SSID |
| `TEST_TOKEN` | (none) | Cashu token for payment tests |
| `SUDO_PW` | `c03rad0r123` | sudo password for route management |

## External Dependencies

- **Test mint:** `testnut.cashu.space` — auto-pays lightning invoices
- **Nostr relays:** `relay.damus.io`, `nos.lol` — for wifistr events
- **Nutshell CLI:** `cashu` command for token generation
- **ESP-IDF:** `source ~/esp/esp-idf/export.sh` before `idf.py` commands
- **System libs for unit tests:** `libmbedtls-dev`, `libcjson-dev`

## Reminders

- **Commit + push every time a test passes that previously didn't pass.** Green tests = checkpoint. Don't batch multiple test fixes into one commit.
- Commit + push after each working change
- Board A is at `/dev/ttyACM0`, Board B at `/dev/ttyACM1`
- `sudo` password: `c03rad0r123`
- SPIFFS is at offset `0x410000`, size `0xF0000` — erase with `esptool.py erase_region 0x410000 0xF0000` if config is stale
- NVS stores wallet proofs — erasing NVS clears wallet balance
- The `nostr_event.c` `created_at` field uses `gettimeofday()` — mock this in unit tests
- Wifistr event signing uses `secp256k1_schnorrsig_sign32()` — verify with `_verify()` in tests
- Portal HTML has server-side template substitution (`__AP_IP__`, `__PRICE__`, `__MINT_URL__`) — no JS fetch
