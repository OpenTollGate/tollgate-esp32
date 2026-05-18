# Squash + Merge Plan: feature/cvm-relay-stability → master

## Goal
Squash the 4 commits on `feature/cvm-relay-stability` into a single commit and
fast-forward merge into master. All tasks are complete and tested (17/17
integration tests, 61/61 unit tests, 120s stable relay connection on Board B).

## Checklist
- [x] Write this plan to markdown
- [x] Rebase `feature/cvm-relay-stability` onto master
- [x] Squash 4 commits into 1
- [x] Fast-forward merge into master
- [x] Run `make test-unit` — 61/61 pass
- [x] Push master to remote
- [x] Remove worktree `/home/c03rad0r/esp32-tollgate-cvm-relay`
- [x] Delete local branch `feature/cvm-relay-stability`
- [x] Delete stale backup branches (`backup/master-2cd372c`, `backup/pre-squash-cvm-20260519-010933`, `feature/local-relay-backup`)

## Squash commit message
```
feat: CVM relay stability fix + MCP relay integration tests

Relay disconnect fix:
- TLS read timeout reduced from 15s to 1s (short poll loop)
- Ping timer fires every 30s independently of read activity
- Consecutive timeout counter (65s) detects real disconnects
- Handle relay close frames (opcode 0x08) explicitly
- Result: 120s+ stable connection (previously ~37s disconnect cycle)

MCP relay integration tests (17/17 pass via `make test-cvm-mcp`):
- MCP initialize roundtrip via relay.primal.net
- get_sessions returns session array (0 active)
- get_usage returns metric/price/step fields
- Non-owner auth rejection (board silently drops)
- Owner control request passes after rejection test

Build fixes:
- Remove display/font/axs15231b deps (from display branch, not in this tree)
- Add esp_timer to CMakeLists REQUIRES

Host unit tests: 61/61 pass
```

## Branch commits being squashed
1. `81885d2` fix: non-blocking WS reads + decoupled ping timer for relay stability
2. `61fe3ac` fix: remove display deps, add esp_timer to CMakeLists for clean build
3. `9d701c6` test: MCP relay integration tests — get_sessions, get_usage, non-owner auth
4. `6c1ccf1` docs: update checklist — all tasks complete
