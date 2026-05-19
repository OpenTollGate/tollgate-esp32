# Multi-Mint Support — Rebase, Backup, Squash & Merge Plan

## Goal
Rebase `feature/multi-mint-support` onto `master`, create a backup branch, squash all 20 commits into one clean commit, then merge to master.

## Current State
- **Branch**: `feature/multi-mint-support` in worktree `/home/c03rad0r/esp32-tollgate-multi-mint`
- **Commits on branch**: 20 (since `master` at `77031f0`)
- **Remote**: `origin` → Nostr relay `relay.ngit.dev` (currently down)
- **Worktree**: shared repo — other sessions use other worktrees on different branches

## Procedure

### Phase 1: Pre-flight
1. Verify working tree is clean (no uncommitted changes)
2. Verify build passes
3. Verify unit tests pass (75/75)

### Phase 2: Backup
4. Create backup branch `backup/multi-mint-support-pre-rebase` at current HEAD
5. Create backup branch `backup/multi-mint-support-pre-squash` (same point, used after rebase)

### Phase 3: Rebase
6. `git rebase master` — rebase all 20 commits onto master
7. Resolve any conflicts
8. Verify build + tests still pass after rebase

### Phase 4: Post-rebase Backup
9. Create `backup/multi-mint-support-rebased` at the rebased HEAD
10. This preserves every individual commit even after squashing

### Phase 5: Squash
11. `git reset --soft master` — soft reset to master, keeping all changes staged
12. `git commit -m "feat: multi-mint Cashu wallet with health tracking, WPA auto-detect, CVM"` — single clean commit
13. Verify build + tests pass after squash

### Phase 6: Merge
14. Merge to master (fast-forward since squashed branch sits on top)
15. Verify master builds and tests pass

## Checklist

### Pre-flight
- [ ] Working tree clean
- [ ] Build passes (`idf.py build`)
- [ ] Unit tests pass (`make test-unit`)

### Backup
- [ ] `backup/multi-mint-support-pre-rebase` created at current HEAD (`3aa372c`)

### Rebase
- [ ] `git rebase master` completed
- [ ] Conflicts resolved (if any)
- [ ] Build passes after rebase
- [ ] Unit tests pass after rebase

### Post-rebase Backup
- [ ] `backup/multi-mint-support-rebased` created at rebased HEAD

### Squash
- [ ] `git reset --soft master` done
- [ ] Single commit created with clean message
- [ ] Build passes after squash
- [ ] Unit tests pass after squash

### Merge
- [ ] Merged to master (fast-forward)
- [ ] Master builds and tests pass
- [ ] Worktree updated

## Remaining Work After Merge
1. **Push to Nostr relay** — blocked until `relay.ngit.dev` recovers
2. **NVS keyset storage** — `ESP_ERR_NVS_NOT_ENOUGH_SPACE` errors; factory partition at `0x10000` limits NVS to 24KB. Options:
   - Store keysets in SPIFFS instead of NVS
   - Compress keyset data
   - Only cache active keysets
3. **Board A crash** — hardware-specific (~50s uptime), not software. Possible causes:
   - Bad power supply on QinHeng UART adapter (serial `5A84017819`)
   - Failing flash chip on that ESP32-S3 board
   - Swap physical boards between UART adapters to isolate
4. **Integration test WiFi stability** — `test-multi-mint-*` targets fail on early steps because WiFi disconnects during 30s probe wait. Fix: `_connect-b-if-needed` should run before each curl call
5. **Display AXS15231B `ESP_ERR_NO_MEM`** — SPI flush fails every ~1s (307KB PSRAM framebuffer). The `display_enabled` config field allows disabling, but proper fix needs:
   - Reduce framebuffer (partial refresh instead of full-screen)
   - Or use SPI DMA with larger chunk sizes
6. **Health probe recovery threshold** — 3 consecutive successes × 300s interval = 15min before a mint is marked reachable. Consider reducing `MINT_HEALTH_RECOVERY_THRESHOLD` to 1 for initial probes
7. **Makefile WPA auto-detect** — `detect-wpa-security` + `generate-spiffs` + `flash-spiffs-{a,b,c}` targets added to `physical-router-test-automation/esp32/Makefile`. Needs separate commit/merge there

## Backup Branch Names
| Branch | Purpose | Created At |
|--------|---------|------------|
| `backup/multi-mint-support-pre-rebase` | Full history before rebase | Before `git rebase master` |
| `backup/multi-mint-support-rebased` | All 20 commits after rebase | After `git rebase master` |
