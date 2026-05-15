# TollGate ESP32 — Progress Checklist

## Phase 0: Bootstrap
- [x] Create project directory and git repo
- [x] Create .env, .env.example, .gitignore
- [x] Persist PLAN.md and CHECKLIST.md
- [x] Create ESP-IDF project skeleton (CMakeLists, partitions.csv, sdkconfig.defaults)
- [x] Create Makefile with detect/build/flash/test targets
- [x] Run `make detect-all` — identified both boards as ESP32-S3 (16MB flash)
- [x] Fix ESP-IDF v5.4.1 installation (was deeply corrupted, re-cloned)

## Phase 1: Captive Portal + Firewall — COMPLETE
- [x] Implement tollgate_main.c (WiFi AP+STA, event loop)
- [x] Implement config.c/h (SPIFFS JSON config loading)
- [x] Implement dns_server.c/h (DNS hijack/forward per-client)
- [x] Implement captive_portal.c/h (HTTP :80, portal HTML)
- [x] Implement firewall.c/h (NAPT on/off per auth state)
- [x] Set up test infrastructure (Node.js tests, helpers, Playwright)
- [x] Fix WiFi init order bug (netif before esp_wifi_init, set_mode before set_config)
- [x] Fix DNS hijack test (nslookup exits 1 for AAAA, catch stderr)
- [x] Fix ping tests (use `-I wlp59s0` to force through TollGate AP)
- [x] Test 1: Boot and AP appears
- [x] Test 2: DHCP lease
- [x] Test 3: Captive portal serves HTML
- [x] Test 4: Captive detection URIs work (8 URIs)
- [x] Test 5: DNS hijack before auth
- [x] Test 6: No internet before auth
- [x] Test 7: /whoami returns MAC
- [x] Test 8: /usage returns no session
- [x] Test 9: Grant access via API
- [x] Test 10: DNS forward after auth
- [x] Test 11: Internet after auth
- [x] Test 12: HTTP browsing works
- [x] Test 13: Reset auth
- [x] Test 14: Internet blocked after reset
- [x] **All 20 API tests pass, all 6 smoke tests pass**
- [x] Committed: `a7d0a67`

## Phase 2: E-Cash Payments — IN PROGRESS (code written, bugs to fix)
- [x] Implement cashu.c/h (Cashu token parse, base64url, checkstate, mint validation)
- [x] Implement session.c/h (time-based allotment, expiry, secret tracking)
- [x] Implement tollgate_api.c/h (:2121 server, GET/POST /, /usage, /whoami)
- [x] Update captive portal HTML with payment form (token textarea, Pay & Connect button)
- [x] Wire into tollgate_main.c (session_init, api_start, session_tick loop)
- [x] Test 15: Advertisement valid (kind=10021 with price_per_step) — PASSING
- [ ] **BUG FIX: Stack overflow in httpd task** — POST to :2121 crashes (Guru Meditation LoadProhibited). Need to increase httpd stack_size to 16384 and heap-allocate large buffers in cashu.c
- [ ] **BUG FIX: cashu_decode_token has 2048B stack buffer** — move json_buf to heap
- [ ] **BUG FIX: cashu_check_proof_states has 4096B stack buffer** — move resp_buf to heap
- [ ] Test 16: Valid payment (needs valid Cashu token from nutshell)
- [ ] Test 17: Usage tracking after payment
- [ ] Test 18: Internet after payment
- [ ] Test 19: Invalid token rejected — blocked by stack overflow crash
- [ ] Test 20: Spent token rejected
- [ ] Test 21: Wrong mint rejected — blocked by stack overflow crash
- [ ] Test 22: Session expiry
- [ ] Test 23: Session renewal
- [ ] Test 24: Portal payment form — blocked by stack overflow crash
- [ ] Test 25: Two clients pay independently
- [ ] Test 26: Client isolation
- [ ] Test 27: Full e2e browser flow

## Infrastructure Setup — TODO (before next hardware session)
- [ ] Update .env: change mint from nofee.testnut.cashu.space → testnut.cashu.space
- [ ] Update Makefile: add nutshell wallet targets (mint-token, send-token, balance)
- [ ] Create Ansible playbook for full dev environment setup
- [ ] Create NetworkManager profile for TollGate testing (ethernet=upstream, wifi=tollgate only)
- [ ] Verify network routing works (ethernet default route, WiFi 192.168.4.0/24 only)

## Phase 3: nucula Wallet + Reseller — NOT STARTED
- [ ] Extract nucula wallet into components/cashu_wallet/
- [ ] Replace simple melt with Wallet::receive()
- [ ] Implement payout.c/h (background melt-to-LN)
- [ ] Implement upstream_client.c/h (reseller mode)
- [ ] Test 28-38: All Phase 3 tests
