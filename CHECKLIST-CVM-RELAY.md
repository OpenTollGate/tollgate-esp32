# CVM Relay Stability Checklist

## Pre-flight
- [x] Create worktree `/home/c03rad0r/esp32-tollgate-cvm-relay`
- [x] Create branch `feature/cvm-relay-stability` from master
- [x] Document plan in PLAN-CVM-RELAY.md

## Task 3: Fix Relay Disconnect
- [x] Modify `cvm_relay_task()` inner loop: 1s TLS read timeout
- [x] Decouple ping timer from read success
- [x] Add consecutive-timeout counter for real disconnect detection
- [x] Handle relay close frames (opcode 0x08)
- [x] `make test-unit` passes (61/61)
- [x] Build firmware
- [x] Lock Board B: `make lock-b PHASE="cvm-relay-stability"`
- [x] Verify Board B port: `esptool.py --port /dev/ttyACM1 chip_id`
- [x] Flash Board B via `make flash-b`
- [x] Monitor serial: confirm WS connected, no disconnect in 120s
- [ ] Unlock Board B (deferred — may need more testing)

## Task 1: Test get_sessions & get_usage via Relay
- [x] Write `tests/integration/test-cvm-mcp-relay.mjs`
- [x] Test: `get_sessions` returns JSON array via relay (0 active sessions)
- [x] Test: `get_usage` returns metric/price/step fields via relay
- [x] Both tests PASS on Board B (via `make test-cvm-mcp`)

## Task 2: Test Non-Owner Auth Rejection via Relay
- [x] Test: non-owner request gets no response (12s wait)
- [x] Test: owner control request succeeds after non-owner test
- [x] Both tests PASS on Board B (via `make test-cvm-mcp`)

## Final
- [x] `make test-unit` — 61 tests pass
- [x] Commit: 3 commits on feature/cvm-relay-stability
- [ ] Push to remote (nostr git relay down — will retry later)
