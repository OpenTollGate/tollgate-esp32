# TollGate on NerdQAxePlus/BitAxe — Fix Plan

**Created:** 2026-05-28
**Branch:** `feature/tollgate-core-v2` (esp32-tollgate), `develop` (NerdQAxePlus)
**Component version:** v1.2.0 → v1.3.0

---

## Phase 1: Implement 3 Missing API Functions in tollgate_core

**File:** `components/tollgate_core/src/tollgate_core.c`

- [ ] 1a. Implement `tollgate_core_on_share_accepted(uint32_t client_ip, double difficulty)`
  - Call `tollgate_core_mining_update_hashrate(client_ip, true)`
  - Get client stats via `tollgate_core_mining_get_client_stats(client_ip)`
  - Find or create session via `tollgate_core_session_find_by_ip()` / `tollgate_core_session_create()`
  - Calculate allotment via `tollgate_core_mining_shares_to_allotment_ms()` or `_bytes()` based on metric
  - Extend session via `tollgate_core_session_extend(session, allotment)`
  - Call `s_platform->on_share_accepted(difficulty)` if set

- [ ] 1b. Implement `tollgate_core_calc_hashprice(double hashrate_ghs)`
  - Check `s_platform->get_hashprice_sats_per_ghs_day()` for manual override (nonzero = return it)
  - Otherwise return `tollgate_core_mining_get_current_hashprice()` (from nbits)

- [ ] 1c. Implement `tollgate_core_get_mining_status_json(void)`
  - Follow `tollgate_core_get_status_json()` pattern (cJSON create → add fields → print → delete)
  - Get proxy stats via `tollgate_core_stratum_proxy_get_stats()`
  - JSON fields: `hashprice`, `nbits`, `total_shares`, `total_accepted`, `total_rejected`, `active_miners`, `hashrate_ghs`

- [ ] 1d. Add unit tests for new functions in `tests/unit/test_mining_api.c`
- [ ] 1e. `make test-unit` — all tests pass
- [ ] 1f. `idf.py build` — esp32-tollgate builds

---

## Phase 2: Wire tick() into NerdQAxePlus Main Loop

**File:** `esp-miner-nerdqaxeplus/main/main.cpp` (line 331 while loop)

- [ ] 2a. Add `tollgate_core_tick()` inside the 10s loop, guarded by `#ifdef TOLLGATE`
- [ ] 2b. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes

---

## Phase 3: Wire WiFi AP Client Events

**File:** `esp-miner-nerdqaxeplus/components/connect/connect.c` (event_handler line 74)

- [ ] 3a. Add `#include "tollgate_core.h"` guarded by `#ifdef TOLLGATE`
- [ ] 3b. Add `WIFI_EVENT_AP_STACONNECTED` handler: resolve MAC→IP via `esp_wifi_ap_get_sta_list_with_ip()`, call `tollgate_core_client_connected(mac, ip)`
- [ ] 3c. Add `WIFI_EVENT_AP_STADISCONNECTED` handler: call `tollgate_core_client_disconnected(mac)`
- [ ] 3d. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes

---

## Phase 4: Make Stratum Proxy Functional

**File:** `components/tollgate_core/src/tollgate_core_stratum_proxy.c`

- [ ] 4a. Line-buffer recv data, parse downstream miner messages using `tollgate_core_stratum_parse_*()`:
  - `mining.subscribe` → respond with extranonce
  - `mining.authorize` → respond with success
  - `mining.submit` → validate difficulty, increment counters, call `tollgate_core_on_share_accepted()`, respond accept/reject
- [ ] 4b. Use real jobs from `tollgate_core_stratum_proxy_set_job()` instead of zeroed placeholders

**File:** `esp-miner-nerdqaxeplus/main/tasks/create_jobs_task.cpp` (line 176)

- [ ] 4c. Hook `create_job_mining_notify()` to forward pool jobs to proxy via `tollgate_core_stratum_proxy_set_job()`, guarded by `#ifdef TOLLGATE`

**File:** `esp-miner-nerdqaxeplus/main/stratum/stratum_manager.cpp` (line 179)

- [ ] 4d. Hook `mining.set_difficulty` to call `tollgate_core_mining_set_current_nbits()`, guarded by `#ifdef TOLLGATE`

- [ ] 4e. `idf.py build` passes for esp32-tollgate
- [ ] 4f. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes

---

## Phase 5: Wire Share Accepted for Self-Mining Credit

**File:** `esp-miner-nerdqaxeplus/main/tollgate_platform.cpp` (line 137)

- [ ] 5a. Update `tollgate_on_share_accepted(double difficulty)` to call `tollgate_core_on_share_accepted(owner_ip, difficulty)` crediting the owner session
- [ ] 5b. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes

---

## Phase 6: Dynamic AP IP and DNS

**File:** `esp-miner-nerdqaxeplus/main/tollgate_platform.cpp` (line 158)

- [ ] 6a. Replace hardcoded `192.168.4.1` with actual AP IP from `esp_netif_get_ip_info()` on AP netif
- [ ] 6b. Replace hardcoded upstream DNS `8.8.8.8` with STA gateway IP
- [ ] 6c. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes

---

## Phase 7: Sync tollgate_core to NerdQAxePlus + Build Verify

- [ ] 7a. rsync `esp32-tollgate/components/tollgate_core/` → `esp-miner-nerdqaxeplus/components/tollgate_core/`
- [ ] 7b. `make test-unit` — all esp32-tollgate tests pass
- [ ] 7c. `idf.py build` — esp32-tollgate builds
- [ ] 7d. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` — NerdQAxePlus builds

---

## Phase 8: Hardware Smoke Test on NerdQAxePlus

- [ ] 8a. Flash NerdQAxePlus with `TOLLGATE=1` to a NerdQAxe+ board
- [ ] 8b. Connect to AP, verify captive portal loads
- [ ] 8c. Test Cashu payment flow
- [ ] 8d. Connect Stratum miner to port 3334, verify shares tracked and sessions extended
- [ ] 8e. Verify board's own ASIC mining credits the owner
- [ ] 8f. Test grant/reset endpoints
- [ ] 8g. Test internet passthrough after payment

---

## Phase 9: Commit, Push, Publish

- [ ] 9a. Commit all tollgate_core changes to esp32-tollgate `feature/tollgate-core-v2`
- [ ] 9b. Push esp32-tollgate to nostr
- [ ] 9c. Commit all NerdQAxePlus integration to `develop`
- [ ] 9d. Push NerdQAxePlus to nostr
- [ ] 9e. Bump tollgate_core to v1.3.0 in `idf_component.yml`
- [ ] 9f. Re-publish to IDF Component Registry

---

## Dependency Order

```
Phase 1 → Phase 5 → Phase 7 → Phase 8
Phase 1 → Phase 4 → Phase 7 → Phase 8
Phase 2 → Phase 7
Phase 3 → Phase 7
Phase 6 → Phase 7
Phase 7 → Phase 8 → Phase 9
```

Phases 1–6 are independent (different files). Phase 7 bundles them. Phase 8 needs hardware.
