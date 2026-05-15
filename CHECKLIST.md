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

## Phase 2: E-Cash Payments — IN PROGRESS
### Code Written (commit `1263d86`)
- [x] Implement cashu.c/h (Cashu token parse, base64url, checkstate, mint validation)
- [x] Implement session.c/h (time-based allotment, expiry, secret tracking)
- [x] Implement tollgate_api.c/h (:2121 server, GET/POST /, /usage, /whoami)
- [x] Update captive portal HTML with payment form
- [x] Wire into tollgate_main.c (session_init, api_start, session_tick loop)

### Bug Fixes (commit `aed51d8`)
- [x] Stack overflow: httpd stack_size increased to 16384 in tollgate_api.c
- [x] Heap allocations: b64, json_buf, post_body, resp_buf moved to heap in cashu.c
- [x] .env: MINT_URL updated to testnut.cashu.space
- [x] Makefile: replaced Go-based tokens target with nutshell wallet targets

### Infrastructure (ready now)
- [x] Upstream gateway on enx00e04c633a90 (192.168.2.0/24, metric 101, default route)
- [x] OpenWRT TollGate on enx00e04c683d2d (10.47.41.0/24, metric 20100, never-default)
- [x] WiFi wlp59s0 free for ESP32 TollGate connection
- [x] NetworkManager profile "TollGate-ESP32" created (manual 192.168.4.2/24, autoconnect=no)

### Tests Passing
- [x] Test 15: Advertisement valid (kind=10021 with price_per_step) — PASSING

### Tests Blocked (need hardware flash + test)
- [ ] Test 16: Valid payment (POST :2121/ with valid Cashu token → kind=1022 session)
- [ ] Test 17: Usage tracking after payment (GET :2121/usage → active usage)
- [ ] Test 18: Internet after payment (ping through TollGate works)
- [ ] Test 19: Invalid token rejected (POST garbage → 400, kind=21023)
- [ ] Test 20: Spent token rejected (reuse token → 402, kind=21023)
- [ ] Test 21: Wrong mint rejected (POST token from wrong mint → 402)
- [ ] Test 22: Session expiry (wait for allotment → internet blocked)
- [ ] Test 23: Session renewal (second payment → allotment extended)
- [ ] Test 24: Portal payment form visible in browser
- [ ] Test 25: Two clients pay independently
- [ ] Test 26: Client isolation (only payer gets internet)
- [ ] Test 27: Full e2e: portal → pay → browse

### Next Steps (TDD cycle)
1. Flash firmware to ESP32 board A (`make flash-a`)
2. Connect WiFi to TollGate AP: `nmcli con up TollGate-ESP32`
3. Run Phase 2 discovery test: `TOLLGATE_IP=192.168.4.1 node tests/phase2.mjs`
4. If Test 15 still passes, proceed to Test 19 (invalid token — no mint needed)
5. Mint a test token: `make mint-token AMOUNT=21`
6. Run full Phase 2 with token: `TEST_TOKEN=$(cashu --env-mint testnut.cashu.space send --legacy 21) TOLLGATE_IP=192.168.4.1 node tests/phase2.mjs`
7. Fix any failures, commit + push when tests pass

## Phase 3: nucula Wallet + Reseller — NOT STARTED
- [ ] Extract nucula wallet into components/cashu_wallet/
- [ ] Replace simple melt with Wallet::receive()
- [ ] Implement payout.c/h (background melt-to-LN)
- [ ] Implement upstream_client.c/h (reseller mode)
- [ ] Tests 28-38
