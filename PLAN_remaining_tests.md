# PLAN_remaining_tests.md — Remaining Integration Tests

## Overview

All high-priority wallet work is complete (receive, send, swap, persistence, burst test). This plan covers the remaining integration test validation and fixes.

## Phase A: Fix Stale Mint URLs — TODO

Three test files still reference `testnut.cashu.space` (dead mint):

- [ ] `tests/integration/test-session-expiry.mjs` (lines 26-27)
- [ ] `tests/integration/test-reset-auth.mjs` (lines 27-28)
- [ ] `tests/integration/test-dns-firewall.mjs` (lines 26-27)

Replace with `testnut-nutshell.mints.orangesync.tech`.

## Phase B: Single-Board Integration Tests — TODO

Board A at `10.185.47.1`, port `/dev/ttyACM0`.

- [ ] `test-reset-auth.mjs` — payment + reset + re-auth flow
- [ ] `test-session-expiry.mjs` — session time-based expiry
- [ ] `test-dns-firewall.mjs` — DNS hijack + firewall per-client
- [ ] `test-local-relay.mjs` — WS pub/sub on port 4869
- [ ] `test-relay-nip11.mjs` — NIP-11 info document
- [ ] `test-market.mjs` — GET /market endpoint

## Phase C: Cross-Board Test — TODO

- [ ] Write Board C SPIFFS config (nsec `71bf3f4d...`)
- [ ] Verify Board C API responds
- [ ] Run `test-cross-board.mjs` against Board C

Board C: `10.74.63.1`, SSID `TollGate-4A2510`, port `/dev/ttyACM2`.

## Phase D: CVM Round-Trip Fix — TODO

9/11 tests pass. MCP kind 25910 requests go unanswered.

- [ ] Check serial logs for `cvm_relay` task creation failure
- [ ] Add error logging to `cvm_server_start` for `xTaskCreate` failure
- [ ] If task creation fails: try `xTaskCreatePinnedToCore(..., 1)` or reduce stack to 12KB
- [ ] Re-run `test-cvm-roundtrip.mjs`

**Root cause hypothesis:** `cvm_relay_task` (16KB stack) fails to create due to internal RAM fragmentation, same as old TLS worker.

## Phase E: Two-Board Price Discovery — DEFERRED

Requires both boards on same network. Complex network setup. Defer until user requests.

## Completed This Session

- [x] Wallet receive via health task queue (5/5 burst)
- [x] Wallet send (valid cashuA tokens)
- [x] Wallet swap (proof consolidation)
- [x] NVS persistence across reboots
- [x] Fix 4 API integration tests (19/19)
- [x] Fix E2E mint URL pattern (14/14 Playwright)
- [x] Flash Board C
- [x] CVM round-trip (9/11, 2 MCP response failures)
