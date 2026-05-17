# TollGate ESP32 — Progress Checklist

## Phase 0: Bootstrap — COMPLETE
- [x] Create project directory and git repo
- [x] Create .env, .env.example, .gitignore
- [x] Persist PLAN.md and CHECKLIST.md
- [x] Create ESP-IDF project skeleton (CMakeLists, partitions.csv, sdkconfig.defaults)
- [x] Create Makefile with detect/build/flash/test targets
- [x] Run `make detect-all` — identified both boards as ESP32-S3 (16MB flash)
- [x] Fix ESP-IDF v5.4.1 installation (was deeply corrupted, re-cloned)

## Phase 1: Captive Portal + Firewall — COMPLETE (commit `a7d0a67`)
- [x] Implement tollgate_main.c (WiFi AP+STA, event loop)
- [x] Implement config.c/h (SPIFFS JSON config loading)
- [x] Implement dns_server.c/h (DNS hijack/forward per-client)
- [x] Implement captive_portal.c/h (HTTP :80, portal HTML)
- [x] Implement firewall.c/h (NAPT on/off per auth state)
- [x] Set up test infrastructure (Node.js tests, helpers, Playwright)
- [x] Fix WiFi init order bug
- [x] Fix DNS hijack test (nslookup exits 1 for AAAA)
- [x] Fix ping tests (use `-I wlp59s0`)
- [x] Tests 1-14: ALL PASSING

## Phase 2: E-Cash Payments — COMPLETE
- [x] Implement cashu.c/h, session.c/h, tollgate_api.c/h
- [x] Update captive portal HTML with payment form
- [x] Wire into tollgate_main.c
- [x] Per-MAC access tracking, two httpd instances
- [x] Bug fixes: stack overflow, heap allocations, TLS, token decode
- [x] Tests 15-24: ALL PASSING

## Phase 3: On-Device Wallet + Nostr Identity + Wifistr — COMPLETE
- [x] nucula wallet integration (git submodule, C++ bridge, C API)
- [x] Nostr identity derivation (HMAC-SHA512, MAC/SSID/IP)
- [x] Nostr event signing (NIP-01, Schnorr)
- [x] Geohash encoding
- [x] Wifistr service discovery (kind 38787)
- [x] 58 unit tests passing

## Phase 4: ESP32 TollGate Client Detection + Auto-Payment — COMPLETE (commit `78dd599`)
- [x] tollgate_client.c/h — detection, payment, monitoring, state machine
- [x] 30/30 unit tests passing

## Phase 5: Lightning Auto-Payout — COMPLETE (commit `cb4bd7d`)
- [x] lnurl_pay.c/h, lightning_payout.c/h, nucula_wallet_melt()
- [x] 18 unit tests passing

## Phase 6: Bytes-Based Billing — COMPLETE (commit `edd125d`)
- [x] Dual-metric session support (milliseconds + bytes)

## Phase 7: MCP Handler + NIP-04 + CVM Server — COMPLETE (commit `fdf662f`)
- [x] mcp_handler.c/h (4 tools, 25 unit tests)
- [x] nip04.c/h (AES-256-CBC + ECDH, 15 unit tests)
- [x] cvm_server.c/h (Nostr DM listener)

## Bug Fixes — COMPLETE (commit `3342c8e`)
- [x] reset_auth, /usage, metric default, sys_evt stack overflow fixes

## Playwright Interop Tests — COMPLETE (commit `4fb44e7`)
- [x] 18/18 tests passing (11 ESP32 + 7 ESP32↔OpenWRT interop)

## Per-Client NAT Filtering — COMPLETE (commit `0c2c67b`)
- [x] Create `main/lwip_tollgate_hooks.h` — LWIP_HOOK_IP4_CANFORWARD definition
- [x] Update `CMakeLists.txt` — inject hook header into lwIP compilation
- [x] Add `tollgate_ip4_canforward_filter()` to `firewall.c` — filter by source IP, network byte order
- [x] NAT always ON, per-client filter in lwIP forwarding path
- [x] Remove `update_nat()`, `firewall_enable_nat()`, `firewall_disable_nat()`
- [x] Subnet-aware: only filter AP subnet packets, allow internet responses
- [x] Fix byte order bug: firewall stores IPs in network byte order
- [x] Reduce API server stack 32KB→16KB (fixes ESP_ERR_HTTPD_TASK)
- [x] E2E verified: block→pay→allow→revoke→block on live hardware

## Spent-Secret Cleanup — COMPLETE (commit `0c2c67b`)
- [x] Remove `s_spent_secrets[]`, `session_is_secret_spent()` from session.c
- [x] Remove `spent_secrets`/`spent_secret_count` from `session_t`
- [x] Remove spent-secret params from `session_create()`/`session_create_bytes()`
- [x] Remove local spent-secret check in `tollgate_api.c`
- [x] Update `tests/unit/test_session.c`
- [x] 186 unit tests passing

---

## TODO — Remaining

### Test Reorganization
- [ ] Fix hardcoded IP fallbacks: `192.168.4.1` → `10.192.45.1` in test files
- [ ] Create `tests/integration/` and `tests/e2e/` directories
- [ ] Move `api.mjs`, `network.mjs`, `phase2.mjs`, `smoke.mjs` → `tests/integration/`
- [ ] Move `captive-portal.spec.mjs`, `interop-happy-path.spec.mjs` → `tests/e2e/`
- [ ] Move `playwright.config.mjs` → `tests/e2e/`

### New Integration Tests
- [ ] Write `tests/integration/test-reset-auth.mjs` — reset → verify blocked → pay → verify allowed → reset → verify blocked
- [ ] Write `tests/integration/test-session-expiry.mjs` — pay → wait 65s → verify blocked (slow test)
- [ ] Write `tests/integration/test-dns-firewall.mjs` — DNS hijack before auth, forward after auth, per-client NAT filter

### Makefile & Package Updates
- [ ] Add `test-unit`, `test-integration`, `test-e2e`, `test-all`, `test-session-expiry` targets
- [ ] Update `package.json` scripts for new paths
- [ ] Update existing targets to new paths

### Playwright Video Recording Fix
- [ ] Per-test context isolation in playwright.config.mjs
- [ ] Verify `.webm` files generated in `tests/e2e/test-results/`

### AGENTS.md Update
- [ ] Update firewall description: "per-client NAT filter via LWIP_HOOK_IP4_CANFORWARD"
- [ ] Update session.c description: remove "spent-secret tracking"

### OpenWRT Interop
- [ ] SSH to `root@10.47.41.1`, verify `tollgate-wrt` still running
- [ ] Test `curl http://10.47.41.1:2121/` — kind=10021 response
- [ ] Investigate `nofee.testnut.cashu.space` API compatibility
- [ ] Document findings

### Board B — Flash + Cross-Board Test
- [x] Generate nsec for Board B: `9af47906b45aca5e238390f3d03c8274e154198e81aa2095065627d1e61ca968`
- [x] Derived identity: SSID `TollGate-b96d80`, AP IP `10.185.47.1`, AP MAC `fe:08:f7:b9:6d:80`
- [ ] Create Board B config.json with new nsec
- [ ] Flash Board B at `/dev/ttyACM1`
- [ ] Verify Board B boots with different SSID/IP
- [ ] Cross-board payment test: Board B pays Board A (Scenario 5)

---

## Reminders
- **Commit + push every time a test passes that previously didn't pass**
- Board A: `/dev/ttyACM0`, SSID `TollGate-C0E9CA`, AP IP `10.192.45.1`
- Board B: `/dev/ttyACM1`, SSID `TollGate-b96d80`, AP IP `10.185.47.1`, nsec `9af47906...`
- OpenWRT Router: SSH `root@10.47.41.1`, port 2121
- `source ~/esp/esp-idf/export.sh` before `idf.py`
- Latest commit: `0c2c67b`
- 186 unit tests + 18 Playwright tests — all passing
- sudo password: `c03rad0r123`
- Token generation: `cashu -h https://testnut.cashu.space send --legacy 21`
- See `AGENTS.md` for full testing rules
