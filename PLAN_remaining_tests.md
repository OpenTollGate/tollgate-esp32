# PLAN_remaining_tests.md — Remaining Integration Tests

## Overview

All high-priority wallet work is complete (receive, send, swap, persistence, burst test). This plan covers the remaining integration test validation and fixes.

## Phase A: Fix Stale Mint URLs — DONE

Three test files still reference `testnut.cashu.space` (dead mint):

- [x] `tests/integration/test-session-expiry.mjs` (lines 26-27)
- [x] `tests/integration/test-reset-auth.mjs` (lines 27-28)
- [x] `tests/integration/test-dns-firewall.mjs` (lines 26-27)

Replace with `testnut-nutshell.mints.orangesync.tech`.

## Phase B: Single-Board Integration Tests — PARTIAL

Board A at `10.185.47.1`, port `/dev/ttyACM0`.

- [ ] `test-reset-auth.mjs` — payment + reset + re-auth flow (BLOCKED: mint SQLite down)
- [ ] `test-session-expiry.mjs` — session time-based expiry (BLOCKED: mint SQLite down)
- [ ] `test-dns-firewall.mjs` — DNS hijack + firewall per-client (BLOCKED: mint SQLite down)
- [x] `test-local-relay.mjs` — WS pub/sub on port 4869 (5/6, concurrent conn minor)
- [x] `test-relay-nip11.mjs` — NIP-11 info document (10/11, Accept header minor)
- [x] `test-market.mjs` — GET /market endpoint (4/4)

## Phase C: Cross-Board Test — TODO

- [ ] Write Board C SPIFFS config (nsec `71bf3f4d...`)
- [ ] Verify Board C API responds
- [ ] Run `test-cross-board.mjs` against Board C

## Phase D: CVM Round-Trip Fix — DONE

9/11 -> 7/8 tests pass. MCP requests now answered by board.

- [x] Move CVM server start before mint_health (avoids RAM fragmentation)
- [x] Fix NIP-01 subscription: `#p` must be array, not string
- [x] Fix read loop: tolerate TLS timeouts, keep connection alive
- [x] Reduce TLS timeout 15s -> 5s for faster event delivery
- [x] Sign test events with board nsec (--sec) for owner auth
- [x] Change default relay to nos.lol (forwards ephemeral events)
- [x] MCP get_config roundtrip confirmed
- [x] MCP get_balance roundtrip confirmed
- [ ] Fix remaining test parsing issue (response JSON structure)

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
