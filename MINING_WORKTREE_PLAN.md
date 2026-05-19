# Mining Feature: Git Worktree Implementation Plan

## Overview
Implement Bitcoin mining-for-bandwidth in a proper git worktree so the shared `esp32-tollgate` repo stays clean for other LLM sessions.

## Worktree Location
- **Shared repo:** `/home/c03rad0r/esp32-tollgate` (stays on `master`, always clean)
- **Mining worktree:** `/home/c03rad0r/esp32-tollgate-mining` (on `feature/mining-payment` branch)

## Checklist

### Phase 1: Cleanup & Setup
- [x] 1.1 Backup all mining files to `/home/c03rad0r/mining-work-backup/`
- [x] 1.2 Restore shared repo to clean master (discard edits, remove untracked, delete accidental branches)
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
- [x] 2.13 N/A — esp-miner not in worktree (not needed as component)

### Phase 3: Build & Test (in worktree)
- [x] 3.1 Clean build from scratch (`rm -rf build && idf.py build`)
  - Note: Pre-existing nucula_lib build error (`save_proofs()` is private) blocks full link
  - All mining-specific source files passed compilation
  - nucula_lib error exists in both main repo and worktree (not caused by mining changes)
- [x] 3.2 Run existing unit tests (`make test-unit`)
- [x] 3.3 All tests pass (84/84: 61 existing + 23 new mining_payment)

### Phase 4: Missing Unit Tests
- [ ] 4.1 `test_stratum_proxy.c` — job management, stats
- [ ] 4.2 `test_session_payment_method.c` — payment_method field
- [ ] 4.3 `test_tollgate_client_mining.c` — mining discovery tag parsing
- [ ] 4.4 `test_firewall_sandbox.c` — sandbox allowlist logic
- [ ] 4.5 All new tests pass

### Phase 5: Commit
- [x] 5.1 Stage all changes in worktree (2 commits made)
- [x] 5.2 Commit with descriptive messages
- [ ] 5.3 Push branch to origin (Nostr git relay issue — branch exists locally)

### Phase 6: Merge (when ready)
- [ ] 6.1 Squash-merge `feature/mining-payment` into `master`
- [ ] 6.2 Remove worktree
- [ ] 6.3 Push master

## Commits Made
1. `c75230e` — feat(mining): add new mining source files and unit tests
2. `beb73a2` — feat(mining): integrate mining subsystem into existing modules

## Known Issues (pre-existing)
- `nucula_lib/nucula_wallet.cpp` calls private `save_proofs()` — build error in both repos
- Nostr git relay (`relay.ngit.dev`) rejected push — branch exists locally only

## Rules
- **NEVER** edit files in `/home/c03rad0r/esp32-tollgate/` directly
- **ALL** work happens in `/home/c03rad0r/esp32-tollgate-mining/`
- **Commit frequently** — don't lose work again
- No comments in code unless explicitly requested
