# Relay Hardening Merge Plan

## Problem

Master at `abee221` is **broken** — the `eeb9d2d` commit (from cvm-relay-stability worktree) removed display/relay CMakeLists entries and tollgate_main includes because that worktree didn't have those modules. The hardening branch at `8d58cef` was based on `81f2dc5` which has the correct complete set.

### Branch State

| Branch | HEAD | Based On | Status |
|--------|------|----------|--------|
| `master` | `abee221` | — | Broken (missing CMakeLists entries) |
| `feature/relay-hardening` | `8d58cef` | `81f2dc5` | 7 commits, all unit tests pass |
| `81f2dc5` | Original relay squash-merge | — | Last known-good build |

### What `eeb9d2d` broke on master
- Removed `display.c`, `font.c`, `local_relay.c`, `relay_selector.c`, `sync_manager.c` from `main/CMakeLists.txt` SRCS
- Removed `axs15231b`, `qrcode`, `wisp_relay` from REQUIRES
- Removed `display.h`, `local_relay.h`, `relay_selector.h`, `sync_manager.h` includes from `tollgate_main.c`
- Removed `display_init()` and `display_set_state()` calls from `tollgate_main.c`
- BUT kept `relay_selector_t`, `sync_manager_t`, `local_relay_*()` calls that reference these modules

### What `eeb9d2d` improved on master
- CVM server WS keepalive (ping/pong every 30s)
- TLS read timeout reduced from 15s to 1s
- Consecutive timeout counter (65s) for disconnect detection
- Relay close frame handling (opcode 0x08)
- Added `test-cvm-mcp-relay.mjs` integration test
- Added `CHECKLIST-CVM-RELAY.md`

---

## Strategy

**Soft-reset squash**: Reset hardening branch to master, manually compose the correct index, single-commit merge via fast-forward.

---

## Checklist

### Step 1: Backup
- [x] Create backup tags
- [x] Create backup branch `feature/relay-hardening-backup`

### Step 2: Compose Final State
- [x] Soft-reset hardening worktree to master
- [x] Restore `main/CMakeLists.txt` from `81f2dc5` (has all source files and deps)
- [x] Restore `main/tollgate_main.c` from `81f2dc5` (has display + relay includes and calls)
- [x] Keep `main/cvm_server.c` from master (has keepalive/timeout fixes)
- [x] Keep `main/display.c` with non-static `escape_wifi_field`
- [x] Stage new files: `negentropy_adapter.c/h`, `test_display.c`, `test_negentropy_adapter.c`, `test-cvm-roundtrip.mjs`, `test-cross-board.mjs`, `RELAY_HARDENING_PLAN.md`
- [x] Stage updated files: `Makefile`, `AGENTS.md`, `tests/unit/Makefile`
- [x] Delete `CHECKLIST-CVM-RELAY.md`
- [x] Delete `PLAN-SQUASH-MERGE.md`
- [x] Keep `test-cvm-mcp-relay.mjs` (from master)
- [x] Keep `components/esp-miner` removed (from master)

### Step 3: Verify
- [x] `git diff --cached --stat` matches expected file list
- [x] `git diff --cached -- main/cvm_server.c` shows master's keepalive version
- [x] `git diff --cached -- main/CMakeLists.txt` shows all source files restored
- [x] `git diff --cached -- main/tollgate_main.c` shows display + relay includes restored
- [x] No `components/esp-miner` in staged diff
- [x] `make test-unit` passes (all 63+ tests)

### Step 4: Commit + Merge
- [x] Create single squash commit on hardening branch
- [x] Fast-forward merge to master
- [x] Push master to origin
- [x] Delete hardening worktree
- [x] Delete `feature/relay-hardening` branch

---

## Expected Final Diff (master → new)

| File | Change |
|------|--------|
| `main/CMakeLists.txt` | **Restored** — add display.c, font.c, local_relay.c, relay_selector.c, sync_manager.c, axs15231b, qrcode, wisp_relay |
| `main/tollgate_main.c` | **Restored** — add display.h, local_relay.h, relay_selector.h, sync_manager.h includes + display calls |
| `main/cvm_server.c` | **Kept master's** — keepalive, timeout, ping/pong, close frame handling |
| `main/display.c` | `escape_wifi_field` made non-static |
| `main/negentropy_adapter.c/h` | **New** — negentropy adapter skeleton |
| `Makefile` | **New** — test-local-relay, test-relay-nip11, test-cvm-roundtrip, test-cross-board targets |
| `AGENTS.md` | **Updated** — display module docs, new test commands |
| `RELAY_HARDENING_PLAN.md` | **New** — this planning doc |
| `RELAY_HARDENING_MERGE.md` | **New** — this merge plan doc |
| `tests/integration/test-cvm-roundtrip.mjs` | **New** — CVM MCP roundtrip test |
| `tests/integration/test-cvm-mcp-relay.mjs` | **Kept** — from master's CVM stability commit |
| `tests/integration/test-cross-board.mjs` | **New** — cross-board payment test |
| `tests/unit/test_display.c` | **New** — 22 unit tests for escape_wifi_field |
| `tests/unit/test_negentropy_adapter.c` | **New** — 13 unit tests for negentropy adapter |
| `tests/unit/Makefile` | **Updated** — new test targets |
| `CHECKLIST-CVM-RELAY.md` | **Deleted** |
| `PLAN-SQUASH-MERGE.md` | **Deleted** |

---

## Commands

```bash
# Step 1: Backups (from main repo)
cd /home/c03rad0r/esp32-tollgate
git tag backup/master-abee221 abee221
git tag backup/hardening-8d58cef 8d58cef
git branch feature/relay-hardening-backup feature/relay-hardening

# Step 2: Soft-reset and compose (in hardening worktree)
cd /home/c03rad0r/esp32-tollgate-hardening
git reset --soft master

# Restore correct versions from last known-good commit
git checkout 81f2dc5 -- main/CMakeLists.txt main/tollgate_main.c

# Delete stale markdowns from index
git rm --cached CHECKLIST-CVM-RELAY.md PLAN-SQUASH-MERGE.md 2>/dev/null || true

# Verify and commit
git diff --cached --stat
git commit -m "feat: relay hardening — restore build, add tests, negentropy adapter"

# Step 3: Verify
make test-unit

# Step 4: Merge to master
cd /home/c03rad0r/esp32-tollgate
git checkout master
git merge --ff-only feature/relay-hardening
git push origin master

# Cleanup
git worktree remove /home/c03rad0r/esp32-tollgate-hardening
git branch -d feature/relay-hardening
git worktree prune
```
