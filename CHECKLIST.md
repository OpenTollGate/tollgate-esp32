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

### Tests Passing
- [x] Tests 15-24: ALL PASSING

## Phase 3: On-Device Wallet + Nostr Identity + Wifistr — COMPLETE
### nucula Wallet Integration
- [x] Add nucula as git submodule (`nucula_src/`)
- [x] Create `components/secp256k1/` (symlink to nucula's libsecp256k1)
- [x] Create `components/nucula_lib/` (C++ bridge + C API)
- [x] C bridge: `nucula_wallet.h` (init, receive, send, swap_all, balance, proofs_json)
- [x] All wallet operations tested on Board A: pay, swap, send, persistence

### Nostr Identity Derivation (identity.c/h)
- [x] HMAC-SHA512 derivation via mbedtls, npub via secp256k1
- [x] Derive STA/AP MAC, SSID, AP IP from nsec
- [x] Set MACs via `esp_wifi_set_mac()` in boot sequence
- [x] 24/24 unit tests passing

### Nostr Event Signing (nostr_event.c/h)
- [x] NIP-01 canonical JSON, SHA-256 ID, Schnorr signature
- [x] 23/23 unit tests passing

### Geohash Encoding (geohash.c/h)
- [x] Standard base-32 geohash encoding
- [x] 11/11 unit tests passing

### Wifistr Service Discovery (wifistr.c/h)
- [x] kind 38787 event builder + WebSocket relay publish
- [x] Publish on boot + periodic timer (6h default)
- [x] Verified published to relay.damus.io and nos.lol

## Phase 4: ESP32 TollGate Client Detection + Auto-Payment — COMPLETE (commit `78dd599`)
- [x] tollgate_client.c/h — detection, payment, monitoring, state machine
- [x] Config fields: client_enabled, client_steps_to_buy, etc.
- [x] Integration into tollgate_main.c
- [x] 30/30 unit tests passing

## Phase 5: Lightning Auto-Payout — COMPLETE (commit `cb4bd7d`)
- [x] lnurl_pay.c/h — LNURL-pay HTTP flow
- [x] lightning_payout.c/h — periodic balance check, threshold, multi-recipient split, melt
- [x] nucula_wallet_melt() bridge for NUT-05
- [x] Config: payout.enabled, recipients, mints, fee_tolerance, etc.
- [x] 7/7 lnurl_pay + 11/11 lightning_payout = 18 unit tests passing

## Phase 6: Bytes-Based Billing — COMPLETE (commit `edd125d`)
- [x] Dual-metric session support (milliseconds + bytes)
- [x] session_create_bytes(), session_add_bytes()
- [x] Config: metric, step_size_bytes
- [x] Discovery endpoint advertises correct metric
- [x] Unit tests: bytes session lifecycle, mixed metrics

## Phase 7: MCP Handler + NIP-04 + CVM Server — COMPLETE (commit `fdf662f`)
- [x] mcp_handler.c/h — 4 tools (get_config, set_config, get_balance, wallet_send), 25 unit tests
- [x] nip04.c/h — AES-256-CBC + ECDH with 0x02 compressed pubkey prefix, 15 unit tests
- [x] cvm_server.c/h — Nostr DM listener skeleton with FreeRTOS task
- [x] Fixed NIP-04 IV bug: mbedtls_aes_crypt_cbc modifies IV in-place
- [x] Fixed missing esp_random.h include in nip04.c
- [x] 156 total unit tests passing across 10 test binaries

## Bug Fixes — COMPLETE (commit `3342c8e`)
- [x] reset_auth_handler now calls session_revoke_all() before firewall_revoke_all()
- [x] Port 80 /usage shows real session data (remaining/total) instead of "0/0"
- [x] Config metric defaults to "milliseconds" (ESP32 can't track per-client bytes from NAT)
- [x] Fixed sys_evt stack overflow: deferred start_services() to dedicated 32KB task

## Playwright Interop Tests — COMPLETE (commit `4fb44e7`)
- [x] 18/18 tests passing (11 ESP32 + 7 ESP32↔OpenWRT interop)
- [x] 7 screenshots generated
- [x] Double-spend rejection verified on live hardware

---

## TODO — In Progress

### Per-Client NAT Filtering (Multi-Client Fix)
- [ ] Create `main/lwip_tollgate_hooks.h` — LWIP_HOOK_IP4_CANFORWARD definition
- [ ] Update `CMakeLists.txt` — inject hook header into lwIP compilation
- [ ] Add `tollgate_ip4_canforward_filter()` to `firewall.c` — filter forwarded packets by source IP
- [ ] Change firewall strategy: NAT always ON, per-client filter in lwIP forwarding path
- [ ] Remove `update_nat()`, `firewall_enable_nat()`, `firewall_disable_nat()` from firewall.c
- [ ] Update `stop_services()` in tollgate_main.c — remove `firewall_disable_nat()` call
- [ ] Add unit test for filter function
- [ ] Build, flash, test on Board A
- [ ] Verify multi-client isolation: expire one client while other is active

### Spent-Secret Cleanup
- [ ] Remove `s_spent_secrets[]` and `session_is_secret_spent()` from `session.c`
- [ ] Remove `spent_secrets` field from `session_t` struct in `session.h`
- [ ] Remove `spent_secrets` params from `session_create()` and `session_create_bytes()`
- [ ] Remove local spent-secret check in `tollgate_api.c` (lines 227-239)
- [ ] Remove `secrets[]` array construction in `tollgate_api.c`
- [ ] Update `tests/unit/test_session.c` — remove secret-tracking tests
- [ ] Run `make test-unit` — all tests pass

### Integration Tests (tests/integration/)
- [ ] Create `tests/integration/` directory
- [ ] Move existing tests (api.mjs, network.mjs, smoke.mjs, phase2.mjs) into integration/
- [ ] Write `test-reset-auth.mjs` — verify sessions cleared after reset
- [ ] Write `test-session-lifecycle.mjs` — pay → verify usage → wait expiry → verify blocked (65s)
- [ ] Write `test-dns-firewall.mjs` — DNS hijack before auth, forward after auth
- [ ] Update Makefile targets for new paths
- [ ] All integration tests passing

### Test Reorganization
- [ ] Fix all hardcoded IPs → `process.env.TOLLGATE_IP`
- [ ] Move `tests/captive-portal.spec.mjs` → `tests/e2e/`
- [ ] Move `tests/interop-happy-path.spec.mjs` → `tests/e2e/` or `tests/integration/`
- [ ] Move `tests/playwright.config.mjs` → `tests/e2e/`

### Playwright Video Recording Fix
- [ ] Per-test context isolation (not shared serial context)
- [ ] Verify `.webm` files generated in test-results/

### OpenWRT Interop
- [ ] Investigate `nofee.testnut.cashu.space` API compatibility issues
- [ ] Fix cashu CLI v0.19.2 Pydantic validation failures with missing `active` field

### Board B
- [ ] Flash Board B with current firmware (different nsec)
- [ ] Cross-board payment test: Board B → Board A
- [ ] ESP32→ESP32 auto-payment (Scenario 5)

---

## Reminders
- **Commit + push every time a test passes that previously didn't pass**
- Board A: `/dev/ttyACM0`, MAC `94:a9:90:2e:37:7c`, SSID `TollGate-C0E9CA`, AP IP `10.192.45.1`
- Board B: `/dev/ttyACM1`, MAC `fc:01:2c:c5:50:50`
- OpenWRT Router: SSH `root@10.47.41.1`, port 2121
- `source ~/esp/esp-idf/export.sh` before `idf.py`
- Latest commit: `3342c8e`
- 156 unit tests + 18 Playwright tests — all passing
- sudo password: `c03rad0r123`
- Token generation: `cashu -h https://testnut.cashu.space send --legacy 21`
- See `AGENTS.md` for full testing rules
