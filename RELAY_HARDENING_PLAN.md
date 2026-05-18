# Relay Hardening + Remaining Work — Implementation Plan

## Overview

Post-merge cleanup and remaining work on the `feature/relay-hardening` branch. Covers CVM+relay integration tests, test infrastructure polish, documentation updates, display testing, cross-board payment, OpenWRT interop, and NIP-77 negentropy adapter.

**Branch:** `feature/relay-hardening` (from `master` at `81f2dc5`)
**Worktree:** `/home/c03rad0r/esp32-tollgate-hardening`
**Main repo:** `/home/c03rad0r/esp32-tollgate`

---

## Checklist

### Phase 1: CVM + Relay Integration (Group A)

- [ ] Write `tests/integration/test-cvm-relay.mjs` — CVM tool call via local relay (ws://BOARD_IP:4869)
  - Connect WS to local relay
  - Subscribe to kind 25910 for board's npub
  - Verify CEP-6 announcements (kind 11316/11317/10002) stored in local relay
  - Publish kind 25910 MCP request via local relay
  - Verify response received via local relay
- [ ] Add `test-cvm-relay` make target to main `Makefile` (with board lock)
- [ ] Add `test-cvm-relay` make target to `physical-router-test-automation/esp32/Makefile` (with board lock)
- [ ] Add passthrough target to top-level `physical-router-test-automation/Makefile`
- [ ] End-to-end MCP tools/call roundtrip via kind 25910 on public relay
  - Extend existing `cvm-test-tool` or write new test
  - Publish kind 25910 to public relay, verify response on public relay
  - Test at least `get_config` and `get_balance`
- [ ] Add `test-cvm-e2e` make target to main `Makefile` (with board lock)
- [ ] Verify board npub on contextvm.org/servers (manual check, document result)
- [ ] Add `test-cvm-e2e` make target to `physical-router-test-automation/esp32/Makefile`
- [ ] Flash firmware with relay to Board B, lock board, run all CVM+relay tests

### Phase 2: Test Infrastructure Cleanup (Group B)

- [x] IP fallbacks already correct (all tests use `process.env.TOLLGATE_IP || '10.192.45.1'`, no `192.168.4.1`)
- [x] Test directories already at correct paths (`tests/integration/`, `tests/e2e/`)
- [x] Integration tests already in `tests/integration/` (api, network, phase2, smoke, test-cvm, test-reset-auth, test-session-expiry, test-dns-firewall, test-local-relay, test-relay-nip11)
- [x] E2E tests already in `tests/e2e/` (captive-portal.spec, interop-happy-path.spec)
- [ ] Add `test-local-relay` and `test-relay-nip11` targets to main `Makefile` (with board lock)
- [ ] Add `smoke` make target alias to `physical-router-test-automation/esp32/Makefile` for relay firmware
- [ ] Per-test context isolation in `tests/e2e/playwright.config.mjs`
- [ ] Verify Playwright `.webm` video recording in `tests/e2e/test-results/`
- [ ] Run full integration test suite on Board B to verify nothing regressed

### Phase 3: Documentation (Group C)

- [ ] Update AGENTS.md: firewall description → "per-client NAT filter via LWIP_HOOK_IP4_CANFORWARD"
- [ ] Update AGENTS.md: session.c description → remove "spent-secret tracking"
- [ ] Update AGENTS.md: add display module docs (display.c/h, font.c/h, QR cycling, states)
- [ ] Update AGENTS.md: add relay hardening make targets to test instructions
- [ ] Verify CVM announcements on relay.primal.net (Board B with internet)
  - Kind 11316 server announcement
  - Kind 11317 tools list
  - Kind 10002 relay list
- [ ] Update CHECKLIST.md: mark verified items, add relay-hardening items

### Phase 4: Display Testing on Board C (Group D.1)

- [ ] Add unit test for `escape_wifi_field()` in `tests/unit/test_display.c`
  - Test: no special chars → no escaping
  - Test: semicolons, colons, backslashes, commas, quotes → backslash-escaped
  - Test: multiple special chars in one string
  - Test: empty string
- [ ] Add unit test for QR matrix generation in `tests/unit/test_display.c`
  - Test: valid QR matrix for various string lengths
  - Test: WIFI: URI format correctness
- [ ] Update `tests/unit/Makefile` with `test_display` target
- [ ] Lock Board C (`make lock-c PHASE="display testing"`)
- [ ] Flash firmware to Board C at `/dev/ttyACM3`
- [ ] Verify boot screen shows "TollGate starting..."
- [ ] Verify QR code renders on display
- [ ] Verify Wi-Fi QR is scannable by Android/iOS camera
- [ ] Verify portal URL QR is scannable and loads captive portal
- [ ] Verify QR cycling (Wi-Fi ↔ Portal URL every 5s)
- [ ] Unlock Board C

### Phase 5: Board B Config + Cross-Board Payment (Group D.2)

- [ ] Create Board B `config.json` with unique nsec (`9af47906b45aca5e238390f3d03c8274e154198e81aa2095065627d1e61ca968`)
  - Derived identity: SSID `TollGate-b96d80`, AP IP `10.185.47.1`, AP MAC `fe:08:f7:b9:6d:80`
- [ ] Lock Board B (`make lock-b PHASE="Board B config + cross-board test"`)
- [ ] Flash Board B with new config
- [ ] Verify Board B boots with different SSID/IP from Board A
- [ ] Connect laptop to Board B, verify captive portal works
- [ ] Cross-board payment test: Board B pays Board A (Scenario 5)
  - Board B as STA connects to Board A's AP
  - Board B auto-detects Board A as upstream TollGate (kind 10021)
  - Board B wallet creates token, POSTs to Board A
  - Verify Board B gets internet through Board A
- [ ] Write integration test `tests/integration/test-cross-board.mjs`
- [ ] Add `test-cross-board` make target to main `Makefile` and physical-router-test-automation
- [ ] Unlock Board B

### Phase 6: OpenWRT Interop (Group D.3)

- [ ] SSH to `root@10.47.41.1`, verify `tollgate-wrt` still running
- [ ] Test `curl http://10.47.41.1:2121/` — verify kind=10021 response
- [ ] Investigate `nofee.testnut.cashu.space` API compatibility
- [ ] Document findings in CHECKLIST.md or OpenWRT interop notes

### Phase 7: NIP-77 Negentropy Adapter (Group D.4)

- [ ] Write `main/negentropy_storage.c/h` — adapter from wisp storage to negentropy API
  - `negentropy_storage_init()` — wrap storage_engine event iterator
  - `negentropy_get_items()` — iterate stored events, return (timestamp, event_id) pairs
  - `negentropy_insert_items()` — insert reconciled events from remote
- [ ] Modify `sync_manager.c` — add negentropy sync path alongside REQ-diff
  - Detect NIP-77 support from relay_selector (NIP-77 flag)
  - If primary supports NIP-77: use NEG_OPEN/NEG_MSG instead of REQ-diff
  - If primary doesn't support NIP-77: fall back to REQ-diff
- [ ] Add `tests/unit/test_negentropy_storage.c` — unit test with mock ID sets
- [ ] Update `tests/unit/Makefile` with `test_negentropy_storage` target
- [ ] Flash to Board B, verify sync with NIP-77 capable relay (orangesync)

---

## Hardware Access Rules

**ALWAYS acquire board lock before any hardware access:**
```bash
# In physical-router-test-automation/esp32/
make lock-b PHASE="relay integration testing"
make lock-c PHASE="display testing"

# Or via main repo
make lock-b PHASE="cross-board payment test"
```

**Make targets that touch hardware MUST use board locks:**
- All `flash-*` targets
- All `test-*` targets that run against live boards
- All `monitor-*` targets

**Make targets that DON'T need locks:**
- `test-unit` (host-only, no hardware)
- `build` (compile only)
- `cvm-pubkey` (read-only query)

**Always release lock when done:**
```bash
make unlock-b
make unlock-c
```

## Board Reference

| Board | Port | Factory MAC | SSID | AP IP | nsec | Use |
|-------|------|-------------|------|-------|------|-----|
| A | `/dev/ttyACM0` | `94:a9:90:2e:37:7c` | `TollGate-B96D80` | `10.185.47.1` | `9af47906...` | Primary test (WiFi broken) |
| B | `/dev/ttyACM1` | `fc:01:2c:c5:50:50` | `TollGate-C0E9CA` | `10.192.45.1` | default | Relay + CVM testing |
| C | `/dev/ttyACM3` | `20:6e:f1:98:d7:08` | TBD | TBD | TBD | Display testing |

## Test Execution Order

1. **No hardware needed:** `make test-unit` — verify all unit tests pass
2. **Board B (lock required):** `make lock-b` → flash → CVM+relay tests → cross-board → `make unlock-b`
3. **Board C (lock required):** `make lock-c` → flash → display tests → `make unlock-c`
4. **No hardware:** documentation updates, AGENTS.md, CHECKLIST.md
5. **OpenWRT (separate):** SSH checks, no board lock needed

## Commit Strategy

- Commit + push after each phase completes
- Commit + push every time a test passes that previously didn't pass
- Squash into single commit before merging back to master
