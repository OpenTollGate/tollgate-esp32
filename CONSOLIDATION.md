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
- [x] **1.6** Verify unit tests pass: `make test-unit` — 16/16 PASS
- [x] **1.7** Commit + push

## Phase 2: Merge feature/display-fix (touch + WiFi setup UI)

- [x] **2.1** Verify feature/display-fix has no uncommitted source changes (only binaries — confirmed)
- [x] **2.2** Squash-merge feature/display-fix into master
- [x] **2.3** Resolve conflicts: took master for config/cvm/api/main, display-fix for display/axs15231b
- [x] **2.4** Fix display_update() call signature (7 args), add tollgate_config_add_wifi()
- [x] **2.5** Verify build: `idf.py build` — PASS
- [x] **2.6** Verify unit tests: `make test-unit` — 16/16 PASS
- [x] **2.7** Commit + push

## Phase 3: Merge feature/miner-integration (full tollgate_core)

- [x] **3.1** Verify feature/miner-integration has no uncommitted source changes (only binaries — confirmed)
- [x] **3.2** Replace tollgate_core with full version (13 files, 22 callbacks, mining + stratum + extern C)
- [x] **3.3** Commit: "feat: upgrade tollgate_core to full version with mining + stratum"
- [x] **3.4** Copy MINER_INTEGRATION_PLAN.md from feature branch
- [x] **3.5** Verify build: `idf.py build` — PASS
- [x] **3.6** Verify unit tests: `make test-unit` — 16/16 PASS
- [x] **3.7** Commit + push

## Phase 4: Restructure — Option B (main/ calls tollgate_core API)

Move module logic from main/ into components/tollgate_core/, making main/ a thin shell.

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

- [x] **6.1** Delete feature/tollgate-core-component branch (merged via Phase 1)
- [x] **6.2** Delete backup/multi-mint-support-pre-rebase branch (fully in master)
- [x] **6.3** Delete feature/display-fix branch (merged in Phase 2)
- [x] **6.4** Delete feature/miner-integration branch (merged in Phase 3)
- [x] **6.5** Remove worktree: esp32-miner-integration/
- [x] **6.6** Remove worktree: esp32-tollgate-arch/
- [x] **6.7** Remove worktree: esp32-tollgate-display/
- [x] **6.8** Clean up test binaries from git tracking: `git rm --cached`
- [x] **6.9** Refresh backup bundles
- [x] **6.10** Push to orangesync + origin

## Phase 7: Update esp-miner (optional, after consolidation)

- [ ] **7.1** Switch esp-miner from flat-file modules to tollgate_core via Component Manager
- [ ] **7.2** Create main/idf_component.yml with git dependency on esp32-tollgate
- [ ] **7.3** Delete flat-file TollGate modules from main/ (tollgate_cashu/dns/firewall/session.c/h)
- [ ] **7.4** Create main/tollgate_platform.c implementing tollgate_platform_t with NVS backend
- [ ] **7.5** Verify build
- [ ] **7.6** Commit + push

---

## Current State Summary

### Repos

| Repo | Path | Branch | HEAD | Status |
|------|------|--------|------|--------|
| esp32-tollgate | /home/c03rad0r/esp32-tollgate | master | 897a8d9 | Canonical source |
| esp-miner-nerdqaxeplus | /home/c03rad0r/esp-miner-nerdqaxeplus | develop | 588c9f67 | Consumer |
| esp-miner | /home/c03rad0r/esp-miner | master | a8dc494 | Consumer (Phase 1 done) |

### Branches (esp32-tollgate)

| Branch | Worktree | Merged? | Unique Content | Action |
|--------|----------|---------|----------------|--------|
| master | esp32-tollgate | — | Current production code | Keep |
| feature/display-fix | esp32-tollgate-display | NO | touch.c/h, keyboard.c/h, wifi_setup.c/h, 4 unit tests, E2E test, WiFi QR, error state | Merge (Phase 2) |
| feature/miner-integration | esp32-miner-integration | PARTIAL | Full tollgate_core (mining+stratum+extern C+22 callbacks), docs | Merge (Phase 3) |
| feature/tollgate-core-component | esp32-tollgate-arch | YES (be4788b) | None — superseded by miner-integration | Delete (Phase 6) |
| backup/multi-mint-support-pre-rebase | (none) | YES | None | Delete (Phase 6) |

### Stashes (esp32-tollgate)

| Stash | Content | Action |
|-------|---------|--------|
| stash@{0} | Recompiled test binaries (no source changes) | Drop |
| stash@{1} | AP-only services, market config wiring, sta_connecting guard | Apply (Phase 1) |
| stash@{2} | Recompiled test binaries (no source changes) | Drop |
| stash@{3} | Recompiled test binaries (no source changes) | Drop |

### Tollgate Core Divergence

| File | Master (old skeleton) | Miner-integration (full) |
|------|----------------------|--------------------------|
| tollgate_core.h | 34 lines, no extern C, no mining API | 48 lines, extern C, 5 mining functions |
| tollgate_platform.h | 17 lines, 7 callbacks | 37 lines, extern C, 22 callbacks |
| CMakeLists.txt | 9 source files (no mining/stratum) | 11 source files (mining+stratum) |
| tollgate_core_firewall.c | unconditional NAPT | conditional CONFIG_LWIP_IPV4_NAPT |
| tollgate_core_mining.c | MISSING | Present (168 lines) |
| tollgate_core_mining.h | MISSING | Present (35 lines) |
| tollgate_core_stratum_proxy.c | MISSING | Present (160 lines) |
| tollgate_core_stratum_proxy.h | MISSING | Present (39 lines) |
