# TollGate Core Extraction Roadmap

**Branch:** `feature/tollgate-core-v2`
**Start commit:** `851801f` (Phase 5 complete, hardware-verified)
**Goal:** Extract all portable TollGate business logic into `components/tollgate_core/` for reuse in NerdQAxePlus and other ESP32 firmware.

## Status (updated 2026-05-23)

| Phase | Description | Status | Commit |
|-------|-------------|--------|--------|
| 5 | Shim layer wiring | Done | `851801f` |
| 6a-6d | Consolidate & clean up | Done | `9330b01` |
| 6e-6f | Break circular deps | Done | `fb8558c` |
| 7 | Eliminate shims | Done | `5140a30` |
| 8 | Full Layer 1 extraction (beacon_price, market, etc.) | Deferred | — |
| 9a | Copy component to NerdQAxePlus | Done | `0ff96287` |
| 9b | NerdQAxePlus build with TOLLGATE=1 | Done | `0ff96287` |
| 9c | BitAxe platform implementation | Future | — |
| 10 | Publish to IDF Component Registry | Future | — |

## Testing Protocol

After **every** step:

1. `make test-unit` -- all 21+ unit tests must pass
2. `idf.py build` -- ESP-IDF build must succeed
3. After multi-step phases: flash Board A + `pytest tests/test_smoke.py --board=a`

---

## Phase 6: Consolidate & Clean Up

### 6a. Delete standalone `tollgate_core/`

Dead code -- nothing in the build references it. The component version is what's compiled.

- [ ] `git rm -r tollgate_core/`
- [ ] Verify nothing references it: `grep -r "tollgate_core/" main/ components/ tests/`
- [ ] Test: `make test-unit` + `idf.py build`

### 6b. Eliminate `dns_server.c` duplication

`main/dns_server.c` (316 lines) duplicates `components/tollgate_core/src/tollgate_core_dns.c`.

- [ ] Rewrite `main/dns_server.c` as thin shim: `dns_server_*()` calls `tollgate_core_dns_*()`
- [ ] Update `main/dns_server.h` to keep original function signatures
- [ ] Move `dns_server_set_client_authenticated()` notification to component's internal DNS
- [ ] Add unit test for DNS shim if not covered by existing `test_firewall_sandbox`
- [ ] Test: `make test-unit` + `idf.py build` + flash + pytest smoke

### 6c. Eliminate `stratum_proxy.c` duplication

`main/stratum_proxy.c` (160 lines) duplicates `components/tollgate_core/src/tollgate_core_stratum_proxy.c`.

- [ ] Rewrite `main/stratum_proxy.c` as thin shim: `stratum_proxy_*()` -> `tollgate_core_stratum_proxy_*()`
- [ ] Update `main/stratum_proxy.h` to keep original type names for backward compat
- [ ] Test: `make test-unit` + `idf.py build`

### 6d. Move sandbox logic into component firewall

`main/firewall.c` has `is_sandbox_allowed()` (allows TCP to ports 80/2121/mining_port for unauth clients) -- missing from component.

- [ ] Add `tollgate_core_fw_set_sandbox_ports()` to component's firewall API
- [ ] Add `tollgate_core_fw_is_sandbox_allowed()` to component's internal firewall
- [ ] Update component's `tollgate_core_ip4_canforward_filter` to check sandbox rules
- [ ] Update `main/firewall.c` shim to call `tollgate_core_fw_set_sandbox_ports()` in init
- [ ] Add unit test for sandbox logic with known port combinations
- [ ] Test: `make test-unit` + `idf.py build` + flash + `pytest tests/test_dns_firewall.py --board=a`

### 6e. Break `mint_health` <-> `tollgate_api` circular dependency

`mint_health.c` includes `tollgate_api.h` for `tls_worker_set_queue()`. `tollgate_api.c` includes `mint_health.h` for `mint_health_get_all()`.

- [ ] Extract `QueueHandle_t tls_worker_queue` into a shared module (e.g., `tls_worker.h/c`)
- [ ] `mint_health.c` includes `tls_worker.h` instead of `tollgate_api.h`
- [ ] `tollgate_api.c` includes `tls_worker.h` to get the queue
- [ ] Test: `make test-unit` + `idf.py build`

### 6f. Break `config.h` -> `lightning_payout.h` reverse dependency

`config.h` includes `lightning_payout.h` for `payout_config_t` type.

- [ ] Move `payout_config_t` typedef to `config.h` (or a shared `tollgate_types.h`)
- [ ] Remove `#include "lightning_payout.h"` from `config.h`
- [ ] Add `#include "config.h"` to `lightning_payout.c` if needed
- [ ] Test: `make test-unit` + `idf.py build` + **commit + push**

---

## Phase 7: Eliminate Shim Files

Remove thin wrappers. Consumers use component headers directly.

### 7a. Remove `session.c` / `session.h` shim

- [ ] Update `tollgate_api.c`: `#include "tollgate_core_session.h"` instead of `#include "session.h"`
- [ ] Update `captive_portal.c`: same
- [ ] Update any other consumers of `session.h`
- [ ] Delete `main/session.c` and `main/session.h`
- [ ] Remove from `main/CMakeLists.txt` SRCS
- [ ] Update unit test Makefile if needed
- [ ] Test: `make test-unit` + `idf.py build`

### 7b. Remove `cashu.c` / `cashu.h` shim

- [ ] Update `tollgate_api.c`: `#include "tollgate_core_cashu.h"` instead of `#include "cashu.h"`
- [ ] Move multi-mint logic (iterating `accepted_mints[]`) into component's `tollgate_core_cashu_is_mint_accepted()`
- [ ] Delete `main/cashu.c` and `main/cashu.h`
- [ ] Remove from `main/CMakeLists.txt` SRCS
- [ ] Test: `make test-unit` + `idf.py build`

### 7c. Remove `mining_payment.c` / `mining_payment.h` shim

- [ ] Update all consumers: `#include "tollgate_core_mining.h"` instead of `#include "mining_payment.h"`
- [ ] Delete `main/mining_payment.c` and `main/mining_payment.h`
- [ ] Remove from `main/CMakeLists.txt` SRCS
- [ ] Test: `make test-unit` + `idf.py build`

### 7d. Remove `firewall.c` / `firewall.h` shim

After 6d, firewall shim only has lwIP hook registration + DNS notification.

- [ ] Move lwIP hook registration into `tollgate_main.c` or a new `main/esp_hooks.c`
- [ ] Update consumers to use `tollgate_core_fw_*()` directly
- [ ] Delete `main/firewall.c` and `main/firewall.h`
- [ ] Remove from `main/CMakeLists.txt` SRCS
- [ ] Test: `make test-unit` + `idf.py build` + flash + pytest smoke + DNS/firewall tests
- [ ] **Commit + push**

---

## Phase 8: Extract Layer 1 into Component

### 8a. Extract `beacon_price.c` -> `tollgate_core_beacon.c`

Dependencies: `config`, `identity`, `esp_wifi`, `mbedtls/sha256`

- [ ] Create `components/tollgate_core/src/tollgate_core_beacon.c/h`
- [ ] Abstract WiFi vendor IE API via `tollgate_platform_t` callbacks: `set_vendor_ie()`, `scan_start()`
- [ ] Move mint URL + npub hashing, geohash embedding, IE construction to component
- [ ] Rewrite `main/beacon_price.c` as thin ESP-specific glue calling component
- [ ] Add unit test with known IE vectors
- [ ] Test: `make test-unit` + `idf.py build`

### 8b. Extract `market.c` -> `tollgate_core_market.c`

Dependencies: `beacon_price`, `config`, `identity`, `esp_wifi`

- [ ] Create `components/tollgate_core/src/tollgate_core_market.c/h`
- [ ] Abstract WiFi scan results via platform callback: `on_scan_result()`
- [ ] Move market entry table, price comparison, cheapest selection to component
- [ ] Rewrite `main/market.c` as thin glue
- [ ] Add unit test for market table operations
- [ ] Test: `make test-unit` + `idf.py build`

### 8c. Extract `captive_portal.c` -> `tollgate_core_portal.c`

Dependencies: `firewall`, `session`, `config`, `mining_payment`, `stratum_proxy`, `esp_http_server`

- [ ] Create `components/tollgate_core/src/tollgate_core_portal.c/h`
- [ ] Extract template rendering logic (HTML generation, `__AP_IP__`/`__PRICE__` substitution)
- [ ] Extract captive detection URI handling (generate_204, hotspot-detect, success.txt, etc.)
- [ ] Extract payment processing flow (POST token -> decode -> validate -> grant)
- [ ] Abstract HTTP server via platform callbacks: `httpd_start()`, `register_handler()`, `send_response()`
- [ ] Keep ESP `httpd` glue in `main/captive_portal.c` (thin handler registration)
- [ ] Add unit test for template substitution + captive URI detection
- [ ] Test: `make test-unit` + `idf.py build` + flash + pytest portal tests

### 8d. Extract `stratum_client.c` -> `tollgate_core_stratum_client.c`

Dependencies: `stratum_proxy`, `mining_payment`, `config`, `esp_transport`

- [ ] Create `components/tollgate_core/src/tollgate_core_stratum_client.c/h`
- [ ] Abstract TCP transport via platform callbacks
- [ ] Move Stratum V1 protocol logic (subscribe, authorize, handle mining.notify, submit share)
- [ ] Rewrite `main/stratum_client.c` as thin glue
- [ ] Add unit test for Stratum message parsing
- [ ] Test: `make test-unit` + `idf.py build`

### 8e. Extract `mint_health.c` -> `tollgate_core_mint_health.c`

Dependencies: `tls_worker` (after 6e), `nucula_wallet`, `esp_http_client`

- [ ] Create `components/tollgate_core/src/tollgate_core_mint_health.c/h`
- [ ] Abstract HTTP client via platform callback: `http_get()`, `tls_worker_queue`
- [ ] Move mint probing logic, health state tracking, reachable/unreachable marking
- [ ] Rewrite `main/mint_health.c` as thin glue
- [ ] Add unit test for health state machine
- [ ] Test: `make test-unit` + `idf.py build`

### 8f. Extract `tollgate_client.c` -> `tollgate_core_client.c`

Dependencies: `config`, `market`, `nucula_wallet`, `esp_http_client`

- [ ] Create `components/tollgate_core/src/tollgate_core_client.c/h`
- [ ] Abstract HTTP + wallet via platform callbacks
- [ ] Move upstream TollGate discovery, auto-pay, usage tracking, auto-renew logic
- [ ] Rewrite `main/tollgate_client.c` as thin glue
- [ ] Add unit test for client state machine (already exists: `test_tollgate_client.c`)
- [ ] Test: `make test-unit` + `idf.py build` + flash + full pytest suite
- [ ] **Commit + push**

---

## Phase 9: NerdQAxePlus Integration

### 9a. Restore miner-integration worktree

- [ ] `git worktree add /home/c03rad0r/esp32-miner-integration feature/miner-integration`
- [ ] Or create fresh from `remotes/orangesync/feature/miner-integration`
- [ ] Verify NerdQAxePlus fork at `/home/c03rad0r/esp-miner-nerdqaxeplus/` is intact

### 9b. Copy finalized `tollgate_core` component

- [ ] Sync `components/tollgate_core/` from esp32-tollgate -> NerdQAxePlus `components/tollgate_core/`
- [ ] Update NerdQAxePlus `CMakeLists.txt` to depend on `tollgate_core`
- [ ] Verify `BOARD=NERDAXE TOLLGATE=1 idf.py build` succeeds

### 9c. Implement `tollgate_platform_t` for BitAxe/BM1397

- [ ] Create `components/tollgate_baxe/` with BitAxe-specific platform implementation
- [ ] Implement callbacks: `get_price_sats()`, `get_mint_url()`, `spend_proofs()`, stratum config
- [ ] Wire BM1397 ASIC -> stratum proxy -> tollgate_core mining pipeline
- [ ] Wire eCash payment -> session -> internet access on BitAxe AP

### 9d. Integrate into NerdQAxePlus UI

- [ ] Add payment status to OLED/LCD display
- [ ] Add WiFi AP setup with SSID derived from identity
- [ ] Add Cashu token input via web portal
- [ ] Test: Flash NerdAxe Ultra + mining test + payment test + internet verification
- [ ] **Commit + push**

---

## Phase 10: Publish

### 10a. Component metadata

- [ ] Update `idf_component.yml` with proper version, description, dependencies
- [ ] Add `README.md` to `components/tollgate_core/` with API docs
- [ ] Add `CHANGELOG.md` to component

### 10b. CI pipeline

- [ ] GitHub Actions or self-hosted CI: build on push, run unit tests
- [ ] Hardware-in-the-loop testing on push to develop (Board A)
- [ ] Integration test matrix: Board A + Board B + Board C

### 10c. Publish to IDF Component Registry

- [ ] `compote component upload` to ESP-IDF Component Registry
- [ ] Verify `idf.py add-dependency` works from a clean project
- [ ] Document usage in top-level README

---

## Dependency Graph (Extraction Order)

```
Layer 0: dns_server, lnurl_pay, asic_miner          (no main/ deps)
Layer 1: config, identity, session, cashu, mining    (foundation)
Layer 2: firewall, beacon_price, lightning_payout    (depends on Layer 1)
Layer 3: market, stratum_proxy, stratum_client       (depends on Layer 2)
Layer 4: captive_portal, tollgate_client, mint_health (depends on Layer 3)
Layer 5: tollgate_api                                 (depends on everything)
```

## Current Test Coverage

| Type | Count | Command |
|------|-------|---------|
| Host unit tests | 21 | `make test-unit` |
| Integration tests | 17 | `TOLLGATE_IP=x make test-integration` |
| E2E tests | 3 suites | `make test-e2e` |
| Pytest (hardware) | 12 files | `pytest tests/ --board=a` |
