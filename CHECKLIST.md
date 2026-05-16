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
- [x] WiFi wlp59s0 free for ESP32 TollGate connection
- [x] Mint URL verified: `testnut.cashu.space` works (auto-pays invoices)

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

### Captive Portal Detection Fix
- [x] Added DoT reject server on port 853 (TCP RST forces DNS fallback to port 53)
- [x] DNS hijack now returns NXDOMAIN for ALL non-A query types (prevents DNS leaks)
- [x] Shorter TTL on hijack responses (10s) for faster detection
- [x] Explicit 302 redirect handlers for all captive detection URIs (/generate_204, /hotspot-detect.html, etc.)
- [x] HTTP request logging for captive detection endpoints
- [x] DNS query logging for unauthenticated clients
- [x] Verified working with GrapheneOS phone (commit `236b61d`)

## Phase 3: On-Device Wallet + ESP32-to-ESP32 Payments — IN PROGRESS
### Wallet Module (wallet.c/h)
- [x] `hash_to_curve()` — SHA256 try-and-increment with Cashu domain separator
- [x] `point_add()`, `scalar_mul()` — mbedTLS secp256k1 primitives
- [x] `random_scalar()` — ESP32 hardware RNG mod curve order
- [x] Proof storage: `wallet_add_proofs()`, `wallet_remove_proof()`, `wallet_clear()`
- [x] Keyset fetching: `wallet_fetch_keysets()` — GET /v1/keys from mint
- [x] Full swap: `wallet_swap_proofs()` — generates blinded messages, POST /v1/swap, unblinds signatures
- [x] Token creation: `wallet_create_token()` — encode proofs as `cashuA` token
- [x] Wallet API endpoints: `GET /wallet`, `POST /wallet/swap`, `POST /wallet/send`
- [x] Payment flow integration: received proofs added to wallet after session creation
- [x] mbedTLS 3.x compatibility (no direct point field access, no point_negate)
- [x] Unblinding: `C = C_ + (order - r) * G` approach
- [x] Clean build (0 warnings, 0 errors)

### Wallet Persistence (wallet_persist.c/h)
- [ ] Implement `wallet_persist_save()` — serialize wallet to `/spiffs/wallet.json`
- [ ] Implement `wallet_persist_load()` — deserialize wallet from `/spiffs/wallet.json` on boot
- [ ] Add `persist_threshold_sats` to config.json and config struct
- [ ] Threshold logic: only persist when `balance >= persist_threshold_sats`
- [ ] Wire `wallet_persist_save()` into wallet mutations (add_proofs, swap, create_token)
- [ ] Wire `wallet_persist_load()` into `wallet_init()`
- [ ] Build and verify clean compile

### Hardware Testing
- [ ] Flash Board A, verify wallet boot (keyset fetch succeeds)
- [ ] Pay Board A with Cashu token, verify proofs stored (GET /wallet)
- [ ] Test POST /wallet/swap on Board A
- [ ] Test POST /wallet/send on Board A, verify token is valid
- [ ] Verify persistence survives reboot on Board A
- [ ] Flash Board B with TollGate firmware
- [ ] Load Board B with balance (pay it a token)
- [ ] Board B creates send token via POST /wallet/send
- [ ] Cross-board payment: Board B token → Board A (laptop relay)
- [ ] Verify both boards show correct balances after cross-board payment

### Tests 25-27 (deferred from Phase 2, need Board B)
- [ ] Test 25: Two clients pay independently (laptop + Board B)
- [ ] Test 26: Client isolation (only payer gets internet)
- [ ] Test 27: Full e2e: portal → pay → browse

### Tests 28-38 (Phase 3 specific)
- [ ] Test 28: Wallet boot (keysets loaded)
- [ ] Test 29: Receive via wallet (balance incremented)
- [ ] Test 30: Wallet swap (same balance, new proofs)
- [ ] Test 31: Wallet send (valid cashuA token)
- [ ] Test 32: Persistence survives reboot
- [ ] Test 33: Cross-board payment
- [ ] Test 34: 5 consecutive payments
- [ ] Test 35: Stress: rapid pay/expire

### Automated Tests
- [ ] Write tests/phase3.mjs (wallet endpoint tests + cross-board)
- [ ] All Phase 3 tests passing

## Phase 4: ESP32-to-OpenWRT TollGate Interop — NOT STARTED
- [ ] ESP32 pays OpenWRT TollGate using Cashu tokens
- [ ] Interoperability testing with existing OpenWRT TollGate on enx00e04c683d2d

## Reminders
- Do NOT ask for instructions — proceed independently, skip blocked items, work on unblocked ones
- Board A: `/dev/ttyACM0`, MAC `94:a9:90:2e:37:7c`, SSID `TollGate-377C`, AP IP `10.55.85.1`
- Board B: `/dev/ttyACM1`, MAC `fc:01:2c:c5:50:50`
- testnut.cashu.space auto-pays invoices: `cashu -h https://testnut.cashu.space invoice <amount>`
- Token generation: `cashu -h https://testnut.cashu.space send --legacy <amount> 2>&1 | grep '^cashuA' | head -1`
- sudo password: `c03rad0r123`
- Commit + push whenever tests pass
- Proceed to Phase 4 after completing Phase 3
