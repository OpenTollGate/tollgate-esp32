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

## Phase 7: MCP Handler + NIP-04 + CVM Server — SKELETON (commit `fdf662f`)
- [x] mcp_handler.c/h (4 tools, 25 unit tests)
- [x] nip04.c/h (AES-256-CBC + ECDH, 15 unit tests)
- [x] cvm_server.c/h (Nostr DM listener skeleton)

## Phase 7b: ContextVM Protocol Rewrite — COMPLETE
- [x] Add 6 new tools to mcp_handler.c/h (get_sessions, get_usage, set_payout, set_metric, set_price, wallet_melt)
- [x] Update test_mcp_handler.c with tests for 6 new tools
- [x] Rewrite cvm_server.c: persistent WebSocket listener, kind 25910 subscription
- [x] MCP protocol handlers: initialize, notifications/initialized, tools/list, tools/call, ping
- [x] Auth check: only accept from owner npub
- [x] CEP-6: publish kind 11316 server announcement on startup
- [x] CEP-6: publish kind 11317 tools list on startup
- [x] CEP-17: publish kind 10002 relay list on startup
- [x] Update config.c: default cvm_enabled = true
- [x] Create test_cvm_server.c unit test (event parsing, announcement construction, auth)
- [x] Update tests/unit/Makefile with test_cvm_server target
- [x] Create tests/integration/test-cvm.mjs (nak-based integration test)
- [x] Update Makefile with cvm-* targets (test-cvm, cvm-pubkey, cvm-test-tool)
- [x] WS frame masking fix (RFC 6455 client-to-server)
- [x] EVENT msg buffer underflow fix (snprintf buffer size)
- [x] TLS write loop for large payloads
- [x] WS ping/pong keepalive (30s interval)
- [x] Subscription REQ fix (removed invalid limit field)
- [x] SNTP init after STA gets IP
- [x] 282 unit tests passing (61 CVM + 60 MCP + 161 existing)

## Phase 7c: CVM Integration Testing — IN PROGRESS
- [x] Per-board hardware locks implemented (board-a/b/c.lock)
- [x] Lock infrastructure in 3 Makefiles (esp32-tollgate, physical-router-test-automation/esp32, top-level)
- [x] CVM test infrastructure verified (API check, relay queries, event publishing)
- [x] Fix CVM test API reachability check (HTTP status instead of JSON parse)
- [x] WiFi password fix for EnterSSID-2.4GHz (c03rad0r123! — was missing `!`)
- [x] WiFi auth threshold fix (WPA3_PSK → WPA2_PSK → WIFI_AUTH_OPEN, now WPA2_PSK)
- [x] PMF capable mode enabled
- [x] WIFI_ALL_CHANNEL_SCAN enabled
- [x] WiFi country code fix (ESP-IDF defaults to CN, need DE for EU regulatory compliance)
- [x] 2s retry delay between WiFi auth attempts
- [x] Board B connects to WiFi successfully with country code DE
- [x] Board A confirmed as hardware WiFi issue (auth fails on all APs, Board B works fine)
- [x] Board B CEP-6 announcements confirmed on relay.primal.net
- [x] Verify kind 11316 announcement on relay.primal.net — PASS
- [x] Verify kind 11317 tools list on relay.primal.net — PASS
- [x] Verify kind 10002 relay list on relay.primal.net — PASS
- [x] Fix subscription #p filter (must be array, not string) — relay rejected as 'bad req'
- [x] Fix MCP response publishing (use existing WS instead of new TLS connection)
- [x] Fix use-after-free bug (tags_str freed before nostr_event_to_json)
- [x] MCP initialize roundtrip via kind 25910 — PASS
- [x] tools/call get_config via kind 25910 — PASS
- [x] tools/call get_balance via kind 25910 — PASS
- [x] tools/list response via kind 25910 — PASS
- [x] tools/call set_price via kind 25910 — PASS (price updated to 42)
- [ ] tools/call get_sessions via kind 25910
- [ ] tools/call get_usage via kind 25910
- [ ] Non-owner auth rejection via live relay (unit test only so far)
- [ ] Verify board npub on contextvm.org/servers
- [ ] Fix relay disconnect cycle (rlen=-26880 every ~15s)
- [ ] Clean up debug logging (reduce INFO→DEBUG for verbose messages)
- [ ] Document Board A hardware issue in AGENTS.md

### WiFi Debugging Findings (Board A — 94:a9:90:2e:37:7c)
- **Symptom:** `WIFI_REASON_AUTH_EXPIRED` (0x200) on all upstream APs
- **APs tested:** EnterSSID-2.4GHz (ch11, WPA2), c03rad0r (not in range), laptop hotspot (ch6, WPA2)
- **Modes tested:** APSTA (ch1/6/11), STA-only (no AP at all)
- **MAC tested:** Custom (derived from nsec) and factory MAC
- **Result:** Auth fails in ALL configurations, even STA-only 1m from laptop hotspot
- **Root cause hypothesis 1:** Missing WiFi country code — ESP-IDF defaults to CN regulatory domain, boards are in DE. Different TX power limits and channel parameters may cause APs to ignore ESP32 auth frames.
- **Root cause hypothesis 2:** Hardware antenna issue on Board A — needs testing on other boards to confirm
- **Spectrum:** Dense environment (ch1: 2 APs, ch6: 4 APs, ch11: 4 APs) but laptop connects fine at 100%
- **Next step:** Add `esp_wifi_set_country_code("DE")` and test Board A, then Board B/C if needed

### Per-Board Hardware Locks
- [x] Lock files in `physical-router-test-automation/locks/` (board-a.lock, board-b.lock, board-c.lock)
- [x] `lock-a/b/c`, `unlock-a/b/c`, `force-unlock-a/b/c` targets
- [x] All hardware-touching targets require corresponding board lock
- [x] Read-only targets (build, cvm-pubkey, lock-status) work without lock
- [x] Board port mapping updated: A=ACM0, B=ACM1, C=ACM3

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

## TFT Display (JC3248W535 / AXS15231B) — IN PROGRESS
- [x] Create QR code component (port qrcoded from NSD, fix bool/pragma/comparison warnings)
- [x] Create AXS15231B QSPI display driver component (init sequence, PSRAM framebuffer, chunked flush)
- [x] Create 8x8 bitmap font (ASCII 32-127)
- [x] Create display abstraction layer (display.h/c — boot/ready/payment/error states)
- [x] Integrate display into tollgate_main.c and main/CMakeLists.txt
- [x] Build succeeds (binary 1.2MB, 71% free in partition)
- [x] Wi-Fi QR code encoding: `WIFI:S:<escaped_ssid>;T:nopass;;` with special char escaping (`\;:,"`)
- [x] QR cycling: alternate between Wi-Fi QR and portal URL QR every 5 seconds
- [ ] Flash to JC3248W535 board at `/dev/ttyACM0` and test
- [ ] Verify Wi-Fi QR is scannable by Android/iOS camera
- [ ] Verify portal URL QR is scannable and loads captive portal
- [ ] Add unit tests for QR generation and escape_wifi_field()
- [ ] Update AGENTS.md with display module docs

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
- Board A: `/dev/ttyACM0`, MAC `94:a9:90:2e:37:7c`, SSID `TollGate-B96D80`, AP IP `10.185.47.1`
- Board B: `/dev/ttyACM1`, MAC `fc:01:2c:c5:50:50`, SSID `TollGate-C0E9CA`, AP IP `10.192.45.1`
- Board C: `/dev/ttyACM3`, MAC `20:6e:f1:98:d7:08`
- `source ~/esp/esp-idf/export.sh` before `idf.py`
- sudo password: `c03rad0r123`
- Token generation: `cashu -h https://testnut.cashu.space send --legacy 21`
- SPIFFS offset `0x410000`, size `0xF0000`
- See `AGENTS.md` for full testing rules
- **Per-board locks:** `make lock-a PHASE="desc"` before hardware access
- **WiFi country code:** Must set `esp_wifi_set_country_code("DE")` before `esp_wifi_start()`
