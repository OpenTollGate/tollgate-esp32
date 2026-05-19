# Mining Feature: Git Worktree Implementation Plan

## Overview
Implement Bitcoin mining-for-bandwidth in a proper git worktree so the shared `esp32-tollgate` repo stays clean for other LLM sessions.

## Status: COMPLETED — Squash-merged into master

## Checklist

### Phase 1: Cleanup & Setup
- [x] 1.1 Backup all mining files to `/home/c03rad0r/mining-work-backup/`
- [x] 1.2 Restore shared repo to clean master
- [x] 1.3 Create `feature/mining-payment` branch from master
- [x] 1.4 Create git worktree at `/home/c03rad0r/esp32-tollgate-mining`
- [x] 1.5 Copy backup files into worktree
- [x] 1.6 Verify worktree is clean and on correct branch

### Phase 2: Apply Tracked File Edits (in worktree)
- [x] 2.1 Edit `main/CMakeLists.txt` — add 6 mining source files + `tcp_transport`
- [x] 2.2 Edit `main/config.h` — add `mining_payout_mode_t` enum + mining fields
- [x] 2.3 Edit `main/config.c` — add mining defaults + JSON parsing
- [x] 2.4 Edit `main/tollgate_main.c` — mining includes, init, tick
- [x] 2.5 Edit `main/tollgate_api.c` — 3 mining endpoints + discovery tag
- [x] 2.6 Edit `main/session.h` — `payment_method_t` enum + field
- [x] 2.7 Edit `main/session.c` — set payment_method in create functions
- [x] 2.8 Edit `main/firewall.h` — `firewall_set_mining_port()` + `firewall_set_sandbox_mint_access()`
- [x] 2.9 Edit `main/firewall.c` — sandbox allowlist + includes
- [x] 2.10 Edit `main/tollgate_client.h` — `TG_CLIENT_MINING` state + mining discovery fields
- [x] 2.11 Edit `main/tollgate_client.c` — mining tag parsing in discovery
- [x] 2.12 Edit `main/captive_portal.c` — tabbed UI with Cashu/Mine tabs

### Phase 3: Build & Test (in worktree)
- [x] 3.1 Clean build from scratch
- [x] 3.2 Run existing unit tests (`make test-unit`)
- [x] 3.3 All tests pass

### Phase 4: Missing Unit Tests
- [x] 4.1 `test_stratum_proxy.c` — job management, stats
- [x] 4.2 `test_session_payment_method.c` — payment_method field
- [x] 4.3 `test_tollgate_client_mining.c` — mining discovery tag parsing
- [x] 4.4 `test_firewall_sandbox.c` — sandbox allowlist logic
- [x] 4.5 All new tests pass

### Phase 5: Commit
- [x] 5.1 Stage all changes in worktree (5 commits made)
- [x] 5.2 Commit with descriptive messages
- [x] 5.3 Push branch to origin (state event published to relay.ngit.dev)

### Phase 6: Merge
- [x] 6.1 Squash-merge `feature/mining-payment` into `master` (resolved merge conflicts)
- [x] 6.2 Remove worktree
- [x] 6.3 Push master to origin
- [x] 6.4 Delete feature branch
- [x] 6.5 All 19 unit test suites pass on master

## Commits on feature/mining-payment (before squash)
1. `c75230e` — feat(mining): add new mining source files and unit tests
2. `beb73a2` — feat(mining): integrate mining subsystem into existing modules
3. `473b4d1` — fix: build errors in cvm_server, stratum_proxy, sw_miner + nucula visibility
4. `ef9ae98` — test: add 4 new unit test suites for mining modules
5. `9d98ba1` — feat: expand nostr relay lists to max capacity

## Squash commit on master
- `e366ceb` — feat(mining): Bitcoin mining-for-bandwidth payment system
- `55917e0` — fix: resolve merge conflicts + test build fixes

## Backup
- `/home/c03rad0r/mining-work-backup/feature-mining-payment.bundle` — full branch history
