# TollGate Core Component: Architecture Design

## Goal

Maintain all TollGate business logic in `esp32-tollgate` as a reusable ESP-IDF
component (`tollgate_core`), and consume it in `esp-miner` (BitAxe) via the
**IDF Component Manager**. No code duplication, no manual sync.

## Current State (Pre-Refactoring)

All TollGate modules live flat in `esp32-tollgate/main/`:

```
esp32-tollgate/main/
  cashu.c / cashu.h
  dns_server.c / dns_server.h
  firewall.c / firewall.h
  session.c / session.h
  tollgate_api.c / tollgate_api.h
  tollgate_client.c / tollgate_client.h
  config.c / config.h
  ...
```

The ESP-Miner port (`esp-miner/main/tollgate_*.c`) is a manual copy with edits:
stripped prefixes (`cashu_` → `tollgate_cashu_`), NVS config instead of
`config.h` singleton, removed wallet integration, moved cross-module wiring.

### Shared Code by Module

| Module | Shared % | Key Differences |
|--------|----------|-----------------|
| cashu | 73% | Config access, mint check parameterized |
| dns_server | 74% | Minor logic reorder, logging stripped |
| firewall | 94% | Cross-module DNS notification moved |
| session | 79% | Bytes metric stripped, DNS notification added |
| tollgate_api vs tollgate.c | 13% | Full rewrite (HTTP server vs library API) |
| tollgate_client | 0% | No ESP-Miner equivalent |

## Target Architecture

### Directory Layout (in `esp32-tollgate`)

```
esp32-tollgate/
  components/
    tollgate_core/                    ← shared ESP-IDF component
      CMakeLists.txt
      idf_component.yml              ← component metadata for IDF Component Manager
      include/
        tollgate_core.h              ← public API
        tollgate_platform.h          ← platform interface (config/state callbacks)
      src/
        tollgate_core_cashu.c        ← from main/cashu.c
        tollgate_core_cashu.h
        tollgate_core_dns.c          ← from main/dns_server.c
        tollgate_core_dns.h
        tollgate_core_firewall.c     ← from main/firewall.c
        tollgate_core_firewall.h
        tollgate_core_session.c      ← from main/session.c
        tollgate_core_session.h
    nucula_lib/                       ← stays as-is (git submodule + wrapper)
      CMakeLists.txt
      nucula_wallet.cpp / .h
  main/
    tollgate_platform.c               ← standalone impl of tollgate_platform.h
    tollgate_api.c / .h               ← standalone HTTP server (unchanged)
    tollgate_client.c / .h            ← standalone client mode (unchanged)
    config.c / config.h               ← standalone config (unchanged)
    ...
```

### How ESP-Miner Consumes It

In `esp-miner/main/idf_component.yml`:

```yaml
dependencies:
  tollgate/core:
    git: https://github.com/<user>/esp32-tollgate.git
    path: components/tollgate_core
```

ESP-Miner provides only:

```
esp-miner/main/
  tollgate_platform.c               ← implements tollgate_platform.h (NVS config)
  tollgate.c / .h                    ← ESP-Miner orchestrator (owner detection, WiFi events)
  tollgate_page.html                 ← captive portal payment UI
  lwip_tollgate_hooks.h              ← LWIP hook (stays in esp-miner)
  http_server.c                      ← modified to call tollgate_core API
```

### Why IDF Component Manager (not submodule)

| Aspect | IDF Component Manager | Git Submodule |
|--------|----------------------|---------------|
| What's downloaded | Only `components/tollgate_core/` | Entire `esp32-tollgate` repo |
| Update mechanism | Modify version in yml, rebuild | Manual `git submodule update` |
| Transitive deps | Automatic (nucula_lib resolved) | Must manage manually |
| CI/CD | Automatic on `idf.py build` | Needs `--recursive` clone |
| Offline after first build | Yes (cached in managed_components) | Yes |
| Contributor friction | Low (automatic) | Moderate (forgot --recursive) |

ESP-Miner never reaches into tollgate_core's source tree. It calls a clean API
and provides a platform implementation. This is exactly the "packaged API
consumption" pattern the Component Manager is designed for.

### Why Git Submodule for nucula (not Component Manager)

nucula is consumed differently — it's a **raw source integration**:

```cmake
# nucula_lib/CMakeLists.txt reaches INTO the submodule and cherry-picks files:
set(NUCULA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../nucula_src/main)
idf_component_register(
    SRCS "nucula_wallet.cpp"
         "${NUCULA_SRC}/crypto.c"       # cherry-picked
         "${NUCULA_SRC}/wallet.cpp"     # cherry-picked
         "${NUCULA_SRC}/cashu_json.cpp" # cherry-picked (6 of ~20 files)
         "${NUCULA_SRC}/nut10.cpp"
         "${NUCULA_SRC}/hex.c"
         "${NUCULA_SRC}/http.c"
    ...
)
```

The Component Manager downloads packaged components — you get everything or
nothing. You can't say "give me this component but only compile these 6 files
from it." A git submodule gives you the raw source tree on disk, which is what
cherry-picking requires.

**Principle:** Need to reach into source tree and pick files? → Submodule.
Only need a clean API? → Component Manager.

### The Platform Interface

```c
// components/tollgate_core/include/tollgate_platform.h

#ifndef TOLLGATE_PLATFORM_H
#define TOLLGATE_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // Config access (each project implements its own storage)
    uint16_t     (*get_price_sats)(void);
    int32_t      (*get_step_ms)(void);
    const char * (*get_mint_url)(void);
    const char * (*get_metric)(void);         // "milliseconds" or "bytes"
    int32_t      (*get_step_bytes)(void);

    // Time source
    int64_t      (*get_time_ms)(void);

    // Wallet integration: called after proofs verified, before session create
    // Return true to proceed, false to reject payment
    // Can be NULL (accepts payment without spending proofs — double-spend risk)
    bool         (*spend_proofs)(const char *raw_token_json);
} tollgate_platform_t;

#endif
```

**Standalone implementation** (`main/tollgate_platform.c`):
- Reads from `tollgate_config_get()` singleton (SPIFFS-backed)
- `spend_proofs` calls `nucula_wallet_receive()` to swap proofs at the mint

**ESP-Miner implementation** (`main/tollgate_platform.c`):
- Reads from `nvs_config_get_*()` (NVS flash)
- `spend_proofs` is initially NULL (Phase 1: accept without spending)
- Later: calls nucula_wallet_receive when wallet component is integrated

### Wallet Integration: The Double-Spend Problem

The `spend_proofs` hook exists because of a real security gap:

```
Client sends Cashu token
    │
    ▼
cashu_decode_token()          ← extract proofs
    │
    ▼
cashu_check_proof_states()    ← HTTP POST to mint /v1/checkstate: "unspent?"
    │
    ▼
spend_proofs()                ← THE CRITICAL STEP
    │                              standalone: nucula_wallet_receive() → swap at mint
    │                              esp-miner: NULL → skipped (double-spend window)
    ▼
session_create()              ← grant client access
```

Without `spend_proofs`, a client can replay the same token on multiple devices.
Both check "unspent?" → both say yes → both grant access. The swap step marks
proofs as spent at the mint, closing the window.

ESP-Miner accepts this risk initially. When `spend_proofs` is NULL, the
component logs a warning. Phase 2 of ESP-Miner integration adds nucula and
implements the hook.

### Cross-Module Wiring (Internal to tollgate_core)

The `session → firewall → dns_server` notification chain stays internal:

```
tollgate_core_session_create()
  → tollgate_core_firewall_grant(ip)
      → tollgate_core_dns_set_authenticated(ip, true)

tollgate_core_session_revoke()
  → tollgate_core_firewall_revoke(ip)
      → tollgate_core_dns_set_authenticated(ip, false)
```

Consumers never see this. They call `tollgate_core_process_payment()` and
`tollgate_core_tick()`. The internal wiring is an implementation detail.

### Full Dependency Graph

```
esp-miner
  └── IDF Component Manager → tollgate_core (API-level boundary)
        ├── CMakeLists.txt REQUIRES: nucula_lib
        └── Platform: esp-miner provides tollgate_platform_t (NVS-backed)

esp32-tollgate (standalone)
  └── tollgate_core (local component, same repo)
        ├── CMakeLists.txt REQUIRES: nucula_lib
        └── Platform: main/tollgate_platform.c (config singleton-backed)

nucula_lib (local component in esp32-tollgate)
  └── cherry-picks source files from nucula_src/ (git submodule → zeugmaster/nucula)
```

### Dependency Chain for IDF Component Manager

When `esp-miner` declares:

```yaml
dependencies:
  tollgate/core:
    git: https://github.com/<user>/esp32-tollgate.git
    path: components/tollgate_core
```

The Component Manager:
1. Clones `esp32-tollgate` (or fetches the component archive)
2. Reads `tollgate_core/idf_component.yml` → finds dependency on `nucula_lib`
3. Since `nucula_lib` is a sibling component in the same repo, resolves it
   from the same clone
4. Downloads into `managed_components/`
5. `nucula_lib` depends on `secp256k1` (local component) and `nucula_src`
   (submodule) — these must be available within the cloned repo

**Note:** The git submodule within `nucula_src` needs verification. The IDF
Component Manager may or may not initialize submodules within a git-sourced
dependency. This needs testing. If it doesn't, `nucula_lib` may need to bundle
the required nucula source files directly instead of referencing a submodule.

## Blocking Dependencies

This refactoring **must not proceed** until these branches land on master:

| Branch | Blocking Files | Status |
|--------|---------------|--------|
| `feature/multi-mint-support` | `cashu.c`, `tollgate_api.c`, `main/CMakeLists.txt`, `nucula_wallet.cpp/h`, `captive_portal.c`, `mint_health.c/h`, `config.c/h` | **In progress** |
| `feature/price-discovery` | `tollgate_api.c`, `tollgate_client.c`, `main/CMakeLists.txt`, `config.c/h`, `beacon_price.c/h`, `market.c/h` | **In progress** |
| `feature/cvm-integration` | Same commit as master — no new changes | **Merged already** |

**Specific conflicts if we refactor now:**
- Moving `cashu.c` → `tollgate_core_cashu.c` while multi-mint modifies `cashu.c`
- Moving `dns_server.c` while price-discovery may touch it
- Modifying `main/CMakeLists.txt` (remove SRCS) while all branches modify it
- Modifying `tollgate_api.c` call sites while multi-mint and price-discovery modify it

## Refactoring Plan (After Blocking PRs Merge)

### Phase 0: Prerequisites

- [ ] All blocking PRs merged to master
- [ ] This branch rebased onto latest master
- [x] Full build passes on master

### Phase 1: Create Component Skeleton

- [x] Create `components/tollgate_core/` directory structure
- [x] Create `components/tollgate_core/include/tollgate_core.h` (public API)
- [x] Create `components/tollgate_core/include/tollgate_platform.h` (platform interface)
- [x] Create `components/tollgate_core/idf_component.yml` (component metadata)
- [x] Create `components/tollgate_core/CMakeLists.txt` (register component)
- [ ] Verify empty component builds without errors

### Phase 2: Move Core Modules (one at a time, build after each)

- [x] Copy `main/cashu.c/h` → `components/tollgate_core/src/tollgate_core_cashu.c/h`
  - [x] Rename functions: `cashu_*` → `tollgate_core_cashu_*`
  - [x] Replace `tollgate_config_get()` calls with parameterized arguments
  - [x] Remove direct `config.h` include
  - [ ] Build and verify
- [x] Copy `main/dns_server.c/h` → `components/tollgate_core/src/tollgate_core_dns.c/h`
  - [x] Rename functions: `dns_server_*` → `tollgate_core_dns_*`
  - [x] No platform dependencies (pure LWIP) — clean copy
  - [ ] Build and verify
- [x] Copy `main/firewall.c/h` → `components/tollgate_core/src/tollgate_core_firewall.c/h`
  - [x] Rename functions: `firewall_*` → `tollgate_core_firewall_*` / `tollgate_core_fw_*`
  - [x] Internalize `dns_set_authenticated` calls (kept within component)
  - [x] Remove `dns_server.h` external dependency
  - [ ] Build and verify
- [x] Copy `main/session.c/h` → `components/tollgate_core/src/tollgate_core_session.c/h`
  - [x] Rename functions: `session_*` → `tollgate_core_session_*`
  - [x] Replace `config.h` calls with platform callbacks for metric check
  - [x] Internalize firewall notification (already calls firewall directly)
  - [x] Support both time and bytes metrics (portable, not stripped)
  - [ ] Build and verify

### Phase 3: Wire Component API

- [x] Implement `tollgate_core_init(const tollgate_platform_t *platform, esp_ip4_addr_t ap_ip)` — stores platform, inits all sub-modules
- [x] Implement `tollgate_core_process_payment(ip, token)` — decode → verify → spend → create session
- [x] Implement `tollgate_core_client_connected(mac, ip)` — owner detection + firewall check
- [x] Implement `tollgate_core_client_disconnected(mac)` — session cleanup + owner reassign
- [x] Implement `tollgate_core_tick()` — session expiry check
- [x] Implement `tollgate_core_get_status_json()` — JSON status
- [x] Implement `tollgate_core_get_config_json()` — JSON config (via platform)
- [ ] Build and verify standalone

### Phase 4: Standalone Platform Implementation

- [x] Create `main/tollgate_platform.c` implementing `tollgate_platform_t`
  - [x] `get_price_sats` → `tollgate_config_get()->price_per_step`
  - [x] `get_step_ms` → `tollgate_config_get()->step_size`
  - [x] `get_mint_url` → `tollgate_config_get()->mint_url`
  - [x] `get_metric` → `tollgate_config_get()->metric`
  - [x] `get_step_bytes` → `tollgate_config_get()->step_bytes`
  - [x] `get_time_ms` → `xTaskGetTickCount() * portTICK_PERIOD_MS`
  - [x] `spend_proofs` → stub returning true (wallet called separately)
- [x] Update `main/tollgate_api.c` to call `tollgate_core_*` instead of direct module calls
- [x] Update `main/tollgate_main.c` init sequence
- [x] Remove old `main/cashu.c`, `main/dns_server.c`, `main/firewall.c`, `main/session.c` from CMakeLists.txt
- [x] Update `main/CMakeLists.txt` (remove old SRCS, add `tollgate_platform.c`, add `tollgate_core` to REQUIRES)
- [x] Update `main/lwip_tollgate_hooks.h` to call `tollgate_core_ip4_canforward_filter`
- [ ] Full standalone build + test

### Phase 5: ESP-Miner Integration

- [ ] Update `esp-miner/main/idf_component.yml` to add tollgate_core dependency
- [ ] Create `esp-miner/main/tollgate_platform.c` implementing `tollgate_platform_t`
  - [ ] Config reads from NVS (`nvs_config_get_*`)
  - [ ] `spend_proofs` = NULL initially (Phase 1: accept without spending)
- [ ] Update `esp-miner/main/tollgate.c` to call `tollgate_core_*` API
- [ ] Remove `esp-miner/main/tollgate_cashu.c`, `tollgate_dns.c`, `tollgate_firewall.c`, `tollgate_session.c`
- [ ] Update `esp-miner/main/CMakeLists.txt` (remove old SRCS)
- [ ] Full ESP-Miner build + test

### Phase 6: Verify Component Manager Flow

- [ ] Remove local `managed_components/` if present
- [ ] Run `idf.py reconfigure` in esp-miner — verify Component Manager downloads tollgate_core
- [ ] Run `idf.py build` — verify transitive dependency resolution (nucula_lib + nucula_src)
- [ ] Test that submodule within nucula_src is properly initialized by Component Manager
- [ ] If submodule init fails: bundle nucula source files directly in nucula_lib instead

### Phase 7: Documentation and Cleanup

- [ ] Update `esp-miner/main/idf_component.yml` with correct git URL
- [ ] Update `esp-miner/TOLLGATE_PR_PLAN.md` to reflect component-based architecture
- [ ] Add `docs/` to `tollgate_core` with integration guide for new consumers
- [ ] Update `esp-miner/TOLLGATE_CHECKLIST.md`
- [ ] Verify both projects build clean from scratch

## Open Questions

- [ ] Does the IDF Component Manager initialize git submodules within git-sourced dependencies?
- [ ] Should tollgate_core publish to the ESP Component Registry (public) or stay git-only?
- [ ] What versioning scheme for tollgate_core? (semver tags in esp32-tollgate?)
- [ ] Should `tollgate_client.c` (client mode) eventually move into tollgate_core?
