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

## Phase 2: E-Cash Payments — IN PROGRESS (commit `3f46bb8` + uncommitted fixes)
### Code Written
- [x] Implement cashu.c/h (Cashu token parse, base64url, checkstate, mint validation)
- [x] Implement session.c/h (time-based allotment, expiry, secret tracking, MAC tracking)
- [x] Implement tollgate_api.c/h (:2121 server, GET/POST /, /usage, /whoami)
- [x] Update captive portal HTML with payment form (Cashu token textarea + "Pay & Connect")
- [x] Wire into tollgate_main.c (session_init, api_start, session_tick loop)
- [x] Per-MAC access tracking: `firewall_get_mac_for_ip()` using `esp_wifi_ap_get_sta_list_with_ip()` + ARP fallback
- [x] Two httpd instances: port 80 (captive portal) and port 2121 (TollGate API)

### Bug Fixes
- [x] Stack overflow: httpd stack_size increased to 32768 (TLS+mbedTLS needs ~20KB)
- [x] Heap allocations: cashu_token_t, cashu_proof_state_t, json_buf, post_body all heap-allocated
- [x] TLS to mint: `esp_crt_bundle_attach` + `esp-tls` in CMakeLists.txt REQUIRES
- [x] HTTP client: `open/write/fetch_headers/read` pattern (not `perform`)
- [x] Token decode: dynamic `json_buf` sizing `malloc((b64_len * 3) / 4 + 4)`, strip trailing `\n`/`\r`
- [x] POST body recv: loop `httpd_req_recv` until all `content_len` bytes read
- [x] `secret_count` bug: capped at `MIN(proof_count, 5)` before `session_create`
- [x] `config.c` default mint URL fixed to `testnut.cashu.space`
- [x] Makefile: nutshell wallet targets (wallet-setup, wallet-info, mint-token, send-token)
- [x] `tests/phase2.mjs`: `/whoami` test checks `includes('mac=')`

### Infrastructure
- [x] Upstream gateway on enx00e04c633a90 (192.168.2.0/24, metric 101, default route)
- [x] OpenWRT TollGate on enx00e04c683d2d (10.47.41.0/24, metric 20100, never-default)
- [x] WiFi wlp59s0 free for ESP32 TollGate connection
- [x] NetworkManager profile "TollGate-ESP32" created (manual 192.168.4.2/24, autoconnect=no)
- [x] Mint URL verified: `testnut.cashu.space` works; `nofee.testnut.cashu.space` and `nofees.testnut.cashu.space` both broken

### Tests Passing
- [x] Test 15: Advertisement valid (kind=10021 with price_per_step) — PASSING
- [x] Test 16: Valid payment (POST :2121/ with valid Cashu token → kind=1022 session) — PASSING
- [x] Test 17: Usage tracking after payment (GET :2121/usage → active usage) — PASSING
- [x] Test 18: Internet after payment (ping through TollGate works) — PASSING
- [x] Test 19: Invalid token rejected (POST garbage → 400, kind=21023) — PASSING
- [x] Test 20: Spent token rejected (reuse token → kind=21023) — PASSING
- [x] Test 21: Wrong mint rejected (POST token from wrong mint → kind=21023) — PASSING
- [x] Test 22: Session expiry (wait for allotment → internet blocked) — PASSING
- [x] Test 23: Session renewal (second payment → allotment extended) — PASSING
- [x] Test: /whoami returns ip=X.X.X.X mac=XX:XX:XX:XX:XX:XX — PASSING
- [x] Test: Portal has payment form (Cashu token input + Pay button) — PASSING

### Tests Not Yet Run (need Playwright)
- [ ] Test 24: Portal payment form visible in browser (Playwright)
- [ ] Test 25: Two clients pay independently
- [ ] Test 26: Client isolation (only payer gets internet)
- [ ] Test 27: Full e2e: portal → pay → browse

### Captive Portal Detection Fix
- [x] Added DoT reject server on port 853 (TCP RST forces DNS fallback to port 53)
- [x] DNS hijack now returns NXDOMAIN for ALL non-A query types (prevents DNS leaks)
- [x] Shorter TTL on hijack responses (10s) for faster detection
- [x] Explicit 302 redirect handlers for all captive detection URIs (/generate_204, /hotspot-detect.html, etc.)
- [x] HTTP request logging for captive detection endpoints
- [x] DNS query logging for unauthenticated clients
- [ ] **Needs verification with actual GrapheneOS phone**

## Phase 3: nucula Wallet + ESP32-to-ESP32 Payments — NOT STARTED
- [ ] Extract nucula wallet into components/cashu_wallet/
- [ ] Replace simple melt with Wallet::receive()
- [ ] Implement payout.c/h (background melt-to-LN)
- [ ] Implement upstream_client.c/h (reseller mode)
- [ ] ESP32-to-ESP32 payments (ESP32 generates/proves tokens to pay another ESP32 TollGate)
- [ ] Tests 28-38

## Phase 4: ESP32-to-OpenWRT TollGate Interop — NOT STARTED
- [ ] ESP32 pays OpenWRT TollGate using Cashu tokens
- [ ] Interoperability testing with existing OpenWRT TollGate on enx00e04c683d2d
