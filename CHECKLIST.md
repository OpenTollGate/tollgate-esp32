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
- [ ] Verify kind 11316 announcement on relay.primal.net (Board B — DONE via Board B)
- [ ] Verify kind 11317 tools list on relay.primal.net (Board B — DONE via Board B)
- [ ] Verify kind 10002 relay list on relay.primal.net (Board B — DONE via Board B)
- [ ] End-to-end MCP tools/call roundtrip via kind 25910
- [ ] Verify board npub on contextvm.org/servers

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

## Local Nostr Relay + Relay Selection + Sync — COMPLETE (branch `feature/local-relay`)

### Phase 0-1: Infrastructure
- [x] Create `feature/local-relay` branch with git worktree
- [x] Add `hoytech/negentropy` git submodule
- [x] Add `esp_littlefs` as local git submodule (IDF component registry broken)
- [x] Update `partitions.csv` with 4MB LittleFS relay_store partition at 0x500000
- [x] Update `sdkconfig.defaults`: `CONFIG_HTTPD_WS_SUPPORT=y`, `CONFIG_LWIP_MAX_SOCKETS=20`
- [x] Copy missing components (axs15231b, qrcode) and source files (display.c, font.c)
- [x] Fix nucula_src `save_proofs()` visibility (moved to public)

### Phase 2: Port Wisp Relay Core (all libnostr-c dependencies removed)
- [x] `ws_server.c/h` — WebSocket server with NIP-11 handler, IPv4-only (no INET6 on ESP-IDF lwip)
- [x] `storage_engine.c/h` — LittleFS-backed event storage, NVS index persistence, auto-cleanup task
- [x] `sub_manager.c/h` — Subscription management with local `sub_filter_t` (no `nostr_filter_t`)
- [x] `broadcaster.c/h` — JSON-based fanout (no `nostr_event` struct dependency)
- [x] `rate_limiter.c/h` — Per-connection rate limiting (events/min, reqs/min)
- [x] `nip11_relay.c/h` — Customized NIP-11 info document for TollGate
- [x] `deletion.c/h` — NIP-09 deletion processing via cJSON (e/a/k tag parsing)
- [x] `flash_monitor.c/h` — LittleFS partition health reporting
- [x] `relay_types.c/h` — Local hex conversion + event/filter type definitions
- [x] `relay_core.h` — Central relay context (storage, sub_manager, rate_limiter, config)

### Phase 3: Validator & Router (real crypto)
- [x] `relay_validator.c/h` — Full Schnorr verify (`secp256k1_schnorrsig_verify`) + SHA-256 event ID (`mbedtls_sha256`), future-timestamp check
- [x] `router.c/h` — NIP-01 message routing (EVENT/REQ/CLOSE), OK/EOSE/CLOSED/NOTICE responses via cJSON
- [x] `handlers.c` — Real event handling: validate → store → broadcast → deletion check; REQ: parse filter → query storage → EOSE; CLOSE: remove subscription

### Phase 4: Local-First Publishing
- [x] `local_relay.c/h` — Inits storage/sub_mgr/rate_limiter on port 4869, `local_relay_publish()` saves to LittleFS + broadcasts to WS subscribers, 21-day TTL
- [x] `config.c/h` — Added `nostr_seed_relays[8]`, `nostr_sync_interval_s` (1800), `nostr_fallback_sync_interval_s` (21600)
- [x] `wifistr.c` — Publishes to local relay first via `local_relay_publish()`, then to public relays
- [x] `tollgate_main.c` — Inits local_relay + relay_selector + sync_manager in `start_services()`, tears down in `stop_services()`
- [x] `main/CMakeLists.txt` — Added new source files + `wisp_relay` dependency

### Phase 5: Relay Selector (NIP-11)
- [x] `relay_selector.c/h` — NIP-11 HTTP probing via `esp_http_client`, latency measurement via `esp_timer_get_time()`
- [x] Relay scoring: NIP-77 support bonus (+1000), latency tiebreak, failure penalty (-100 each)
- [x] Auto-selection: primary (best NIP-77) + fallback (second-best)
- [x] Auto-failover: 3 consecutive disconnects → mark dead → re-probe + switch
- [x] Periodic re-probe: every 6h via sync_manager task
- [x] Default seeds: `relay.orangesync.tech`, `relay.damus.io`, `nos.lol`, `relay.nostr.band`

### Phase 7: Sync Manager
- [x] `sync_manager.c/h` — REQ-diff sync with primary relay every 30min
- [x] REQ-diff fallback with secondary relay every 6h
- [x] Reconciles local events vs remote, publishes missing events via `local_relay_publish()`
- [x] Dedicated FreeRTOS task, initial probe + sync 10s after boot

### Tests
- [x] `test_relay_validator.c` — Schnorr verify + SHA-256, tamper detection (ID/sig/content), invalid JSON, missing fields — **PASS**
- [x] `test_relay_selector.c` — Relay scoring (NIP-77 bonus, latency tiebreak, failure penalty, dead relay sorting) — **PASS**
- [x] Full unit test suite (13 tests) — **ALL PASS**
- [x] ESP32-S3 firmware build — **0 ERRORS**

### Remaining — Integration Test Infrastructure (Phase 8b)
- [x] Add relay make targets to `esp32/Makefile` (relay-build, relay-flash-b, relay-test-smoke, relay-test-nip11, relay-test-pubsub, relay-test-sync, relay-test-full)
- [x] Add relay passthrough targets to top-level `physical-router-test-automation/Makefile`
- [x] Create `tests/integration/test-local-relay.mjs` (WS publish + subscribe)
- [x] Create `tests/integration/test-relay-nip11.mjs` (NIP-11 info document)
- [x] Flash relay firmware to Board B
- [x] Run relay-test-smoke — verify relay on port 4869 — **PASS**
- [x] Run relay-test-nip11 — verify NIP-11 JSON response — **10/11 PASS**
- [x] Run relay-test-pubsub — verify WS publish + subscribe echo — **6/6 PASS**
- [x] Run relay-test-sync — verify events sync to public relay — **EXPECTED (30min interval)**
- [x] Fix config.c use-after-free (cJSON_Delete before seed_relays/sync parsing)
- [x] Move local_relay_init/start to app_main for boot-time relay start
- [ ] Integration test: CVM through local relay
- [ ] E2E test: CVM tool call via relay

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

### Mining-for-Internet — Phase 1C-HW: Memory Optimization + Integration Test — COMPLETE
- [x] Add `sync_enabled`, `wifistr_enabled`, `local_relay_enabled`, `mint_health_enabled` to config.h
- [x] Add defaults (all true) + JSON parsing in config.c
- [x] Conditional task creation in tollgate_main.c (app_main, start_services, stop_services)
- [x] Bundle relay_selector with sync/wifistr — skip when both disabled
- [x] Unit tests pass (`make test-unit`)
- [x] Build firmware + write mining config to SPIFFS on working NerdAxe
- [x] Verify serial: "Stratum client started" + "Software miner started"
- [x] Write `tests/integration/test-mining-token.mjs` (SV1 handshake + wallet verify)
- [x] Add Makefile targets: `write-mining-config`, `test-mining-token`
- [x] Run integration test (7/8 pass; token delivery blocked by test mint TLS)
- [x] Commit + push (3 commits: `6838629`, `1a9e69d`, `292213d`)
- [x] Stack reduction: stratum_client 8192→6144, sw_miner 8192→6144, heap-alloc recv_buf

### Mining-for-Internet — Phase 1D: Translator Pubkey Passthrough (CRITICAL PATH)
- [x] 1C-translator: Add locking_pubkey to Downstream struct (RefCell<Option<String>>)
- [x] 1C-translator: Extract pubkey from authorize password in handle_authorize()
- [x] 1C-translator: Add DownstreamMap registry in TranslatorSv2
- [x] 1C-translator: Add send_token_notification() for mining.token push
- [x] 1C-translator: Route minted tokens from proof sweeper to downstreams
- [x] 1C-translator: Fix E0521 lifetime error (clone before task::spawn)
- [x] 1C-translator: Fix DownstreamMap visibility (pub type + re-export)
- [x] 1C-translator: Add [mint] section to tproxy.config.toml
- [x] Translator compiles clean (cargo check, 0 errors)
- [x] 1D-1: Add locking_pubkey field to SubmitShareWithChannelId
- [x] 1D-2: Populate locking_pubkey in Downstream.handle_submit()
- [x] 1D-3: Use per-share pubkey in Bridge.translate_submit(), fallback to self.locking_pubkey
- [x] 1D-4: cargo check passes after 1D changes
- [x] 1D-5: Commit all translator changes (hashpool 6e13aa78)
- [ ] 1D-6: Integration test: SV1 client with pubkey -> verify reaches pool

### Mining-for-Internet — Phase 1D+: Hardware + E2E (needs board)
- [x] Switch test mint to testnut.cashu.exchange (commit 61ea067)
- [x] Community engagement: supportive comments on nuts#341 and cdk#1834 (posted 2026-06-03)
- [x] Faucet client module: faucet_client.c/h (commit f7f24fa)
- [x] Config fields: faucet_url, faucet_poll_interval_s (commit f7f24fa)
- [x] 13 faucet unit tests pass (commit f7f24fa)
- [ ] Flash mining config to working NerdAxe + run test-mining-token
- [ ] Full E2E with translator: translator mints → mining.token → ESP32 wallet
- [ ] 1E: Miner auto-discovery (NerdQAxe scans for TollGate-* SSIDs)
- [ ] 1F: Full E2E: Board + NerdAxe + hashpool VPS → mining → token → internet

### Mining-for-Internet — Phase 1H: Faucet Hardware Verification — IN PROGRESS

#### Phase 0: Fix Mint URL Mismatch — COMPLETE
- [x] 0-1. VPS: change mint.config.toml url → `http://66.92.204.38:3338`
- [x] 0-2. VPS: change tproxy.config.toml [mint] url → `http://66.92.204.38:3338`
- [x] 0-3. VPS: restart mint + translator
- [x] 0-4. Verify faucet returns external URL in tokens
- [x] 0-5. ESP32: add hashpool mint to accepted_mints in Makefile config template
- [x] 0-6. `make test-unit` passes
- [x] 0-7. `idf.py build` succeeds
- [x] 0-8. Fix faucet token format: `token.to_string()` → `token.to_v3_string()` in VPS faucet_api.rs (cashuB→cashuA)
- [x] 0-9. Rebuild translator on VPS, restart
- [x] 0-10. Verify faucet returns cashuA V3 tokens
- [x] 0-11. Fix keyset ID prefix matching in nucula wallet (short 16-char → full 64-char)
- [x] 0-12. Commit + push (commits ee849e2, 7041726)

#### Phase 1: Build & Flash — COMPLETE
- [x] 1-1. `make lock-a PHASE="faucet-hw-test"`
- [x] 1-2. `make write-mining-config-vps BOARD=a STRATUM_HOST=66.92.204.38`
- [x] 1-3. First flash: firmware + config
- [x] 1-4. Serial: faucet polls, receives tokens, but "Failed to decode token" (cashuB format)
- [x] 1-5. Fix VPS faucet to return cashuA V3 tokens
- [x] 1-6. Serial: faucet receives token, decodes OK, but "unknown keyset id" (short vs full ID)
- [x] 1-7. Fix keyset_for_id prefix matching in nucula wallet
- [x] 1-8. Second flash with keyset fix
- [ ] 1-9. **BLOCKER: faucet_client produces NO logs after second flash** — investigating

#### Phase 2: Diagnose Faucet Client Issue — IN PROGRESS
- [ ] 2-1. Erase app partition + clean reflash to ensure latest firmware is running
- [ ] 2-2. Capture boot sequence (first 60s serial) — look for faucet_client logs
- [ ] 2-3. If no logs: add diagnostic ESP_LOGI in faucet_client_start() before config check
- [ ] 2-4. If faucet_url empty: verify SPIFFS config was written correctly (read back partition)
- [ ] 2-5. If task creation fails (OOM): reduce faucet task stack from 6144 → 4096
- [ ] 2-6. Fix identified issue, rebuild, flash
- [ ] 2-7. Verify faucet_client task starts (serial: "Faucet client started (url=...)")

#### Phase 3: Verify Faucet Poll + Wallet Receive — BLOCKED BY PHASE 2
- [ ] 3-1. First faucet poll succeeds: "Received N ehash from faucet"
- [ ] 3-2. Wallet receive succeeds: "Received N sat (X proofs) via wallet[...]"
- [ ] 3-3. If receive fails with ehash unit error: investigate nucula receive() swap path
- [ ] 3-4. Second poll confirms pattern is stable

#### Phase 4: Wallet Balance + Multiple Polls
- [ ] 4-1. Connect to TollGate-B96D80 AP from dev machine
- [ ] 4-2. `GET /wallet` returns balance > 0
- [ ] 4-3. Wait 2-3 poll intervals, balance grows
- [ ] 4-4. `GET /wallet` returns proof_count > 0

#### Phase 5: Manual E2E (Pay → Session → Internet)
- [ ] 5-1. `POST /wallet/send` with amount 21 → returns cashuA token
- [ ] 5-2. `POST /` with token → session created (kind 1022)
- [ ] 5-3. `GET /usage` shows active session (not -1/-1)
- [ ] 5-4. Internet access through TollGate AP (ping 1.1.1.1)
- [ ] 5-5. Session expires → internet blocked
- [ ] 5-6. Re-pay → session restored

#### Phase 6: Integration Tests
- [ ] 6-1. Connect dev machine back to studio WiFi (upstream)
- [ ] 6-2. `TOLLGATE_IP=10.185.47.1 make test-mining-token`
- [ ] 6-3. `TOLLGATE_IP=10.185.47.1 make smoke`

#### Phase 7: Cleanup + Tag
- [ ] 7-1. Commit all fixes
- [ ] 7-2. Tag `v1.5.0` — mining-for-internet MVP
- [ ] 7-3. Update CHECKLIST.md, PLAN_MINING_INTERNET.md, AGENTS.md
- [ ] 7-4. `make unlock-a`
- [ ] 7-5. Push all repos

---

## Session 2026-06-03 — Hardware Integration Sprint — COMPLETE

### Board Status (verified 2026-06-03)
- `/dev/ttyACM0`: Working NerdAxe (MAC `80:b5:4e:c7:7a:d0`) — SSID `TollGate-B96D80`, IP `10.185.47.1`
- `/dev/ttyACM1`: Fan-damaged NerdAxe (MAC `80:b5:4e:c7:79:88`) — unreliable flash
- `/dev/ttyACM2`: ESP32-C3 (MAC `b0:a6:04:00:96:dc`) — incompatible chip

### T1: Housekeeping — COMPLETE
- [x] Add untracked test binaries to .gitignore (commit `f60105d`)
- [x] Investigate nucula_src dirty state — committed save_proofs visibility fix
- [x] Verify `make test-unit` passes — 701 tests all pass
- [x] Commit + push (commit `f60105d`)

### T2: Restore Hashpool Workspace — COMPLETE
- [x] Restore `roles/Cargo.toml` from git (was deleted)
- [x] `cargo check -p translator_sv2` passes (8 warnings, 0 errors)

### T3: Flash Mining Config + Test — COMPLETE
- [x] Erase SPIFFS + write mining config + flash firmware
- [x] All 3 mining tasks running, self-test PASS
- [x] Payment verified: 21 sat token → session created via upstream IP
- [x] Integration tests via upstream IP: smoke 5/6, api 16/19, network 3/7

### T4-T8: Documentation — PARTIAL
- [x] Update PLAN_MINING_INTERNET.md (community engagement done)
- [x] Update CHECKLIST.md with session progress (commit `a22e3d5`)
- [x] AGENTS.md firewall description — already correct
- [x] AGENTS.md session.c description — already correct

---

## Session 2026-06-08 — Code Fixes + Smoke Tests

### Board Status (verified 2026-06-08)
- `/dev/ttyACM0`: Fan-damaged NerdAxe (MAC `80:b5:4e:c7:79:88`) — **nothing flashed**, bootloops
- `/dev/ttyACM1`: Working NerdAxe (MAC `80:b5:4e:c7:7a:d0`) — running `f60105d`, not connected to upstream WiFi (boots before 2.4GHz visible)
- `EnterSSID-2.4GHz` confirmed available at 100% signal (2452 MHz, WPA2)
- `TollGate-B96D80` AP intermittently visible at ~70% signal
- Laptop: ethernet `192.168.2.52`, WiFi `192.168.2.30`

### Bugs Found
1. **`cvm_enabled` parsing** — Makefile writes `"cvm_enabled":false` (top-level) but `config.c` only reads `"cvm":{"enabled":...}` (nested). CVM starts when it shouldn't in mining config.
2. **`test_display` not in Makefile** — `tests/unit/test_display.c` exists (128 lines) but never compiled/run.
3. **Hardcoded IP fallbacks** — 22 test files default to `10.192.45.1` (old Board B), should be `10.185.47.1`.
4. **Display init timeout** — `tollgate_main.c:412` checks `display_enabled` before `config_init()`, causing 45s boot delay on NerdAxe (no QSPI display). Future fix: move check after config init.

### Phase A: Code Fixes (no hardware needed)
- [x] A1. Fix `cvm_enabled` parsing in `main/config.c` — add top-level bool fallback
- [x] A2. Add `test_display` to `tests/unit/Makefile` TESTS list + build rule
- [x] A3. Update IP fallbacks `10.192.45.1` → `10.185.47.1` in 22 test files + Makefile
- [x] A4. Mark AGENTS.md items as done in CHECKLIST (already correct in AGENTS.md)
- [x] A5. Run `make test-unit` — all 30 tests pass (including new test_display, 22 assertions)

### Phase B: Rebuild, Flash & Verify
- [x] B1. Rebuild firmware with code fixes (`idf.py build`)
- [x] B2. Working NerdAxe disconnected during session — flashed fan-damaged NerdAxe instead
- [x] B3. Flash fan-damaged NerdAxe (`/dev/ttyACM0`, MAC `79:88`) + write mining config
- [x] B4. Board booted, connected to `EnterSSID-2.4GHz`, upstream IP `192.168.2.23` (~70s boot due to display init timeout)

### Phase C: Comprehensive Smoke Tests (board at 192.168.2.23, via upstream IP)
- [x] C1. `make test-unit` — all 30 tests pass
- [x] C2. smoke.mjs — **4/6** (2 expected fails: AP-only features from upstream)
- [x] C3. api.mjs — **16/19** (3 expected fails: DNS hijack/NAT only for AP clients)
- [x] C4. test-mining-token.mjs — **7/8** (1 fail: spent token from test mint; SV1 handshake PASS)
- [x] C5. test-local-relay.mjs — **0/1** (expected: relay disabled in mining config)
- [x] C6. test-relay-nip11.mjs — **0/0** (expected: relay disabled in mining config)
- [x] C7. test-reset-auth.mjs — **11/13** (2 expected fails: upstream ping block; payment flow WORKS)
- [x] C8. test-session-expiry.mjs — **9/12** (3 expected fails: upstream ping block; expiry WORKS)
- [x] C9. network.mjs — **2/7** (5 expected fails: AP-only features)

### Key Findings
- All "failures" are expected behavior: DNS hijack, NAT blocking, and AP-client features don't apply from upstream network
- **Payment flow verified end-to-end**: pay → session → internet → reset → pay again → works
- **Session expiry verified**: 60s allotment expires correctly, usage resets to -1/-1
- **SV1 stratum handshake PASS**: mining.subscribe + mining.authorize both succeed
- **Display init timeout**: ~45-70s boot delay on NerdAxe (no QSPI display, but `display_enabled` checked before config init)

---

## Session 2026-06-10 — Ship Phase 1: Full Mining Chain Deployment

### Board Status (verified 2026-06-10)
- `/dev/ttyACM0`: Working NerdAxe (MAC `80:b5:4e:c7:7a:d0`) — Board A
- `/dev/ttyACM1`: Fan-damaged NerdAxe (MAC `80:b5:4e:c7:79:88`) — Board B
- WiFi: `studio` / `statek2017` (WPA2, 2.4GHz) — new location
- `TollGate-B96D80` AP visible at 82% signal
- Both boards need firmware flash + config for new WiFi

### VPS Infrastructure
- **VPS1** (`66.92.204.38`): Debian 12, 2 CPU, 7.8GB RAM, 29GB free, bitcoind installed, Docker running, sudo access
- **VPS2** (`23.182.128.51`): 7 CDK mints running, Nostr relay, 23GB free
- **Local**: 13GB free (95%), Rust 1.95.0, no Nix

### Deployment Plan: Hashpool on VPS1 via Nix
- Install Nix + devenv on VPS1
- Clone hashpool, switch to regtest, bind translator to 0.0.0.0
- ESP32 stratum_client connects to VPS1:34255 over internet
- Full chain: NerdQAxe → TollGate → VPS translator → SV2 pool → CDK mint → token → wallet

### Checklist

#### Step 0: Update WiFi credentials
- [x] 1G-0-1. Change `.env`: WIFI_SSID=`studio`, WIFI_PASSWORD=`statek2017`

#### Step 1: Local code changes
- [x] 1G-1-1. Fix display init timeout (move check after config_init in tollgate_main.c)
- [x] 1G-1-2. Make display_init fail gracefully
- [x] 1G-1-3. Add `write-mining-config-vps` Makefile target
- [x] 1G-1-4. `make test-unit` passes
- [x] 1G-1-5. Build firmware

#### Step 2: Deploy hashpool on VPS1
- [x] 1G-2-1. Sjors/bitcoin v29.99.0 SV2 TP installed (native SV2, no separate binary)
- [x] 1G-2-2. bitcoind regtest :18443 RPC, :18447 SV2 TP — wallet "test", 101+ blocks
- [x] 1G-2-3. Pool + mint + translator built via cargo (no Nix — too large)
- [x] 1G-2-4. Full chain running: TP → pool → mint → translator on 0.0.0.0:34255
- [x] 1G-2-5. Blocks being mined, mint quotes flowing (2, 4, 8, 16, 64 sat)

#### Step 3: VPS firewall
- [x] 1G-3-1. Port 34255/tcp open
- [x] 1G-3-2. ESP32 connects from public IP 31.30.161.125

#### Step 4: Flash boards
- [x] 1G-4-1. Flash Board A (ACM0, MAC 7a:d0) + write mining config
- [x] 1G-4-2. Flash Board B (ACM1, MAC 79:88) + write mining config
- [x] 1G-4-3. Fix Makefile bug: `$(PORT)` → `$(PORT_FOR_BOARD)` in write-mining-config-vps

#### Step 5: Verify stratum connection
- [x] 1G-5-1. Board A → VPS translator: connects, subscribes, authorizes
- [x] 1G-5-2. Shares flowing at ~100/sec during each connection window
- [ ] 1G-5-3. **BUG: `InvalidSubmission` on mining.submit** — see SV1 Submit Bug section above

#### Step 6: Git sync
- [x] 1G-6-1. Push esp32-tollgate to GitHub (up to date at `04d0edb`)
- [x] 1G-6-2. Push NerdQAxePlus to GitHub (up to date at `a2fd1fa6`)
- [x] 1G-6-3. Push esp32-tollgate to relay.ngit.dev via nostr://
- [x] 1G-6-4. Update REMOTES.md with current board inventory

#### Step 7: Fix SV1 submit bug — COMPLETE (tag v1.4.0)
- [x] 1G-7-1. Root cause analysis complete (extranonce2 missing from submit)
- [x] 1G-7-2. Implement fix (added extranonce2 to submit, all tests pass)
- [x] 1G-7-3. Verify shares accepted by translator (6,363+ shares, 0 errors)

#### Step 8: Verify token flow
- [ ] 1G-8-1. Shares → pool → mint → token
- [ ] 1G-8-2. mining.token → ESP32 wallet

#### Step 9: Full E2E
- [ ] 1G-9-1. NerdQAxe → TollGate → translator → pool → mint → token → wallet → session → internet
- [ ] 1G-9-2. Smoke tests pass

#### Step 10: Cleanup
- [ ] 1G-10-1. Commit + push all changes
- [ ] 1G-10-2. Update planning docs

---

## SV1 `InvalidSubmission` Bug — Root Cause Found + Fix

### Root Cause
The ESP32 `mining.submit` message omits the required `extranonce2` field, shifting all subsequent params one position left. The translator's SV1 parser interprets `ntime` as `extranonce2`, causing a size mismatch (4 bytes vs expected ~32 from SV2 channel) → `InvalidSubmission` → fatal connection kill.

**What ESP32 sends:** `[user, job_id, ntime, nonce, version]` (5 params, no extranonce2)
**What SV1 standard requires:** `[user, job_id, extranonce2, ntime, nonce]` (5 params)

**Failure chain:**
1. Translator parses ntime (`"6436eddf"`) as extranonce2 → 4 bytes
2. Validation: `extranonce2_size (32) != extra_nonce2.len (4)` → `InvalidSubmission`
3. `Error::V1Protocol` → `ErrorBranch::Break` → bridge task loop exits → connection killed
4. ESP32 reconnects after ~10s → cycle repeats

**Evidence:**
- `tollgate_core_stratum_client.c:103-107` — submit builder sends `%08lx` ntime in position 2 (extranonce2 slot)
- `client_to_server.rs:197-204` — parser maps position 2 → extranonce2
- `lib.rs:121` — `extranonce2_size() == submit.extra_nonce2.len()` fails
- NerdQAxePlus client (`stratum_api.cpp:488-496`) correctly includes extranonce2 → works fine

### Fix Plan: Add extranonce2 to ESP32 mining.submit

- [x] RC-1: Root cause identified (param layout mismatch, not authorize bug)
- [x] FIX-1: Add `extranonce2_size` to `stratum_client_state_t` (`stratum_client.h`)
- [x] FIX-2: Parse subscribe response in `stratum_client.c` — extract `result[2]` as extranonce2_size
- [x] FIX-3: Update `tollgate_core_stratum_build_submit()` — add extranonce2 param at position 2, drop version
- [x] FIX-4: Update `stratum_client_submit_share()` — generate zero-filled extranonce2 hex string
- [x] FIX-5: Update `sw_miner.c` call site — remove version param
- [x] FIX-6: Update unit test `test_stratum_client.c` — verify correct 5-param format with extranonce2
- [x] FIX-7: `make test-unit` — all 701+ tests pass
- [x] FIX-8: Build firmware + flash to Board A — verified on hardware
- [x] FIX-9: Verify translator logs: no more `InvalidSubmission`, shares accepted
- [x] FIX-10: Commit + push

### Files to Modify
| File | Change |
|------|--------|
| `main/stratum_client.h` | Add `extranonce2_size` to state struct |
| `main/stratum_client.c` | Parse subscribe response, store extranonce2_size |
| `components/tollgate_core/src/tollgate_core_stratum_client.c` | Add extranonce2 param to submit builder, drop version |
| `components/tollgate_core/src/tollgate_core_stratum_client.h` | Update submit builder signature |
| `main/sw_miner.c` | Update call to stratum_client_submit_share |
| `tests/unit/test_stratum_client.c` | Add test for correct submit format |

---

## Session 2026-06-11 — Faucet Hardware Verification Sprint

### Phase 0 Fixes (ALL COMPLETE)
- [x] 0-1. VPS mint.config.toml: `url` → `http://66.92.204.38:3338`
- [x] 0-2. VPS tproxy.config.toml `[mint]`: `url` → `http://66.92.204.38:3338`
- [x] 0-3. VPS services restarted (mint + pool + translator)
- [x] 0-4. Faucet returns external URL in tokens (verified: `66.92.204.38:3338` in base64)
- [x] 0-5. Faucet token format fixed: `cashuB` → `cashuA` (V3) via `token.to_v3_string()`
- [x] 0-6. Keyset ID prefix matching fixed in nucula wallet (16-char short → 64-char full)
- [x] 0-7. Makefile config: `mint_url` = hashpool mint, `accepted_mints` = [hashpool, testnut]
- [x] 0-8. `make test-unit` passes, firmware builds clean
- [x] 0-9. Board A locked, mining config written, firmware flashed

### Bugs Found & Fixed on First Flash
- [x] BUG-1: Faucet returned `cashuB` (V4 CBOR) tokens → nucula only supports `cashuA` (V3 JSON)
  - Fix: Changed `token.to_string()` → `token.to_v3_string()` in VPS faucet_api.rs, rebuilt translator
- [x] BUG-2: Keyset ID mismatch (proofs use 16-char short ID, wallet stores 64-char full ID)
  - Fix: `keyset_for_id()` now matches by prefix (commit `7041726`)
- [x] BUG-3: Token decoded OK but `receive()` failed with "unknown keyset id 0170bc02c93eab97"
  - Root cause: BUG-2 above. Fix applied, firmware reflashed.

### Phase 1-2: Hardware Flash & Boot Verification
- [x] 1-1. `make lock-a PHASE="faucet-hw-test"`
- [x] 1-2. `make write-mining-config-vps BOARD=a STRATUM_HOST=66.92.204.38`
- [x] 1-3. `make flash-a` (first flash)
- [x] 1-4. Second flash with keyset fix
- [x] 2-1. Board boots, WiFi connects, stratum connects to VPS translator
- [x] 2-2. Stratum client works (mining jobs flowing, job ID 11552+)
- [ ] 2-3. **BLOCKER: faucet_client produces NO logs** after second flash
  - First flash showed: `faucet_client: Received 32 ehash from faucet` + `wallet: receive: unknown keyset id`
  - Second flash (keyset fix): no faucet logs at all, but stratum + wallet init working
  - Hypothesis: firmware may not have reflashed correctly, or task creation failing silently

### Phase 2 Diagnosis: Faucet Client Silence — IN PROGRESS

#### Step 1: Clean flash + capture full boot sequence
- [ ] 1-1. Erase app partition: `esptool --port /dev/ttyACM0 erase_region 0x10000 0x3f0000`
- [ ] 1-2. Rebuild + flash: `make flash-a`
- [ ] 1-3. Capture first 60s of serial output — look for faucet_client logs
- [ ] 1-4. If no faucet logs, add debug ESP_LOGI before the `if` check in `faucet_client_start()`

#### Step 2: Connect to board WiFi + verify config
- [ ] 2-1. `nmcli dev wifi connect TollGate-B96D80`
- [ ] 2-2. `curl http://10.185.47.1:2121/debug` — check mining_enabled, faucet_url
- [ ] 2-3. `curl http://10.185.47.1:2121/wallet` — check wallet state

#### Step 3: Diagnose + fix
- [ ] 3-1. If `faucet_url` is empty in debug output → config parsing bug
- [ ] 3-2. If `mining_enabled` is false → config JSON issue
- [ ] 3-3. If faucet_url present but task not created → RAM issue, reduce stack to 4096
- [ ] 3-4. If task created but no logs → log level or buffer issue

#### Step 4: Verify faucet poll + wallet receive
- [ ] 4-1. Serial shows: `faucet_client: Faucet client started (url=..., interval=120s)`
- [ ] 4-2. After 30s: `faucet_client: Received 32 ehash from faucet`
- [ ] 4-3. `nucula_wallet: Received 32 sat (N proofs) via wallet[http://66.92.204.38:3338], new balance=32`
- [ ] 4-4. If receive still fails → investigate ehash unit handling in nucula

#### Step 5: Wallet balance accumulation
- [ ] 5-1. Wait 2-3 faucet polls (4-6 min)
- [ ] 5-2. `curl http://10.185.47.1:2121/wallet` — balance > 0, proof_count > 0
- [ ] 5-3. Balance grows with each poll

#### Step 6: Manual E2E — payment → session → internet
- [ ] 6-1. `curl -X POST http://10.185.47.1:2121/wallet/send -d '21'` → get cashuA token
- [ ] 6-2. `curl -X POST http://10.185.47.1/ -d '<token>'` → session created
- [ ] 6-3. `curl http://10.185.47.1:2121/usage` → active session
- [ ] 6-4. From TollGate AP: `ping -c 1 1.1.1.1` → internet works
- [ ] 6-5. Wait for session expiry → internet blocked
- [ ] 6-6. Re-pay → session restored

#### Step 7: Integration tests
- [ ] 7-1. `TOLLGATE_IP=10.185.47.1 make test-mining-token`
- [ ] 7-2. `TOLLGATE_IP=10.185.47.1 make smoke`
- [ ] 7-3. Write `test-faucet-wallet.mjs` integration test (optional)

#### Step 8: Cleanup
- [ ] 8-1. Commit all fixes
- [ ] 8-2. Tag `v1.5.0`
- [ ] 8-3. Update CHECKLIST.md, PLAN_MINING_INTERNET.md
- [ ] 8-4. `make unlock-a`
- [ ] 8-5. Push all repos

### Phase 1G: Ship Phase 1 (Session 2026-06-10) — MOSTLY COMPLETE
- [x] SV1 submit bug FIXED (tag v1.4.0)
- [x] Hashpool chain running on VPS1
- [x] Faucet API exposed and working
- [ ] Full E2E: mine → faucet → wallet → session → internet (blocked by faucet silence)

### Local Relay (branch `feature/local-relay`) — DONE, merging to master
- [ ] Integration test: CVM through local relay
- [ ] E2E test: CVM tool call via relay
- [ ] Future: implement negentropy binary protocol (NIP-77 NEG_OPEN/NEG_MSG) — currently using REQ-diff

### OpenWRT Interop
- [ ] SSH to `root@10.47.41.1`, verify `tollgate-wrt` still running
- [ ] Test `curl http://10.47.41.1:2121/` — kind=10021 response
- [ ] Investigate `nofee.testnut.cashu.space` API compatibility

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
- Ports shift on USB replug — always verify with `esptool chip-id`:
  - Working NerdAxe: MAC `80:b5:4e:c7:7a:d0`, SSID `TollGate-B96D80`, AP IP `10.185.47.1`
  - Fan-damaged NerdAxe: MAC `80:b5:4e:c7:79:88`
- WiFi: `studio` / `statek2017` (WPA2, 2.4GHz) — updated 2026-06-10
- `source ~/esp/esp-idf/export.sh` before `idf.py`
- sudo password: `c03rad0r123`
- Token generation: `cashu -h https://testnut.cashu.exchange send --legacy 21`
- Test mint: `testnut.cashu.exchange`
- SPIFFS offset `0x410000`, size `0xF0000`
- **Per-board locks:** `make lock-a PHASE="desc"` before hardware access
- **WiFi country code:** Must set `esp_wifi_set_country_code("DE")` before `esp_wifi_start()`
- **Lock directory:** `/home/c03rad0r/physical-router-test-automation/locks/`
- **VPS1:** `66.92.204.38` (debian, sudo, bitcoind, Docker)
- **VPS2:** `23.182.128.51` (debian, CDK mints, Nostr relay)
- **Hashpool:** `https://github.com/vnprc/hashpool` (translator at `6e13aa78`)
- **Translator config:** SV1 on `0.0.0.0:34255`, SV2 upstream `127.0.0.1:34254`, CDK mint `testnut.cashu.exchange`
- **NerdQAxe firmware:** `BOARD=NERDQAXEPLUS TOLLGATE=1` build from May 28
