# TollGate on NerdQAxePlus/BitAxe — Fix Plan

**Created:** 2026-05-28
**Updated:** 2026-05-28
**Branch:** `feature/tollgate-core-v2` (esp32-tollgate), `develop` (NerdQAxePlus)
**Component version:** v1.3.0 (published)

---

## Phase 1: Implement 3 Missing API Functions in tollgate_core

**File:** `components/tollgate_core/src/tollgate_core.c`

- [x] 1a. Implement `tollgate_core_on_share_accepted(uint32_t client_ip, double difficulty)`
- [x] 1b. Implement `tollgate_core_calc_hashprice(double hashrate_ghs)`
- [x] 1c. Implement `tollgate_core_get_mining_status_json(void)`
- [x] 1d. Add unit tests in `tests/unit/test_mining_api.c` (19 assertions)
- [x] 1e. `make test-unit` — all 29 tests pass
- [x] 1f. `idf.py build` — esp32-tollgate builds

---

## Phase 2: Wire tick() into NerdQAxePlus Main Loop

**File:** `esp-miner-nerdqaxeplus/main/main.cpp`

- [x] 2a. Add `tollgate_core_tick()` inside the 10s loop, guarded by `#ifdef TOLLGATE`
- [x] 2b. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes

---

## Phase 3: Wire WiFi AP Client Events

**File:** `esp-miner-nerdqaxeplus/components/connect/connect.c`

- [x] 3a. Add `#include "tollgate_core.h"` guarded by `#ifdef TOLLGATE`
- [x] 3b. Add `WIFI_EVENT_AP_STACONNECTED` handler
- [x] 3c. Add `WIFI_EVENT_AP_STADISCONNECTED` handler
- [x] 3d. Build passes

**Note:** Upstream moved `connect.c` into `main/network/NetworkManager`. Our connect component was removed during rebase (Phase 7.5). WiFi AP events need to be re-integrated into NetworkManager (deferred).

---

## Phase 4: Make Stratum Proxy Functional

- [x] 4a. Line-buffer recv data, parse downstream miner messages
- [x] 4b. Use real jobs from `tollgate_core_stratum_proxy_set_job()`
- [x] 4c. Hook `create_job_mining_notify()` to forward pool jobs to proxy
- [x] 4d. Hook `mining.set_difficulty` to call `tollgate_core_mining_set_current_nbits()`
- [x] 4e. `idf.py build` passes for esp32-tollgate
- [x] 4f. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes

---

## Phase 5: Wire Share Accepted for Self-Mining Credit

- [x] 5a. `tollgate_on_share_accepted()` calls `tollgate_core_on_share_accepted(owner_ip, difficulty)`
- [x] 5b. Build passes

---

## Phase 6: Dynamic AP IP and DNS

- [x] 6a. Replace hardcoded `192.168.4.1` with actual AP IP from `esp_netif_get_ip_info()`
- [x] 6b. Replace hardcoded `8.8.8.8` with STA gateway IP
- [x] 6c. Build passes

---

## Phase 7: Sync tollgate_core to NerdQAxePlus + Build Verify

- [x] 7a. rsync tollgate_core v1.2.0 → NerdQAxePlus
- [x] 7b. `make test-unit` — all 29 tests pass
- [x] 7c. `idf.py build` — esp32-tollgate builds
- [x] 7d. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` — NerdQAxePlus builds

---

## Phase 7.5: Stratum Username Fallback + Upstream Rebase

- [x] 7.5a. Add `#ifdef TOLLGATE` fallback: use `CONFIG_STRATUM_USER` as default instead of `NULL`
- [x] 7.5b. Rebase 9 TollGate commits onto upstream `4b8f3225` (CAN bus + NetworkManager refactor)
- [x] 7.5c. Resolve conflicts in `main.cpp` (CAN slave/master split), `CMakeLists.txt`, `nvs_config.h`
- [x] 7.5d. Remove stale `connect/` component (upstream moved to `main/network/`)
- [x] 7.5e. Fix W5500 build: enable `CONFIG_ETH_SPI_ETHERNET_W5500=y`
- [x] 7.5f. Build passes: `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build`

---

## Phase 8: Hardware Smoke Test

**Board B** (MAC `fc:01:2c:c5:50:50`, `/dev/ttyACM0`) — flashed with NerdQAxePlus TOLLGATE=1 NERDQAXEPLUS
- [ ] 8a. Board B AP visible
- [ ] 8b. Connect to AP, verify captive portal loads
- [ ] 8c. Test Cashu payment flow
- [ ] 8d. Test grant/reset endpoints
- [ ] 8e. Test internet passthrough after payment
- [ ] 8f. `GET /api/tollgate/status`
- [ ] 8g. `GET /api/tollgate/wallet`
- [ ] 8h. `GET /api/tollgate/config`

**NerdAxe** (MAC `80:b5:4e:c7:79:88`, `/dev/ttyACM2`) — actual hardware with BM1366 ASIC (fan-damaged, 0 hashrate)
- [ ] 8i. Flash with `TOLLGATE=1 BOARD=NERDAXE`
- [ ] 8j. Monitor serial: check ASIC detection
- [ ] 8k. Test TollGate WiFi/payment (ASIC-independent)
- [ ] 8l. Check if ASIC responds (diagnostic)

**Board A** (MAC `94:a9:90:2e:37:7c`, `/dev/ttyACM1`) — USB flash failing, needs cable check
- [ ] 8m. Re-seat USB cable, retry flash

---

## Phase 9: Commit, Push, Publish

- [x] 9a. Commit tollgate_core changes to `feature/tollgate-core-v2`
- [x] 9b. Push esp32-tollgate to GitHub (`OpenTollGate/tollgate-esp32`) — `master` + `feature/tollgate-core-v2`
- [x] 9c. Push esp32-tollgate to nostr (`relay.ngit.dev`) — both branches
- [x] 9d. Commit NerdQAxePlus integration to `develop` (rebased onto upstream `4b8f3225`)
- [x] 9e. Push NerdQAxePlus to GitHub (`c03rad0r/ESP-Miner-NerdQAxePlus`)
- [ ] 9f. Push NerdQAxePlus to nostr (deferred — repo too large, times out)
- [x] 9g. Bump tollgate_core to v1.3.0 in `idf_component.yml`, update URL to GitHub
- [ ] 9h. Re-publish to IDF Component Registry (pending `compote component pack` + `upload`)

---

## Deferred

- **WiFi AP event hooks** — Need to integrate into upstream `NetworkManager` class (`main/network/network_manager.cpp`)
- **NerdQAxePlus nostr push** — Repo too large for relay timeout, retry later
- **Board A flash** — USB connection drops during write, hardware issue
- **ASIC diagnostic on NerdAxe** — Fan was blocked, ASIC may be damaged
- **Nostr push for orangesync** — orangesync.tech still offline
