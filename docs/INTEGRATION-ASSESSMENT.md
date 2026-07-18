# Integration Assessment — Balloon POW Track

Date
2026-07-18

## 1. What Works Right Now

| Item | Platform | Evidence |
|------|----------|---------|
| Mining payment core (nbits→difficulty, hashprice, share validation SHA256d, shares→allotment) | ESP32-S3 (firmware), Host (unit tests) | `tollgate_core_mining.c/h` — 25 unit test assertions, all pass |
| Stratum SV1 proxy (JSON-RPC server, job distribution, share collection, per-IP hashrate meter) | ESP32-S3 | `stratum_proxy.c` → `tollgate_core_stratum_proxy.c`, 23 unit test assertions |
| Stratum SV1 client (connects to pool, job reception, share submission) | ESP32-S3 | `stratum_client.c` → `tollgate_core_stratum_client.c`, 55 unit test assertions |
| Software miner (ESP32-S3 HW SHA256 accelerator, nonce iteration, low-priority FreeRTOS task) | ESP32-S3 | `sw_miner.c`, sha256d via mbedTLS, task started in `tollgate_main.c:311` |
| ASIC miner stub (init + detection + software fallback) | ESP32-S3 | `asic_miner.c` — stub, `asic_miner_is_present()` returns false, falls back to sw_miner |
| Remote miner (client-mode mining for mesh — fetches jobs from upstream TollGate, submits shares) | ESP32-S3 | `remote_miner.c`, 57 unit test assertions |
| Client mining mode (tollgate_client.c TG_CLIENT_MINING states, upstream proxy connection, share-for-bandwidth) | ESP32-S3 | `tollgate_client.c`, 26 unit test assertions in `test_tollgate_client_mining.c` |
| Beacon price (mining discovery tag in WiFi IE, hashprice broadcast) | ESP32-S3 | `beacon_price.c` → `tollgate_core_beacon.c` |
| Lightning payout (LNURL-pay, nucula wallet send, recipient config) | ESP32-S3 (code), Host (unit tests) | `lightning_payout.c`, `lnurl_pay.c`, 15 unit test assertions |
| Faucet client (HTTP faucet for bootstrap ecash) | ESP32-S3 (code), Host (unit tests) | `faucet_client.c`, 15 unit test assertions |
| Mining API endpoints (GET /mining/job, POST /mining/share, GET /mining/stats) | ESP32-S3 | `tollgate_api.c` + `tollgate_core_mining.h`, 23 unit test assertions in `test_mining_api.c` |
| Stratum PoW validation (block header build, nbits→target, SHA256d, target comparison) | Host (unit tests) | `test_stratum_pow.c`, 37 unit test assertions |
| Mining token integration test (live board: identity → stratum → share → token → wallet) | ESP32-S3 (live) | `tests/integration/test-mining-token.mjs` — requires `TOLLGATE_IP` env + `mining_enabled=true` |
| tollgate_core component extraction (13 modules, platform vtable pattern) | ESP32-S3 | Phase 5 verified (commit `827133a`), 801 unit tests pass |
| Config fields (mining_enabled, mining_port, mining_payout_mode, mining_sandbox_mint_access, hashprice_override) | ESP32-S3 | `config.c/h`, config.json schema documented in AGENTS.md |
| Main boot integration (mining tasks lifecycle: init → start → tick) | ESP32-S3 | `tollgate_main.c:269-317`, conditional on `mining_enabled` |

## 2. What Exists But Is Untested

| Item | Status | Notes |
|------|--------|-------|
| ASIC detection (BM1366/BM1368 SPI probe) | Stub only — `asic_miner_init()` always returns `!present` | No real SPI driver. `asic_miner_is_present()` hardcoded false. Never tested on real ASIC hardware. |
| SV1 stratum client against live pool | Code compiles, unit tests pass on parsing/hex | No integration test connecting to real Braiins/Slush Pool. `stratum_client_start()` called in main but never exercised end-to-end. |
| Mining token integration on live board | Test script exists but requires hardware + `mining_enabled=true` | `test-mining-token.mjs` — skips gracefully if board unreachable or mining disabled, but no evidence of full green run. |
| Lightning payout end-to-end | Code compiles, unit tests cover init/config | No integration test: LNURL-pay fetch + nucula wallet send + recipient confirmation never exercised on live board. |
| Faucet client against live faucet | Code compiles, unit tests cover HTTP parsing | No integration test against actual faucet endpoint. |
| Remote miner against upstream TollGate | Code compiles, unit tests cover PoW + share building | No two-board test: remote miner fetching job from upstream TollGate and submitting shares. |
| Firewall sandbox for mining ports | Code in `tollgate_core_firewall.c` (`set_sandbox_ports`, `set_sandbox_mint_access`) | Called in `tollgate_main.c:269-271` but no integration test verifying unauthenticated client can reach stratum port + mint URLs only. |
| Hashprice from live block template | `tollgate_core_mining_calc_hashprice(nbits)` unit tested with known nbits | Never fed real `nbits` from a live SV1 `mining.notify` — only tested with static values. |

## 3. What Does NOT Exist Yet

1. **BitAxe git submodule** — planned in MINING_PLAN.md Phase 1, never added. No `components/bitaxe/` or `.gitmodules` entry for ESP-Miner.

2. **SV2 (Stratum v2) upstream** — MINING_PLAN.md describes SV2 upstream with Noise handshake. Only SV1 implemented. `stratum_client.c` uses raw TCP + JSON-RPC, no Noise encryption, no SV2 binary framing.

3. **Hashpool / CDK mint translator integration** — PLAN_MINING_INTERNET.md describes 12-step flow: BitAxe → TollGate SV1 proxy → Hashpool translator → CDK mint → ecash token → TollGate wallet. Steps 3-8 (translator + mint) are external infrastructure, never integrated. No code for `mining.token` SV1 method handling.

4. **Portal UI "Mine for Access" tab** — planned in Phase 10. No HTML/JS in captive portal for mining tab, hashrate display, or earnings counter. No Playwright E2E test (`mining.spec.mjs`).

5. **CVM MCP tools for mining** — planned in Phase 11: `get_hashprice`, `set_mining_config`. Not in `mcp_handler.c` (only 10 existing tools: get_config, set_config, get_balance, wallet_send, get_sessions, get_usage, set_payout, set_metric, set_price, wallet_melt).

6. **BM1366/BM1368 SPI driver** — `asic_miner.c` is a stub. No SPI init, register config, job submission, nonce readback. No board-specific pin mapping.

7. **Hashrate-to-bandwidth dynamic pricing** — `tollgate_core_mining_shares_to_allotment_ms/bytes` exist and are unit tested, but never fed live hashrate data from a running miner. The conversion formula is static; no feedback loop adjusting price based on actual network hashprice.

8. **Dual payout mode auto-detection** — config field `mining_payout_mode` exists (auto/pool/upstream/proxy_only), but `auto` mode logic (detect upstream TollGate → switch to upstream/proxy) is not implemented in `stratum_client.c` or `tollgate_main.c`.

9. **Sandbox mint URL access for unauthenticated miners** — `tollgate_core_fw_set_sandbox_mint_access()` called in main but the firewall rules for whitelisting mint URLs for unauthenticated clients are not verified. No test confirms a miner can reach `mint_url` without paying first.

10. **E2E mining test** — `mining.spec.mjs` planned in Phase 13, not created. No Playwright test for portal mining tab interaction.

## 4. Blockers for ESP32-C3 Port

(4MB flash, 400KB RAM, single-core, no PSRAM)

| Component | Limit | Current Usage (ESP32-S3) | Status |
|-----------|-------|-------------------------|--------|
| Flash (4MB vs 16MB) | 4MB total, ~3MB app partition | TollGate firmware on ESP32-S3 uses 16MB flash. Mining stack (stratum client + proxy + sw_miner + tollgate_core_mining + crypto) adds ~200-400KB to binary. On 4MB: partition table must shrink. NVS + SPIFFS + LittleFS + app must all fit. | TIGHT — needs custom partitions.csv. Current `partitions.csv` targets 16MB. |
| RAM (400KB vs 512KB+) | ~400KB total, ~100-150KB heap after WiFi stack | Stratum client uses TLS (mbedTLS ~30-50KB heap). Stratum proxy needs per-client connection buffers. sw_miner task stack 4KB. ASIC SPI driver would need DMA buffers. | TIGHT — mbedTLS TLS for SV2 upstream would be worst-case. SV1 (raw TCP) is lighter. No measurement done on ESP32-C3. |
| SHA256 hardware accelerator | ESP32-C3 has NO SHA256 DMA (ESP32-S3 does) | `sw_miner.c` uses mbedTLS software SHA256 (works on both). ESP32-S3 HW accelerator not actually used in current code — already software. | OK for function but SLOW: ESP32-C3 at 160MHz ≈ 1-5 kH/s software SHA256d. Usable for demo, not for profit. |
| Single-core | No parallelism | FreeRTOS tasks: WiFi AP/STA + DNS + captive portal + API + local relay + stratum proxy + sw_miner + remote_miner. All time-sliced on one core. | OK — FreeRTOS handles it, but mining task will steal CPU from WiFi/network. Must use lowest priority. |
| No PSRAM | All state in internal RAM | Stratum job structs (~80 bytes each), mining client stats (10 clients × ~48 bytes), TLS buffers (~16KB). All fit in internal RAM. | OK — no large buffers needed for SV1. SV2 Noise handshake buffers would be tight. |
| SPI for ASIC | ESP32-C3 has 2 SPI peripherals (VSPI, HSPI) | BM1366 needs SPI. ESP32-C3 SPI is sufficient for single ASIC. | OK — but no BitAxe/NerdAxe board targets ESP32-C3. All ASIC boards are ESP32-S3. Hardware mismatch. |
| TLS for SV2 upstream | mbedTLS on ESP32-C3 is possible but memory-constrained | Current SV1 client uses raw TCP (no TLS). SV2 needs Noise handshake (crypto-heavy). | BLOCKED for SV2 — SV1 path works without TLS. SV2 deferred until memory budget proven. |
| BitAxe submodule | BitAxe ESP-Miner targets ESP32-S3 | No ESP32-C3 build target in BitAxe/ESP-Miner. Porting BitAxe to C3 = significant effort (different SoC, no USB-OTG, less flash). | BLOCKED — BitAxe does not support ESP32-C3. ASIC mining is ESP32-S3 only. |

**Key blocker:** ASIC mining (BitAxe/NerdAxe) is ESP32-S3 only — no C3 port exists or is practical. ESP32-C3 can only do software mining (1-5 kH/s) or act as stratum proxy/relay. SV2 upstream is blocked by TLS memory cost on C3.

## 5. Estimated Effort

| Work Item | Effort | Confidence |
|-----------|--------|------------|
| Integration test: SV1 stratum client against Slush Pool (live) | 1-2 days | HIGH — code works, just needs live pool + board |
| Integration test: mining token flow on live board | 1 day | HIGH — script exists, needs board + mining_enabled |
| Integration test: firewall sandbox for mining ports | 1 day | HIGH — curl-based, needs board |
| Integration test: two-board remote miner (client → upstream) | 2-3 days | MEDIUM — needs two boards, both with mining enabled |
| Portal UI "Mine for Access" tab + hashrate display | 2-3 days | HIGH — HTML/CSS/JS in captive portal, pattern established |
| Playwright E2E: mining.spec.mjs | 1 day | HIGH — follows existing pattern |
| CVM MCP tools: get_hashprice, set_mining_config | 4-8 hours | HIGH — pattern established, 10 tools already exist |
| BitAxe git submodule + build integration | 2-3 days | MEDIUM — submodule add is easy, CMakeLists integration + build verification is the work |
| BM1366 SPI driver (asic_miner.c real impl) | 1-2 weeks | LOW — SPI register sequence for BM1366 is documented in BitAxe source, but real hardware debugging always finds issues |
| SV2 upstream (Noise handshake + binary framing) | 1-2 weeks | LOW — Noise_XX handshake in C on ESP32, new protocol, no existing C impl in repo |
| Hashpool translator integration (external service) | 1-2 weeks | LOW — depends on external service availability, untested protocol path |
| Dual payout mode auto-detection | 1-2 days | MEDIUM — detect upstream TollGate via beacon, switch mode |
| Port mining stack to ESP32-C3 (partitions, memory budget) | 3-5 days | LOW — no ASIC (C3), SV1-only, needs custom partitions.csv + memory profiling |
| BitAxe port to ESP32-C3 | 1-2 weeks+ | VERY LOW — no upstream support, different SoC, no USB-OTG, major effort, questionable value |

## 6. Dependencies on Other Tracks

| Dependency | Track | What We Need | Status |
|-----------|-------|-------------|--------|
| WiFi AP/STA + captive portal + firewall | TollGate core (this repo) | Working AP, STA, DNS hijack, NAT, firewall per-client grant | DONE — all in main/, working on ESP32-S3 |
| Cashu wallet (nucula) | TollGate core (this repo) | Token decode, checkstate, wallet_receive, wallet_send | DONE — `cashu.c` + `nucula_lib/` |
| Nostr identity derivation | TollGate core (this repo) | nsec → npub → locking_pubkey for SV2 | DONE — `identity.c` |
| Local Nostr relay | TollGate core (this repo) | wisp-esp32 on port 4869 | DONE — `local_relay.c` |
| Beacon price IE | TollGate core (this repo) | WiFi beacon with price + mint hash | DONE — `beacon_price.c` |
| Mesh connectivity (FIPS) | balloon-fips | FIPS transport for mesh mining (relay mode, proxy mode) | NOT STARTED — FIPS mesh routing not implemented. POW track depends on FIPS for mesh mining scenario. |
| Radio link (LR2021/ESP-NOW) | balloon-fips / balloon-range | Physical layer for mesh mining communication | PARTIAL — range track proved 1377 kbps FLRC. FIPS track has transport stubs but no real radio. |
| Hashpool/CDK mint external service | External (not in any track) | SV1→SV2 translator + CDK mint for ecash token minting | DOES NOT EXIST — planned in PLAN_MINING_INTERNET.md but external infrastructure. No track owns this. |

## 7. Shared Resources Needed

| Resource | Who Provides | When Needed | Notes |
|----------|-------------|-------------|-------|
| ESP32-S3 board (Board A: /dev/ttyACM0, MAC 94:a9:90:2e:37:7c) | HW lab | Integration testing | Primary test target. Ports change on replug. |
| ESP32-S3 board (Board B: /dev/ttyACM1) | HW lab | Two-board remote miner test | Secondary board for client→upstream test. |
| BitAxe/NerdAxe hardware | Not in lab | ASIC integration | No ASIC mining hardware currently available. NerdAxe Ultra 500GH/s listed as "available for testing" in MINER_INTEGRATION_PLAN.md but not confirmed in lab. |
| Slush Pool / Braiins Pool account | External | SV1 client integration test | Need pool URL + credentials for live stratum test. |
| Cashu test mint | `testnut-nutshell.mints.orangesync.tech` | Mining token flow test | Already configured in config.json default. |
| Hashpool translator service | External (does not exist) | Hashpool integration | Major external dependency — no implementation started. |

## 8. Integration Checklist

1. [x] Mining payment core (tollgate_core_mining.c) — nbits→difficulty, hashprice, share validation, allotment conversion
2. [x] Stratum SV1 proxy server (stratum_proxy.c) — job distribution, share collection, per-IP hashrate
3. [x] Stratum SV1 client (stratum_client.c) — pool connection, job reception, share submission
4. [x] Software miner (sw_miner.c) — SHA256d nonce iteration, low-priority task
5. [x] ASIC miner stub (asic_miner.c) — detection + software fallback
6. [x] Remote miner (remote_miner.c) — client-mode mesh mining
7. [x] Client mining mode (tollgate_client.c) — TG_CLIENT_MINING states, upstream proxy
8. [x] Beacon price (beacon_price.c) — mining discovery IE
9. [x] Lightning payout (lightning_payout.c) — LNURL-pay + nucula send
10. [x] Faucet client (faucet_client.c) — bootstrap ecash
11. [x] Mining API endpoints — GET /mining/job, POST /mining/share, GET /mining/stats
12. [x] Config fields — mining_enabled, mining_port, mining_payout_mode, sandbox_mint_access
13. [x] Boot integration — mining tasks lifecycle in tollgate_main.c
14. [x] tollgate_core component extraction — 13 modules, platform vtable
15. [x] Unit tests — 9 test files, 276 assertions, all pass
16. [x] Integration test script — test-mining-token.mjs (exists, needs live run)
17. [ ] BitAxe git submodule — add + build integration
18. [ ] BM1366 SPI driver — real ASIC implementation
19. [ ] SV2 upstream — Noise handshake + binary framing
20. [ ] Hashpool translator integration — external service
21. [ ] Portal UI "Mine for Access" tab
22. [ ] Playwright E2E — mining.spec.mjs
23. [ ] CVM MCP tools — get_hashprice, set_mining_config
24. [ ] Dual payout mode auto-detection
25. [ ] Integration test: SV1 client against live pool (green run)
26. [ ] Integration test: mining token flow on live board (green run)
27. [ ] Integration test: two-board remote miner
28. [ ] Integration test: firewall sandbox verification
29. [ ] ESP32-C3 port: custom partitions.csv + memory profiling
30. [ ] ESP32-C3 port: build verification (SV1-only, no ASIC)

## 9. Key Risks

1. **ASIC mining is ESP32-S3 only.** No BitAxe/NerdAxe board supports ESP32-C3. The "mine for bandwidth" value proposition on C3 is limited to software mining (1-5 kH/s) — enough for demo, not for meaningful earnings. This may make POW track irrelevant on C3 hardware.

2. **SV2 upstream blocked by TLS memory.** mbedTLS Noise handshake on ESP32-C3 (400KB RAM) is unproven. SV1 path works but lacks encryption (hash hijacking risk) and bandwidth efficiency. If SV2 is required for production, C3 may not support it.

3. **Hashpool external dependency.** The 12-step ecash-token-from-mining flow depends on an external translator + CDK mint service that does not exist yet. No track owns this. POW track cannot complete the full "mine → earn ecash → spend on bandwidth" loop without it. Current implementation can only do: mine → validate share locally → grant session (offline mode).

4. **No real ASIC hardware in lab.** `asic_miner.c` is a stub. Even if BM1366 SPI driver is written, no hardware to test against. NerdAxe Ultra listed as "available" but not confirmed present.

5. **Mining steals CPU from WiFi.** On single-core ESP32-C3, the sw_miner task competes with WiFi AP/STA, DNS, captive portal, and API for CPU time. Even at lowest priority, SHA256d computation is CPU-intensive. Could cause WiFi disconnections or API timeouts under load.

6. **Stratum proxy port exposure.** Mining port (default 4033 or configurable) must be open to unauthenticated clients. Firewall sandbox must prevent miners from reaching non-mint URLs. If sandbox is misconfigured, miners get free internet without valid shares.

7. **Hashprice volatility.** Hashprice derived from `nbits` changes every block (~10 min). A miner earning bandwidth at current hashprice may find earnings drop when difficulty adjusts. No mechanism to alert miner or renegotiate mid-session.

## 10. Questions for the Coordinator

1. **Is ESP32-C3 a target for POW at all?** Given no ASIC support and ~1-5 kH/s software mining, is the C3 use case limited to stratum proxy/relay mode (no local mining)? This affects which features need C3 porting.

2. **Who owns the Hashpool translator service?** PLAN_MINING_INTERNET.md describes it as external infrastructure. No balloon track covers it. Is it out of scope for balloon integration? If so, POW track's "mine → ecash" loop is limited to local share validation only.

3. **Is FIPS mesh mining a dependency or stretch goal?** POW track can operate standalone (SV1 proxy + local miners). Mesh mining (relay/proxy mode via FIPS transport) depends on FIPS track completing radio + routing. Should POW track assume FIPS will be ready, or design for standalone-only?

4. **What ASIC hardware is actually in the lab?** MINER_INTEGRATION_PLAN.md lists "NerdAxe Ultra 500GH/s — available for testing" but this is not confirmed. Is there any BM1366/BM1368 hardware available for real SPI driver testing?

5. **SV2 priority?** SV1 works (code compiles, unit tests pass). SV2 adds encryption + efficiency but costs 1-2 weeks + TLS memory. Is SV2 needed for balloon integration, or is SV1 sufficient for the mesh mining demo?

6. **Cross-track integration point:** POW track needs a working TollGate AP+STA+firewall+Cashu wallet as baseline. All of these exist in this repo and work on ESP32-S3. The integration question is: does the coordinator want POW to test against the balloon-range or balloon-fips radio link, or is WiFi (existing AP/STA) the only transport for POW?