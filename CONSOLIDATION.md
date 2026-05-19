# Consolidation Plan

Merge all feature branches into master, establish tollgate_core as the single source of truth,
and clean up worktrees/stashes/stale branches.

## End State

```
esp32-tollgate (master only)
  main/ = thin shell calling tollgate_core API
  components/tollgate_core/ = THE reusable core (cashu, dns, firewall, session, mining, stratum)

esp-miner-nerdqaxeplus (develop)
  references tollgate_core via IDF Component Manager (override_path for local dev)

esp-miner (master)
  references tollgate_core via IDF Component Manager (after its feature/tollgate-component-integration)
```

---

## Phase 1: Preserve Uncommitted Work

- [x] **1.1** Revert broken merge (be4788b) that gutted config.c/tollgate_main.c/tollgate_api.c
- [x] **1.2** Restore tollgate_core component + tollgate_platform.c + docs from merge
- [x] **1.3** Wire market config fields in config.c
- [x] **1.4** Drop all 4 stashes (binaries only, content already in master)
- [x] **1.5** Verify master builds: `idf.py build` — PASS
- [x] **1.6** Verify unit tests pass: `make test-unit` — 18/18 suites PASS
- [x] **1.7** Commit + push

## Phase 2: Merge feature/display-fix (touch + WiFi setup UI)

- [x] **2.1** Verify feature/display-fix has no uncommitted source changes (only binaries — confirmed)
- [x] **2.2** Squash-merge feature/display-fix into master
- [x] **2.3** Resolve conflicts: took master for config/cvm/api/main, display-fix for display/axs15231b
- [x] **2.4** Fix display_update() call signature (7 args), add tollgate_config_add_wifi()
- [x] **2.5** Verify build: `idf.py build` — PASS
- [x] **2.6** Verify unit tests: `make test-unit` — 18/18 suites PASS
- [x] **2.7** Commit + push

## Phase 3: Merge feature/miner-integration (full tollgate_core)

- [x] **3.1** Verify feature/miner-integration has no uncommitted source changes (only binaries — confirmed)
- [x] **3.2** Replace tollgate_core with full version (13 files, 22 callbacks, mining + stratum + extern C)
- [x] **3.3** Commit: "feat: upgrade tollgate_core to full version with mining + stratum"
- [x] **3.4** Copy MINER_INTEGRATION_PLAN.md from feature branch
- [x] **3.5** Verify build: `idf.py build` — PASS
- [x] **3.6** Verify unit tests: `make test-unit` — 18/18 suites PASS
- [x] **3.7** Commit + push

## Phase 4: Restructure — Option B (main/ calls tollgate_core API)

Move module logic from main/ into components/tollgate_core/, making main/ a thin shell.

**Status: DEFERRED** — requires careful API design. Both main/ and tollgate_core currently have
independent implementations of the same modules. The restructure would make main/ delegate to
tollgate_core for all shared logic.

- [ ] **4.1** Identify modules in main/ that have tollgate_core equivalents:
  - cashu.c/h → tollgate_core_cashu.c/h
  - dns_server.c/h → tollgate_core_dns.c/h
  - firewall.c/h → tollgate_core_firewall.c/h
  - session.c/h → tollgate_core_session.c/h
  - mining_payment.c/h → tollgate_core_mining.c/h
  - stratum_proxy.c/h → tollgate_core_stratum_proxy.c/h
- [ ] **4.2** For each module: replace main/*.c implementation with calls to tollgate_core API
  - Keep the main/*.c files as thin wrappers that delegate to tollgate_core
  - OR remove them entirely if tollgate_core API is called directly from tollgate_main.c
- [ ] **4.3** Update main/CMakeLists.txt — remove replaced source files from SRCS
- [ ] **4.4** Verify build: `idf.py build`
- [ ] **4.5** Verify unit tests: `make test-unit`
- [ ] **4.6** Run integration tests if hardware available: `make test-integration`
- [ ] **4.7** Commit: "refactor: main/ delegates to tollgate_core component"
- [ ] **4.8** Push

## Phase 5: Wire up NerdQAxePlus via Component Manager

**Status: DEFERRED** — depends on Phase 4. Currently uses a broken symlink
(pointing to removed worktree).

- [ ] **5.1** Create main/idf_component.yml in NerdQAxePlus:
  ```yaml
  dependencies:
    tollgate_core:
      override_path: /home/c03rad0r/esp32-tollgate/components/tollgate_core
  ```
- [ ] **5.2** Remove symlink: rm esp-miner-nerdqaxeplus/components/tollgate_core
- [ ] **5.3** Update main/CMakeLists.txt — remove hardcoded include path, use component dependency
- [ ] **5.4** Verify build: `BOARD=NERDAXE TOLLGATE=1 idf.py build`
- [ ] **5.5** Commit + push

## Phase 6: Delete Branches & Worktrees

- [x] **6.1** Delete feature/tollgate-core-component branch
- [x] **6.2** Delete backup/multi-mint-support-pre-rebase branch
- [x] **6.3** Delete feature/display-fix branch
- [x] **6.4** Delete feature/miner-integration branch
- [x] **6.5** Remove worktree: esp32-miner-integration/
- [x] **6.6** Remove worktree: esp32-tollgate-arch/
- [x] **6.7** Remove worktree: esp32-tollgate-display/
- [x] **6.8** Clean up test binaries from git tracking: `git rm --cached` (20 binaries)
- [x] **6.9** Refresh backup bundles
- [x] **6.10** Push to orangesync + origin

## Phase 7: Update esp-miner (optional, after Phase 4)

- [ ] **7.1** Switch esp-miner from flat-file modules to tollgate_core via Component Manager
- [ ] **7.2** Create main/idf_component.yml with git dependency on esp32-tollgate
- [ ] **7.3** Delete flat-file TollGate modules from main/ (tollgate_cashu/dns/firewall/session.c/h)
- [ ] **7.4** Create main/tollgate_platform.c implementing tollgate_platform_t with NVS backend
- [ ] **7.5** Verify build
- [ ] **7.6** Commit + push

---

## Current State

### Repos

| Repo | Path | Branch | HEAD | Status |
|------|------|--------|------|--------|
| esp32-tollgate | /home/c03rad0r/esp32-tollgate | master | 32e1bfd | **Consolidated** — build + tests pass |
| esp-miner-nerdqaxeplus | /home/c03rad0r/esp-miner-nerdqaxeplus | develop | 588c9f67 | Consumer — broken symlink needs fix |
| esp-miner | /home/c03rad0r/esp-miner | master | a8dc494 | Consumer — Phase 1 flat-file done |

### esp32-tollgate: Clean

- **1 branch** (master), **0 worktrees**, **0 stashes**
- **Build**: `idf.py build` — PASS (2.9MB firmware)
- **Unit tests**: `make test-unit` — 18/18 suites, 400+ assertions, all PASS
- **Pushed to**: orangesync (git.orangesync.tech) + origin (relay.ngit.dev)

### Modules in main/ vs tollgate_core

| main/ module | tollgate_core equivalent | Duplicated? |
|-------------|------------------------|-------------|
| cashu.c/h | tollgate_core_cashu.c/h | Yes — both exist |
| dns_server.c/h | tollgate_core_dns.c/h | Yes |
| firewall.c/h | tollgate_core_firewall.c/h | Yes |
| session.c/h | tollgate_core_session.c/h | Yes |
| mining_payment.c/h | tollgate_core_mining.c/h | Yes |
| stratum_proxy.c/h | tollgate_core_stratum_proxy.c/h | Yes |
| config.c/h | — | No (unique to esp32-tollgate) |
| identity.c/h | — | No |
| display.c/h | — | No |
| touch.c/h | — | No (new from display-fix) |
| keyboard.c/h | — | No (new from display-fix) |
| wifi_setup.c/h | — | No (new from display-fix) |
| captive_portal.c/h | — | No |
| tollgate_api.c/h | — | No |
| tollgate_client.c/h | — | No |
| cvm_server.c/h | — | No |
| local_relay.c/h | — | No |
| wifistr.c/h | — | No |
| beacon_price.c/h | — | No |
| market.c/h | — | No |
| mint_health.c/h | — | No |
| lightning_payout.c/h | — | No |
| mcp_handler.c/h | — | No |
| nostr_event.c/h | — | No |
| geohash.c/h | — | No |
| lnurl_pay.c/h | — | No |
| nip04.c/h | — | No |
| font.c/h | — | No |
| negentropy_adapter.c | — | No |
| stratum_client.c/h | — | No |
| sw_miner.c/h | — | No |
| asic_miner.c/h | — | No |
| relay_selector.c/h | — | No |
| sync_manager.c/h | — | No |

### Known Issues

1. **NerdQAxePlus broken symlink** — `esp-miner-nerdqaxeplus/components/tollgate_core` points to
   removed worktree `esp32-miner-integration`. Needs Phase 5 fix.
2. **esp-miner flat-file duplication** — has its own copy of cashu/dns/firewall/session in main/,
   independent from tollgate_core. Needs Phase 7 to consolidate.
3. **tollgate_core not yet used by main/** — main/ still calls its own modules directly.
   tollgate_core exists as a component but is only linked, not called (except by tollgate_platform.c).
   Phase 4 would make main/ delegate to tollgate_core.
