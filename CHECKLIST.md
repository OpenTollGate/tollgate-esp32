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

## Phase 3: On-Device Wallet + Nostr Identity + Wifistr — IN PROGRESS
### nucula Wallet Integration
- [x] Add nucula as git submodule (`nucula_src/`)
- [x] Create `components/secp256k1/` (symlink to nucula's libsecp256k1)
- [x] Create `components/nucula_lib/` (C++ bridge + C API)
- [x] C bridge: `nucula_wallet.h` (init, receive, send, swap_all, balance, proofs_json)
- [x] All wallet operations tested on Board A: pay, swap, send, persistence

### Nostr Identity Derivation (identity.c/h)
- [x] Create `identity.h` — API: `identity_init(nsec_hex)`, derived value accessors
- [x] Create `identity.c` — HMAC-SHA512 derivation via mbedtls, npub via secp256k1
- [x] Derive STA MAC: `tollgate_derive(nsec, "sta-mac", 0)` → 6 bytes, locally administered
- [x] Derive AP MAC: `tollgate_derive(nsec, "ap-mac", 0)` → 6 bytes, locally administered
- [x] Derive SSID: `"TollGate-" + hex(AP_MAC[3:6])`
- [x] Derive AP IP: hash-based from AP MAC bytes
- [x] Compute npub: secp256k1 x-only pubkey from nsec
- [x] Set MACs via `esp_wifi_set_mac()` in boot sequence

### Nostr Event Signing (nostr_event.c/h)
- [x] Create `nostr_event.h` — NIP-01 event struct + sign/serialize API
- [x] Create `nostr_event.c` — canonical JSON, SHA-256 ID, Schnorr signature
- [x] Uses `secp256k1_schnorrsig_sign32()` for BIP-340 signatures

### Geohash Encoding (geohash.c/h)
- [x] Create `geohash.h` — `geohash_encode(lat, lon, precision, out)`
- [x] Create `geohash.c` — standard base-32 geohash encoding

### Wifistr Service Discovery (wifistr.c/h)
- [x] Create `wifistr.h` — `wifistr_publish()` API
- [x] Create `wifistr.c` — kind 38787 event builder + WebSocket relay publish
- [x] Build event with tags: d, ssid, h, security, g, c
- [x] WebSocket client: raw TCP + TLS (esp_tls.h) + HTTP Upgrade
- [x] Publish on boot + periodic timer (6h default)

### Config Changes (config.c/h)
- [x] Add to struct: nsec, npub, nostr_geohash, nostr_relays, nostr_publish_interval_s, sta_mac, ap_mac
- [x] Remove from JSON parsing: ap_ssid, ap_ip (now derived from nsec)
- [x] Keep: ap_password, ap_channel, ap_max_conn (hardcoded defaults)
- [x] Update default config.json template with nsec and Nostr fields

### Boot Sequence Changes (tollgate_main.c)
- [x] Call `identity_init(nsec)` after config load, before WiFi init
- [x] Set STA/AP MAC via `esp_wifi_set_mac()` after `esp_wifi_init()`, before `esp_wifi_start()`
- [x] Remove old `tollgate_config_derive_unique()` call
- [x] Use derived SSID/IP in AP configuration
- [x] Start wifistr publish task after services start

### Build System
- [x] Add identity.c, nostr_event.c, geohash.c, wifistr.c to CMakeLists.txt SRCS
- [x] Add `secp256k1` to REQUIRES (for identity.c and nostr_event.c)
- [x] Clean build (0 errors, 0 warnings)

### Hardware Testing
- [x] Flash Board A, verify wallet boot (keyset fetch succeeds)
- [x] Pay Board A with Cashu token, verify proofs stored (GET /wallet)
- [x] Test POST /wallet/swap on Board A
- [x] Test POST /wallet/send on Board A, verify token is valid
- [x] Flash Board A with new identity derivation, verify derived SSID/MAC/IP
- [x] Verify captive portal works with new SSID/IP
- [x] Verify payment flow still works with identity-derived config
- [x] Verify wifistr event published to relay (damus + nos.lol)
- [ ] Flash Board B with new firmware (different nsec)
- [ ] Cross-board payment: Board B token → Board A
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

## Test Coverage — IN PROGRESS

### Host Unit Tests (tests/unit/)
- [ ] Create `tests/unit/stubs/` — clean ESP-IDF type stubs for host compilation
- [ ] Create `tests/unit/Makefile` — compiles all unit tests with host gcc
- [ ] Install system deps: `libmbedtls-dev`, `libcjson-dev`
- [ ] `test_geohash.c` — geohash_encode against reference vectors (Munich, NYC, origin)
- [ ] `test_identity.c` — HMAC-SHA512 derivation, MAC bits, SSID/IP determinism
- [ ] `test_nostr_event.c` — NIP-01 event ID, Schnorr sign+verify, JSON serialization
- [ ] `test_cashu.c` — token decode, allotment calc, mint validation
- [ ] `test_session.c` — session lifecycle, expiry, spent-secret dedup
- [ ] `make test-unit` passes all unit tests

### Test Reorganization
- [ ] Move `tests/api.mjs` → `tests/integration/phase1_api.mjs`
- [ ] Move `tests/network.mjs` → `tests/integration/phase1_network.mjs`
- [ ] Move `tests/smoke.mjs` → `tests/integration/smoke.mjs`
- [ ] Move `tests/phase2.mjs` → `tests/integration/phase2.mjs`
- [ ] Move `tests/captive-portal.spec.mjs` → `tests/e2e/captive-portal.spec.mjs`
- [ ] Move `tests/playwright.config.mjs` → `tests/e2e/playwright.config.mjs`
- [ ] Fix all hardcoded IPs (`192.168.4.1`) → `process.env.TOLLGATE_IP`

### New Integration Tests
- [ ] `tests/integration/phase3.mjs` — wallet GET/swap/send, identity SSID/IP, wifistr on relay
- [ ] All Phase 3 integration tests passing

### New E2E Tests
- [ ] `tests/e2e/payment.spec.mjs` — paste token → pay → success, error handling, full flow
- [ ] All E2E tests passing

### Build System Updates
- [ ] Update `Makefile` with `test-unit`, `test-integration`, `test-e2e`, `test-all` targets
- [ ] Update `package.json` npm scripts for new paths
- [ ] All `make test-*` targets work

## Phase 4: ESP32-to-OpenWRT TollGate Interop — NOT STARTED
- [ ] ESP32 pays OpenWRT TollGate using Cashu tokens
- [ ] Interoperability testing with existing OpenWRT TollGate on enx00e04c683d2d

## Reminders
- Do NOT ask for instructions — proceed independently, skip blocked items, work on unblocked ones
- **Commit + push every time a test passes that previously didn't pass**
- Board A: `/dev/ttyACM0`, factory MAC `94:a9:90:2e:37:7c`
- Board B: `/dev/ttyACM1`, factory MAC `fc:01:2c:c5:50:50`
- Identity is now derived from nsec in config.json (SSID, IP, MAC all deterministic)
- testnut.cashu.space auto-pays invoices: `cashu -h https://testnut.cashu.space invoice <amount>`
- Token generation: `cashu -h https://testnut.cashu.space send --legacy <amount> 2>&1 | grep '^cashuA' | head -1`
- sudo password: `c03rad0r123`
- Run `make test-unit` after any code change — must pass before commit
- See `AGENTS.md` for full testing rules and project context
- Proceed to Phase 4 after completing Phase 3
