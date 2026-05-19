# Miner Integration Plan

## Goal

Integrate TollGate paid-WiFi with Bitcoin mining hardware (BitAxe/NerdAxe family) so that
miners earn internet access by mining real Bitcoin blocks via Stratum v1. Uses the
`tollgate_core` ESP-IDF component consumed by NerdQAxePlus via the IDF Component Manager.

## Hardware Targets

| Device | Board | ASIC | Chips | Build Flag | Status |
|--------|-------|------|-------|------------|--------|
| NerdAxe Ultra 500GH/s | `NERDAXE` | BM1366 | 1 | `BOARD=NERDAXE TOLLGATE=1` | Available for testing |
| NerdQAxe++ Hydro 4.8TH/s | `NERDQAXEPLUS2` | BM1370 | 4 | `BOARD=NERDQAXEPLUS2 TOLLGATE=1` | Future |
| BitAxe Gamma 601 1.5TH/s | `GAMMA` | BM1366 | 1 | Upstream BitAxe | Future reference |

## Architecture

```
esp32-tollgate (this repo)
  components/tollgate_core/              ← shared ESP-IDF component
    CMakeLists.txt
    idf_component.yml
    include/
      tollgate_core.h                    ← public API
      tollgate_platform.h                ← platform interface (callbacks)
    src/
      tollgate_core_cashu.c/h            ← from main/cashu.c
      tollgate_core_dns.c/h              ← from main/dns_server.c
      tollgate_core_firewall.c/h         ← from main/firewall.c
      tollgate_core_session.c/h          ← from main/session.c
      tollgate_core_mining.c/h           ← from main/mining_payment.c
      tollgate_core_stratum_proxy.c/h    ← from main/stratum_proxy.c
  main/
    tollgate_platform.c                  ← standalone impl (SPIFFS config)
    ...                                  ← rest of standalone firmware

ESP-Miner-NerdQAxePlus (fork of shufps/ESP-Miner-NerdQAxePlus)
  main/idf_component.yml                 ← declares tollgate_core dependency
  main/tollgate_platform.cpp             ← implements platform interface (NVS config + ASIC state)
  main/boards/tollgate_board.h/cpp       ← TollGateBoard extends NerdAxe
  main/tasks/asic_result_task.cpp        ← 3-line #ifdef TOLLGATE hook
  main/main.cpp                          ← #ifdef TOLLGATE init block
```

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| ESP-Miner fork | NerdQAxePlus (shufps) | C++17 OOP, 9 board types, V1+V2, most maintained (877 stars) |
| Component distribution | IDF Component Manager | Clean API boundary, automatic transitive deps |
| Mining in tollgate_core | Extend existing component | Same payment core, mining hooks via platform interface |
| Component reconciliation | Cherry-pick skeleton from arch branch | Keep arch's proven interface design, fill with current master code |
| nucula wallet | Git submodule (unchanged) | Cherry-picks source files — Component Manager can't do this |

## Plan Checklist

### Step 1: Fix Master Build — COMPLETE (commit `62bce81`)

- [x] Create `components/negentropy_lib/` wrapper component (mbedTLS SHA-256 compat for negentropy submodule)
- [x] Fix `main/CMakeLists.txt` (remove `esp_littlefs`, `esp_timer`; keep `tcp_transport`)
- [x] Fix `config.c` duplicate seed_relays/sync_interval/fallback_interval blocks
- [x] Remove leftover merge conflict marker in `tollgate_api.c`
- [x] `idf.py build` passes on master (1.3MB, 68% free)
- [x] `make test-unit` passes (19 test suites, 344+ assertions)
- [x] Committed (push failed — nostr relay state event rejection)

### Step 2: Create Miner Integration Branch + Worktree — COMPLETE

- [x] Create `feature/miner-integration` branch from master
- [x] Create git worktree at `/home/c03rad0r/esp32-miner-integration`
- [x] Initialize git submodules in worktree (esp_littlefs, negentropy, nucula_src)
- [x] Verify worktree builds and tests pass

### Step 3: Cherry-pick tollgate_core Skeleton from Arch Branch — COMPLETE

- [x] Copy `components/tollgate_core/CMakeLists.txt` from `feature/tollgate-core-component`
- [x] Copy `components/tollgate_core/idf_component.yml` from `feature/tollgate-core-component`
- [x] Copy `components/tollgate_core/include/tollgate_core.h` from `feature/tollgate-core-component`
- [x] Copy `components/tollgate_core/include/tollgate_platform.h` from `feature/tollgate-core-component`
- [x] Extend `tollgate_platform.h` with mining callbacks (get_stratum_url, on_share_accepted, etc.)
- [x] Extend `tollgate_core.h` with mining API (tollgate_core_stratum_proxy_start, etc.)

### Step 4: Populate tollgate_core with Current Master Modules — COMPLETE (commit `6a61810`)

- [x] Copy from arch branch: `tollgate_core_cashu.c/h` (Cashu token decode/verify)
- [x] Copy from arch branch: `tollgate_core_dns.c/h` (per-client DNS hijack/forward)
- [x] Copy from arch branch: `tollgate_core_firewall.c/h` (per-client NAT filter)
- [x] Copy from arch branch: `tollgate_core_session.c/h` (session lifecycle)
- [x] Copy from arch branch: `tollgate_core.c` (orchestrator — init, payment, tick, owner)
- [x] Create `tollgate_core_mining.c/h` (from mining_payment.c — hashprice, share validation, client stats)
- [x] Create `tollgate_core_stratum_proxy.c/h` (from stratum_proxy.c — local SV1 TCP server)
- [x] Fix nucula_src `save_proofs` visibility (public)
- [x] `idf.py build` passes
- [x] `make test-unit` passes
- [ ] Update `components/tollgate_core/CMakeLists.txt` with all SRCS and REQUIRES

### Step 5: Wire tollgate_core into Standalone Build — DEFERRED

- [ ] Create `main/tollgate_platform.c` implementing platform interface (SPIFFS config)
- [ ] Update `main/CMakeLists.txt` — remove old SRCS, add tollgate_core to REQUIRES
- [ ] Update `main/tollgate_main.c` — call `tollgate_core_init()` instead of direct module calls
- [ ] Update `main/tollgate_api.c` — call `tollgate_core_*` API
- [ ] Update `main/lwip_tollgate_hooks.h` — call `tollgate_core_is_client_allowed()`
- [ ] `idf.py build` passes standalone
- [ ] `make test-unit` passes
- [ ] Flash to Board A + smoke test
- [ ] Commit

Note: Deferred to after NerdQAxePlus integration is working. The standalone build works fine
with existing main/ code. The component is ready for consumption by external projects.

### Step 6: Fork NerdQAxePlus + Set Up Build — COMPLETE

- [x] Clone `shufps/ESP-Miner-NerdQAxePlus` to `/home/c03rad0r/esp-miner-nerdqaxeplus/`
- [x] Initialize git submodules (libsecp256k1)
- [x] Set target to ESP32-S3: `BOARD=NERDAXE idf.py set-target esp32s3`
- [x] Verify stock build: `BOARD=NERDAXE idf.py build` — PASS (2.9MB, 29% free)
- [x] Identified key integration points:
  - `main/tasks/asic_result_task.cpp:121` — share accepted hook
  - `main/main.cpp:282` — wifi_softap_off() (must skip for TollGate AP mode)
  - `main/main.cpp:307-313` — task creation (add tollgate tasks)
  - `components/connect/connect.c:162` — APSTA mode already supported

### Step 7: Implement NerdQAxePlus TollGate Integration — COMPLETE (commit `83e09ab9`)

- [x] Create `main/tollgate_platform.cpp` — implements platform interface with NVS config + ASIC state
- [x] Create `main/tollgate_nerdqaxe.h` — init declarations for main.cpp/asic_result_task.cpp
- [x] Patch `main/tasks/asic_result_task.cpp` — `#ifdef TOLLGATE` hook on share accepted
- [x] Patch `main/main.cpp` — skip `wifi_softap_off()`, call `tollgate_nerdqaxe_init()` after mining starts
- [x] Update `main/CMakeLists.txt` — conditional TOLLGATE sources via `$ENV{TOLLGATE}`
- [x] Update top-level `CMakeLists.txt` — `-DTOLLGATE` compile definition when env var set
- [x] Add TollGate NVS keys to `main/nvs_config.h`
- [x] Symlink `components/tollgate_core` from esp32-miner-integration worktree
- [x] Build: `BOARD=NERDAXE TOLLGATE=1 idf.py build` — PASS (2.9MB)
- [x] Stock build: `BOARD=NERDAXE idf.py build` — PASS (unaffected)
- [x] tollgate_core: extern "C" guards, stratum_proxy_init name fix, conditional NAPT
- [x] `ngit init` NerdQAxePlus as separate nostr repo (`esp-miner-nerdqaxeplus-tollgate`)
- [x] Push to `git.orangesync.tech` GRASP server
- [x] Cross-reference documentation (REMOTES.md in both repos)

Note: `main/boards/tollgate_board.h/cpp` and `main/lwip_tollgate_hooks.h` deferred —
using simpler `#ifdef TOLLGATE` patches directly in existing files instead.

### Step 8: Hardware Testing on NerdAxe Ultra

- [ ] Flash stock NerdQAxePlus (no TollGate) — verify mining works (regression)
- [ ] Flash with `TOLLGATE=1` — verify:
  - [ ] Stock mining still works (hashrate normal)
  - [ ] WiFi AP starts with TollGate SSID
  - [ ] Captive portal serves payment page
  - [ ] DNS hijack/forward works (pre/post auth)
  - [ ] Local stratum proxy on port 3334 accepts downstream miners
  - [ ] Shares from downstream miners count toward internet access
  - [ ] Captive portal mining tab shows hashrate + time earned
  - [ ] LVGL display shows TollGate session info alongside mining stats
- [ ] Write integration tests
- [ ] Commit + push

### Step 9: Cleanup + Documentation

- [ ] Remove old `main/cashu.c`, `main/dns_server.c`, `main/firewall.c`, `main/session.c` from standalone (replaced by component)
- [ ] Update AGENTS.md with miner integration docs
- [ ] Update PLAN.md
- [ ] Squash-merge `feature/miner-integration` into master
- [ ] Remove worktree

## Open Questions

- [ ] Does the IDF Component Manager initialize git submodules within git-sourced deps? (nucula_src)
- [ ] Should tollgate_core publish to ESP Component Registry or stay git-only?
- [ ] Versioning scheme for tollgate_core? (semver tags in esp32-tollgate?)
- [ ] Display theme: new LVGL screen in NerdQAxePlus, or overlay on existing mining screen?

## Relevant Files

### Master (esp32-tollgate)
- `main/CMakeLists.txt` — build config (needs negentropy fix)
- `components/negentropy/` — NIP-77 set reconciliation (needs CMakeLists.txt)
- `main/mining_payment.c/h` — hashprice, share validation
- `main/stratum_proxy.c/h` — local SV1 TCP server
- `main/stratum_client.c/h` — upstream pool connection
- `main/sw_miner.c/h` — software SHA256d miner
- `main/asic_miner.c/h` — ASIC detection stub

### Arch Branch (feature/tollgate-core-component)
- `components/tollgate_core/CMakeLists.txt` — component registration
- `components/tollgate_core/idf_component.yml` — Component Manager metadata
- `components/tollgate_core/include/tollgate_core.h` — public API
- `components/tollgate_core/include/tollgate_platform.h` — platform interface
- `docs/TOLLGATE_CORE_DESIGN.md` — architecture decision record

### NerdQAxePlus (shufps/ESP-Miner-NerdQAxePlus)
- `main/boards/nerdaxe.cpp` — NerdAxe Ultra board definition (BM1366)
- `main/boards/nerdqaxeplus2.cpp` — NerdQAxe++ Hydro board definition (BM1370)
- `main/tasks/asic_result_task.cpp` — share acceptance hook point
- `main/stratum/stratum_manager.h` — Stratum V1+V2 abstraction
- `main/displays/displayDriver.cpp` — LVGL ST7789 TFT display
- `main/main.cpp` — entry point with BOARD selection
