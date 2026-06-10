# Mining-for-Internet via Hashpool Ecash — Implementation Plan

**Created:** 2026-05-29
**Updated:** 2026-06-10
**Status:** Phase 1G — Ship Phase 1: Full Mining Chain Deployment
**Branch:** `feature/tollgate-core-v2` (esp32-tollgate)

---

## Architecture Overview

```
BitAxe Miner          ESP32 TollGate                    Hashpool Translator          Hashpool Pool + CDK Mint
    |                        |                                |                              |
    |--1. WiFi connect ---->| AP (sandbox mode)               |                              |
    |                        |                                |                              |
    |--2. SV1 stratum ------>| SV1 Proxy (port 4033)           |                              |
    |   subscribe/auth       | - Full SV1 handshake            |                              |
    |   mining.submit        | - Local PoW validation          |                              |
    |                        | - Per-client hashrate tracking   |                              |
    |                        |                                |                              |
    |                        |--3. SV1 upstream -------------->| Translator (SV1->SV2)          |                              |
    |                        |   authorize password includes   | Extracts TollGate's           |
    |                        |   TollGate's locking_pubkey     | locking_pubkey, registers     |
    |                        |                                | pubkey→channel mapping        |
    |                        |                                |--4. SV2 SubmitSharesExtended->|
    |                        |                                |                              |
    |                        |                                |<--5. Pool mints ehash quote ---|
    |                        |                                |    for TollGate's pubkey      |
    |                        |                                |                              |
    |                        |                                |--6. Proof sweeper mints ------|
    |                        |                                |    tokens for pubkey          |
    |                        |                                |    (mint_tokens_for_pubkey)   |
    |                        |                                |                              |
    |                        |                                |--7. Generate cashuA... token->|
    |                        |                                |    lookup pubkey in registry  |
    |                        |                                |    send via mining.token      |
    |                        |<--8. mining.token notification--|                              |
    |                        |    {"method":"mining.token",    |                              |
    |                        |     "params":["cashuA..."]}     |                              |
    |                        |                                |                              |
    |                        |--9. Decode token + store in nucula wallet                   |
    |                        |    (existing cashu_decode_token + wallet_receive)           |
    |                        |                                                              |
    |                        |   10. Miner submits ecash token to TollGate (existing        |
    |                        |      Cashu payment endpoint, same as manual payment)        |
    |                        |   11. Session created, firewall grants NAT                   |
    |                        |                                                              |
    |<--12. Internet ---------| NAT open                                                     |
```

### SV2 Future Path (Phase 2)

```
BitAxe Miner          ESP32 TollGate (SV1 downstream, SV2 upstream)     Hashpool Pool + CDK Mint
    |                        |                                              |
    |-- SV1 stratum -------->| SV1 Proxy (downstream miners)                |
    |                        |                                              |
    |                        |-- SV2 direct (Noise NX encrypted) ---------->|
    |                        |   SubmitSharesExtended with locking_pubkey   |
    |                        |                                              |
    |                        |<-- mining.token (or NUT-XX direct query) ----|
    |                        |<-- Ecash tokens stored in nucula wallet -----|
```

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Stratum protocol on ESP32 (Phase 1) | SV1 only | SV2 requires Noise NX + ElligatorSwift (18-25 days crypto porting) |
| Stratum protocol on ESP32 (Phase 2) | SV2 direct | Cleaner architecture, bypasses translator, gets own mint quotes directly |
| Hashpool connection (Phase 1) | Via translator (SV1->SV2) | TollGate speaks SV1, translator converts to SV2 |
| Per-TollGate mint identity | Pubkey passthrough in translator | TollGate sends locking_pubkey in authorize password |
| Locking key generation | Derive from nsec via HMAC-SHA512 | Deterministic, tied to device identity, secp256k1 available |
| Session credit model | Explicit token submission | Miner submits ecash tokens via existing Cashu payment flow |
| Share validation | Local PoW check before forwarding | Avoids wasting bandwidth on invalid shares |
| SV2 feasibility on ESP32-S3 | Yes, resources sufficient | ~60-75 KB flash, ~8-10 KB RAM needed; ESP32-S3 has 2 MB free flash, 8 MB PSRAM |

---

## Phase 1: SV1 MVP (Mining-for-Internet via Translator)

### 1A: SV1 Stratum Proxy — COMPLETE

**Goal:** SV1 stratum server so miners can connect, subscribe, authorize, and submit shares with local PoW validation.

**Commit:** `3e32ebe` (self-test + diagnostics), `3d41ef1` (PoW fix), `9038874` (full rewrite)

**Verified on hardware:** Laptop connects to ESP32 AP (`10.192.45.1:4033`), full stratum handshake succeeds (subscribe + authorize).

- [x] 1A-1. SV1 message parser: `mining.subscribe`, `mining.authorize`, `mining.submit`, `mining.extranonce.subscribe`, `mining.suggest_difficulty`
- [x] 1A-2. `mining.subscribe` response with subscription ID + extranonce
- [x] 1A-3. `mining.authorize` response with success
- [x] 1A-4. `mining.submit` parsing: job_id, ntime, nonce, version
- [x] 1A-5. PoW validation: double-SHA256, byte reversal, target generation from difficulty
- [x] 1A-6. Share callback mechanism for upstream forwarding
- [x] 1A-7. Job broadcast (`mining.notify`) and difficulty broadcast to authorized miners
- [x] 1A-8. Per-client IP tracking via `tollgate_core_mining_update_hashrate()`
- [x] 1A-9. Miner disconnect handling, mutex-protected miner array
- [x] 1A-10. Unit tests: 35 assertions using Bitcoin genesis block as known vector
- [x] 1A-11. Loopback self-test at boot confirms TCP stack works
- [x] 1A-12. End-to-end verified: AP client connects, subscribe + authorize both succeed
- [x] 1A-13. TCP "listen failure" resolved — was not a bug; earlier failures were from older firmware
- [ ] 1A-14. Integration test with real BitAxe/NerdQAxe miner
- [x] 1A-15. Make self-test conditional on config flag (commit `4b8d89f`)

---

### 1B: Upstream Connection + Locking Pubkey — COMPLETE

**Commit:** `c07a7c0`

**Verified on hardware:** `GET /mining/pubkey` returns `{"locking_pubkey":"03703b0d..."}` on Board B (fan-damaged NerdAxe at `10.192.45.1`).

- [x] 1B-1. Implement `identity_derive_locking_key()` in `identity.c`:
  - HMAC-SHA512(nsec_bytes, "tollgate-cashu-locking-key") using mbedtls
  - First 32 bytes → secp256k1 scalar (verify < curve order)
  - Compute compressed pubkey (33 bytes)
  - Store in static, return on demand
- [x] 1B-2. Add `identity_get_locking_pubkey_hex()` — returns 66-char hex string
- [x] 1B-3. Add `GET /mining/pubkey` endpoint in `tollgate_api.c`
- [x] 1B-4. Modify `stratum_client send_authorize()` to include `.locking_pubkey_hex` in password
- [x] 1B-5. Unit test: known nsec → expected HMAC output → expected pubkey (6 golden vectors)
- [x] 1B-6. Verify on hardware: check `/mining/pubkey` returns correct hex

---

### 1C: Ecash Token Delivery via SV1 Notification (Option B3)

**Goal:** The hashpool translator mints ehash tokens for the TollGate's locking pubkey and pushes them downstream via a custom `mining.token` SV1 JSON-RPC notification. The ESP32 receives the `cashuA...` token, decodes it, and stores proofs in the nucula wallet.

**Why B3 instead of A (poll CDK mint directly):**
- No CDK fork needed — translator already mints tokens via `mint_tokens_for_pubkey()`
- No blinded minting on ESP32 — saves ~8KB RAM and avoids complex crypto
- Push model (near-instant) vs poll model (60s latency)
- Uses existing TCP connection, no new HTTP endpoints
- Standard `cashuA...` v3 tokens — existing `cashu_decode_token()` works

**Architecture change in translator:**
```
Current:
  accept_connections() → new_downstream() → 3 tasks per connection
  proof_sweeper() → mints tokens → generate_single_ehash_token() → logs token (unused)

New:
  accept_connections() → new_downstream(tx_token) → 4 tasks per connection
  TranslatorSv2 maintains: HashMap<PublicKey, Sender<String>> (downstream registry)
  proof_sweeper() → mints tokens → looks up pubkey in registry → sends token via channel
  new token_sender_task per downstream → writes mining.token to TCP socket
```

**Translator files to modify:**
- `roles/translator/src/lib/downstream_sv1/downstream.rs` — add `locking_pubkey` to `Downstream`, add `tx_token: Sender<String>`, add token sender task
- `roles/translator/src/lib/downstream_sv1/mod.rs` — add token channel to `SubmitShareWithChannelId` or new struct
- `roles/translator/src/lib/mod.rs` — add downstream registry (`HashMap<PublicKey, Sender<String>>`), route tokens from proof sweeper, modify `spawn_proof_sweeper()` and `generate_single_ehash_token()`
- `roles/translator/src/lib/proxy/bridge.rs` — parse locking pubkey from authorize password, register in downstream map

**ESP32 files to modify:**
- `components/tollgate_core/src/tollgate_core_stratum_client.c` — handle `mining.token` notification from upstream
- `main/tollgate_main.c` — wire token callback to nucula wallet receive

**Test files to create:**
- `tests/unit/test_stratum_token.c` — unit test for token notification parsing

**Tasks:**
- [x] 1C-1. Add `locking_pubkey: Option<String>` to `Downstream` struct in `downstream.rs`
- [x] 1C-2. Add `tx_token: Sender<String>` to `Downstream` for per-connection token delivery
- [x] 1C-3. Add downstream registry: `HashMap<PublicKey, Sender<String>>` in `TranslatorSv2`
- [x] 1C-4. Parse locking pubkey from `mining.authorize` password in `handle_submit` or bridge
- [x] 1C-5. Register downstream pubkey→channel mapping after successful authorize
- [x] 1C-6. Spawn `token_sender_task` per downstream that listens on `rx_token` and writes `mining.token` JSON-RPC notification
- [x] 1C-7. Modify `generate_single_ehash_token()` to route token to correct downstream via registry instead of just logging
- [x] 1C-8. ESP32: handle `mining.token` notification in `stratum_client.c` upstream reader
- [x] 1C-9. ESP32: decode `cashuA...` token via existing `cashu_decode_token()` and call `nucula_wallet_receive()`
- [x] 1C-10. Unit test: token notification JSON parsing on ESP32
- [ ] 1C-11. Integration test: translator mints → sends token → ESP32 receives and stores

**Effort:** 5-7 days

### 1C-HW: Memory Optimization + Mining Integration Test

**Problem:** `stratum_cli` (8192) and `sw_miner` (8192) tasks fail to create at boot due to ~28KB free internal RAM. Other services consume ~106KB of stack before mining tasks are reached.

**Solution:** Individual boolean flags in config.json to control which services start. In mining mode, disable display, CVM, sync, wifistr, local relay — saving ~90KB of stack.

**New config flags:**
| Flag | Default | Controls | Stack Saved |
|------|---------|----------|-------------|
| `sync_enabled` | `true` | sync_manager task | 16384 |
| `wifistr_enabled` | `true` | wifistr_init task + periodic timer | 16384 |
| `local_relay_enabled` | `true` | local Nostr relay (ws_server + cleanup) | 16384 |
| `mint_health_enabled` | `true` | mint health monitoring + wallet queue | 16384 |

*(Existing: `display_enabled`, `cvm_enabled`, `mining_enabled`)*

**relay_selector** is bundled with sync/wifistr — only initialized when either is enabled.

**Mining test config.json:**
```json
{
  "display_enabled": false,
  "cvm_enabled": false,
  "sync_enabled": false,
  "wifistr_enabled": false,
  "local_relay_enabled": false,
  "mint_health_enabled": true,
  "mining": {
    "enabled": true,
    "payout_mode": "auto",
    "stratum_host": "<auto-detected-host-ip>",
    "stratum_port": 34255,
    "stratum_user": "tollgate_test",
    "stratum_pass": "x",
    "mining_port": 3334
  }
}
```

**Key dependency:** `mint_health` creates the wallet queue (`tls_worker_set_queue()`) used by `stratum_client`'s `mining.token` handler via `tls_worker_submit()`. Must stay enabled in mining profile so token receive runs async in 16384-stack task, not sync in 8192-stack stratum_client.

**Task stack budget (mining profile):**
| Task | Stack | Running |
|------|-------|---------|
| ~~display~~ | ~~24576~~ | Disabled |
| ~~cvm_relay~~ | ~~16384~~ | Disabled |
| ~~sync_mgr~~ | ~~16384~~ | Disabled |
| ~~wifistr_init~~ | ~~16384~~ | Disabled |
| ~~ws_server (relay)~~ | ~~12288~~ | Disabled |
| ~~relay_cleanup~~ | ~~4096~~ | Disabled |
| mint_health | 16384 | Running |
| stratum_cli | 6144 | Running |
| sw_miner | 6144 | Running |
| stratum_proxy | 6144 | Running |
| httpd (API) | 16384 | Running |
| httpd (portal) | 4096 | Running |
| dns_server | 4096 | Running |
| dot_reject | 3072 | Running |
| **Total** | **58048** | |

**Tasks:**
- [x] 1C-HW-1. Add `sync_enabled`, `wifistr_enabled`, `local_relay_enabled`, `mint_health_enabled` to `config.h`
- [x] 1C-HW-2. Add defaults (all `true`) + JSON parsing in `config.c`
- [x] 1C-HW-3. Conditional task creation in `tollgate_main.c` (`app_main`, `start_services`, `stop_services`)
- [x] 1C-HW-4. Bundle `relay_selector` with sync/wifistr — skip when both disabled
- [x] 1C-HW-5. Unit tests pass (`make test-unit`)
- [x] 1C-HW-6. Build firmware, write mining config to SPIFFS, flash to working NerdAxe
- [x] 1C-HW-7. Verify serial: "Stratum client started" + "Software miner started"
- [x] 1C-HW-8. Write `tests/integration/test-mining-token.mjs` (SV1 handshake + wallet verify)
- [x] 1C-HW-9. Add Makefile targets: `write-mining-config`, `test-mining-token`
- [x] 1C-HW-10. Run integration test (7/8 pass; token delivery blocked by test mint TLS)
- [x] 1C-HW-11. Commit + push (3 commits: `6838629`, `1a9e69d`, `292213d`)

**Commits:** `6838629` (service flags), `1a9e69d` (stack reduction), `292213d` (cashu CLI fix)

**Hardware verified on working NerdAxe (MAC `80:b5:4e:c7:7a:d0`):**
- All 3 mining tasks created: stratum_cli, sw_miner, stratum_proxy
- Stratum proxy self-test passed (loopback on port 3334)
- SV1 handshake test passed: subscribe + authorize both succeed
- `free_internal: 17935` after all tasks running
- Board IP: `10.185.47.1` (SSID: `TollGate-B96D80`, nsec: `9af47906...`)

---

### 1C-ROADMAP: Future Migration to NUT-XX (Option A — Direct Mint Polling)

**When NUT-XX ("Mint Quote Lookup by Public Key") merges into CDK upstream:**

The NUT spec (`cashubtc/nuts#341`) is now OPEN and actively developed. callebtc himself opened it after initially concept-NACKing the earlier attempt. Key developments:
- **NUT spec:** `cashubtc/nuts#341` — "NUT-XX: Get quotes by pubkeys", requires Schnorr signatures, domain-separated with timestamps
- **CDK mint-side PR:** `cashubtc/cdk#1834` — route `v1/mint/quote/{method}/pubkey`, DB migration, thesimplekid reviewing
- **CDK wallet-side PR:** `cashubtc/cdk#1932` — HTTP client, wallet sign function, cdk-cli command, lorenzolfm approved
- **CDK custom router:** `cashubtc/cdk#1251` — MERGED into v0.15.0 (Jan 2026), mintd accepts custom Axum routers
- **Signature scheme:** `cashubtc/nuts#363` — domain-separated signatures with timestamps to prevent replay

**Migration path (when ready):**
1. Hashpool mint upgrades to CDK with NUT-XX support
2. ESP32 calls `POST /v1/mint/quote/{method}/pubkey` with Schnorr signature
3. ESP32 receives quote IDs, then calls standard `POST /v1/mint/bolt11` for blinded minting
4. Remove `mining.token` notification path from translator
5. ESP32 no longer depends on translator for token delivery

**Community engagement plan:**
- [x] Post supportive comment on `cashubtc/nuts#341` explaining ESP32 use case — POSTED 2026-06-03
- [x] Post supportive comment on `cashubtc/cdk#1834` offering to test — POSTED 2026-06-03
- [ ] Acknowledge vnprc's earlier rejected PRs were correct in principle
- [ ] Thank thesimplekid for the custom router suggestion (now merged)
- [ ] Note that B3 is our temporary workaround, not urgent

---

### 1D: Hashpool Translator Pubkey Passthrough (server-side Rust)

**Goal:** The hashpool translator routes each downstream's locking pubkey through `SubmitSharesExtended` so the pool can mint ehash tokens per-TollGate.

**Problem:** `bridge.rs:translate_submit()` uses `self.locking_pubkey` (the translator's configured key) for ALL downstreams. The pool sees every share as coming from the same pubkey.

**Solution:** Route the downstream's locking pubkey through the share pipeline: `Downstream.handle_authorize()` → `locking_pubkey` field → `SubmitShareWithChannelId` → `Bridge.translate_submit()`.

**Prerequisite (done, committed hashpool `6e13aa78`):**
- [x] 1C-translator-1. Add `locking_pubkey: RefCell<Option<String>>` to `Downstream` struct
- [x] 1C-translator-2. Extract pubkey from `mining.authorize` password in `handle_authorize()`
- [x] 1C-translator-3. Add `DownstreamMap` registry in `TranslatorSv2` + register after authorize
- [x] 1C-translator-4. Add `send_token_notification()` method for pushing `mining.token`
- [x] 1C-translator-5. Route minted tokens from proof sweeper to registered downstreams
- [x] 1C-translator-6. Fix E0521 lifetime error (clone `downstream_registry` before `task::spawn`)
- [x] 1C-translator-7. Fix `DownstreamMap` visibility (`pub type` + re-export in `main.rs`)
- [x] 1C-translator-8. Add `[mint]` section to `tproxy.config.toml`

**Phase 1D tasks (pubkey passthrough in share pipeline) — COMPLETE:**
- [x] 1D-1. Add `locking_pubkey: Option<String>` to `SubmitShareWithChannelId` in `downstream_sv1/mod.rs`
- [x] 1D-2. Populate `locking_pubkey` in `Downstream.handle_submit()` from `self.locking_pubkey.borrow()`
- [x] 1D-3. Use per-share pubkey in `Bridge.translate_submit()`, fallback to `self.locking_pubkey`
- [x] 1D-4. `cargo check` passes
- [x] 1D-5. Commit translator changes (hashpool `6e13aa78`)

**Files modified (in ehash-setup/hashpool):**
- `roles/translator/src/lib/downstream_sv1/mod.rs` — add field to struct
- `roles/translator/src/lib/downstream_sv1/downstream.rs` — populate field in `handle_submit()`
- `roles/translator/src/lib/proxy/bridge.rs` — use per-share pubkey in `translate_submit()`
- `roles/translator/src/lib/mod.rs` — downstream registry, token routing, E0521 fix
- `roles/translator/src/main.rs` — re-export `DownstreamMap`
- `config/tproxy.config.toml` — `[mint]` section

**Effort:** 1-2 hours

---

### 1E: Miner Auto-Discovery and Configuration

**Goal:** BitAxe/NerdQAxe automatically discovers TollGate APs and configures stratum.

**Files to modify (in esp-miner-nerdqaxeplus):**
- `main/main.cpp`
- Stratum manager or equivalent

**Tasks:**
- [ ] 1E-1. Scan WiFi for `TollGate-*` SSIDs on boot
- [ ] 1E-2. Connect to strongest TollGate AP
- [ ] 1E-3. Read beacon/price info from `GET /` endpoint (kind 10021)
- [ ] 1E-4. Reconfigure stratum to point at `<AP_IP>:<mining_port>`
- [ ] 1E-5. Start mining through TollGate

**Effort:** 2-3 days

---

### 1F: Integration Testing

**Tasks:**
- [ ] 1F-1. Full E2E: Board B as TollGate, NerdAxe as miner, hashpool on VPS
- [ ] 1F-2. Verify shares flow: miner -> TollGate -> translator -> pool -> mint
- [ ] 1F-3. Verify ecash tokens arrive in TollGate wallet
- [ ] 1F-4. Verify miner submits token, gets internet
- [ ] 1F-5. Update documentation (AGENTS.md, PLAN files)
- [ ] 1F-6. Add hashpool translator config to tollgate-infrastructure-kit Ansible

**Effort:** 3-5 days

---

## Phase 2: SV2 Direct Connection (Roadmap)

**Prerequisite:** Phase 1 complete and working.

### 2A: Update secp256k1 + Enable ElligatorSwift

**Goal:** Get ElligatorSwift support in the ESP32's libsecp256k1.

**Tasks:**
- [ ] 2A-1. Check current secp256k1 submodule version for ellswift module
- [ ] 2A-2. Update submodule if needed (requires secp256k1 >= 0.4.0 with ellswift experimental module)
- [ ] 2A-3. Enable `SECP256K1_ELLSWIFT_IMPL` compile flag in CMakeLists.txt
- [ ] 2A-4. Verify nucula still builds with updated secp256k1
- [ ] 2A-5. Unit tests: ellswift encode/decode roundtrip with BIP-324 test vectors

**Risk:** Updating secp256k1 may break nucula integration. Test carefully.

**Effort:** 5-10 days

---

### 2B: Noise NX Handshake in C

**Goal:** Implement Noise NX encryption protocol for SV2 transport.

**Tasks:**
- [ ] 2B-1. Implement ChaCha20-Poly1305 AEAD (RFC 8439) using mbedTLS primitives (~200 lines)
- [ ] 2B-2. Implement Noise NX state machine:
  - `Initialize()` with protocol name + prologue
  - `Write()` / `Read()` with ElligatorSwift ECDH -> HKDF -> encrypt/decrypt
  - 3-step handshake: initiator 64 bytes -> responder 234 bytes -> session keys derived
- [ ] 2B-3. Session key management (rekey after N bytes)
- [ ] 2B-4. Unit tests with Noise Framework NX test vectors
- [ ] 2B-5. Integration test: Noise handshake with hashpool pool responder

**Effort:** 3-5 days

---

### 2C: SV2 Binary Codec in C

**Goal:** Implement the SV2 custom binary serialization.

**Tasks:**
- [ ] 2C-1. Frame parser: 6-byte header (extension_type u16 LE + msg_type u8 + length u24 LE) + payload
- [ ] 2C-2. SV2 type serializers: `Str0255`, `B032`, `B064K`, `U256`, `U32AsRef`, `Seq0255`, `Sv2Option`
- [ ] 2C-3. Message structs + serialize/parse for 9 minimum message types:
  - `SetupConnection` (0x00)
  - `SetupConnectionSuccess` (0x01)
  - `OpenStandardMiningChannel` (0x10)
  - `OpenStandardMiningChannelSuccess` (0x11)
  - `NewMiningJob` (0x15)
  - `SetNewPrevHash` (0x20)
  - `SetTarget` (0x21)
  - `SubmitSharesStandard` (0x1a)
  - `SubmitSharesSuccess` (0x1c)
- [ ] 2C-4. Frame chunking for messages > 65535 bytes
- [ ] 2C-5. Unit tests: encode/decode roundtrip for each message type

**Effort:** 2-3 days

---

### 2D: SV2 Client State Machine

**Goal:** Replace SV1 upstream with SV2 direct connection to hashpool pool.

**Tasks:**
- [ ] 2D-1. TCP connection to pool -> Noise NX handshake (initiator role)
- [ ] 2D-2. Send `SetupConnection { protocol: Mining, min_version: 2, max_version: 2 }`
- [ ] 2D-3. Receive `SetupConnectionSuccess`
- [ ] 2D-4. Send `OpenStandardMiningChannel { user_identity, nominal_hash_rate, max_target }`
- [ ] 2D-5. Receive `OpenStandardMiningChannelSuccess { channel_id, target, extranonce_prefix }`
- [ ] 2D-6. Receive `NewMiningJob` / `SetNewPrevHash` -> forward to downstream SV1 miners
- [ ] 2D-7. Send `SubmitSharesStandard` for each valid share from downstream
- [ ] 2D-8. Handle `SetTarget` (difficulty adjustment)
- [ ] 2D-9. Reconnection logic on disconnect
- [ ] 2D-10. Integration test: SV2 connection to hashpool pool, submit shares, receive jobs

**Effort:** 3-5 days

---

### 2E: Mint Quote Subprotocol (SV2)

**Goal:** Receive mint quote responses directly via SV2, eliminating the need for translator pubkey passthrough.

**Tasks:**
- [ ] 2E-1. Implement `SubmitSharesExtended` (0x1b) with `hash` and `locking_pubkey` fields
- [ ] 2E-2. Receive `MintQuoteResponse` (0x81) with quote_id
- [ ] 2E-3. Handle `MintQuoteError` (0x82)
- [ ] 2E-4. Track quote IDs for polling
- [ ] 2E-5. Integration test: share -> mint quote -> claim tokens

**Effort:** 2-3 days

---

### 2F: SV1-SV2 Proxy Bridge

**Goal:** The TollGate acts as an SV1-to-SV2 translator for downstream BitAxe miners.

**Tasks:**
- [ ] 2F-1. SV1 proxy (from Phase 1) receives shares from downstream miners
- [ ] 2F-2. SV2 client (from Phase 2D) forwards shares to hashpool pool
- [ ] 2F-3. SV2 jobs are translated back to SV1 `mining.notify` for downstream miners
- [ ] 2F-4. SV2 difficulty (`SetTarget`) translated to SV1 `mining.set_difficulty`
- [ ] 2F-5. Full E2E test: BitAxe SV1 -> TollGate SV1 proxy -> SV2 -> hashpool pool

**Effort:** 2-3 days

---

### 2G: SV2 Testing + Production

**Tasks:**
- [ ] 2G-1. Full E2E with SV2: miner -> TollGate -> hashpool pool -> mint -> ecash -> internet
- [ ] 2G-2. Performance test: measure SV2 encryption overhead on ESP32-S3
- [ ] 2G-3. Longevity test: 24-hour mining session
- [ ] 2G-4. Remove translator dependency (direct SV2 path only)
- [ ] 2G-5. Update documentation

**Effort:** 3-5 days

---

## Timeline

### Phase 1 (SV1 MVP): 18-28 days total

```
Week 1-2: 1A (SV1 proxy) + 1D (translator) in parallel
Week 2-3: 1B (upstream + pubkey)
Week 3-4: 1C (ecash poller)
Week 4:   1E (miner discovery)
Week 5:   1F (integration testing)
```

### Phase 2 (SV2 Direct): 18-28 days total

```
Week 5-6: 2A (secp256k1 + ellswift)
Week 6-7: 2B (Noise NX handshake)
Week 7:   2C (SV2 binary codec)
Week 8:   2D (SV2 client state machine)
Week 8-9: 2E (mint quote subprotocol)
Week 9:   2F (SV1-SV2 proxy bridge)
Week 10:  2G (testing + production)
```

---

## Dependencies

```
Phase 1A ──> 1B ──> 1C
Phase 1D (independent, server-side)
Phase 1E (depends on 1A)
Phase 1F (depends on all above)

Phase 2A ──> 2B ──> 2C ──> 2D ──> 2E
Phase 2F (depends on 2D + Phase 1A)
Phase 2G (depends on all above)
```

---

## Server-Side Repositories

| Repo | Location | Role |
|------|----------|------|
| hashpool | `/home/c03rad0r/ehash-setup/hashpool/` | SRI fork with CDK Cashu mint, issues ehash for shares |
| axepool | `/home/c03rad0r/axepool/` | SV1 proxy + mint client (stub), simpler V1 variant |
| tollgate-infrastructure-kit | `/home/c03rad0r/tollgate-infrastructure-kit/` | Ansible deployment of CDK mints, Nostr relay, services |
| mint-orchestrator | (inside infra-kit) | Nostr-gated mint quote approval via gRPC to CDK mintd |
| money-printer/cashu-brrr | `/home/c03rad0r/money-printer/cashu-brrr/` | Operator web UI for issuing ecash tokens |

---

## Open Items

- [x] ~~**TCP listen failure**: Raw BSD socket `listen()` rejects SYNs from AP clients~~ — RESOLVED, was not a bug
- [x] ~~**CDK mint API**: Does it have a "list paid quotes by pubkey" endpoint?~~ — RESOLVED: No, but NUT-XX spec is being standardized (`nuts#341`) with active CDK PRs (`cdk#1834`, `cdk#1932`). Using B3 workaround in the meantime.
- [x] ~~**Blinded minting on ESP32**: Does nucula wallet support generating blinding factors and unblinding signatures?~~ — RESOLVED: Yes, nucula has `cashu_blind_message()` and `cashu_unblind()`, but NOT needed for B3 approach.
- [x] ~~**ehash token format**: Are ehash tokens standard Cashu v3 tokens (cashuA...) that `tollgate_core_cashu_decode_token()` can parse?~~ — RESOLVED: Yes, standard `cashuA...` v3 tokens.
- [x] ~~**Translator quote ID relay**: How does the TollGate learn its quote IDs?~~ — RESOLVED: Option B3 — translator mints tokens and pushes them downstream via `mining.token` SV1 notification.
- [ ] **secp256k1 ellswift compatibility**: Will updating secp256k1 break nucula? (Phase 2)
- [ ] **NUT-XX migration**: Track `nuts#341`, `cdk#1834`, `cdk#1932` — migrate when merged

---

## Global Checklist

### Phase 1A: SV1 Stratum Proxy — COMPLETE
- [x] SV1 message parser (subscribe, authorize, submit)
- [x] PoW validation (double-SHA256, target generation)
- [x] Unit tests (35 assertions, Bitcoin genesis block vector)
- [x] Heap allocation for all proxy buffers
- [x] Double-init guard
- [x] TCP listen verified working (loopback + AP client)
- [x] Loopback self-test at boot (conditional via `proxy_self_test` config flag)
- [x] End-to-end: laptop stratum handshake on port 4033
- [ ] Integration test with real BitAxe/NerdQAxe miner
- [ ] Make self-test conditional on config flag

### Phase 1B: Upstream + Locking Pubkey — COMPLETE
- [x] Implement `identity_derive_locking_key()` (HMAC-SHA512 + secp256k1)
- [x] Add `identity_get_locking_pubkey_hex()` accessor
- [x] Add `GET /mining/pubkey` HTTP endpoint
- [x] Modify `stratum_client` authorize to include pubkey
- [x] Unit test: known nsec → expected pubkey (6 golden vectors)
- [x] Verify on hardware: `/mining/pubkey` returns `03703b0d...`

### Phase 1C: Ecash Token Delivery via SV1 Notification (Option B3) — COMPLETE
- [x] Translator: add `locking_pubkey` to `Downstream` struct
- [x] Translator: add `tx_token` channel to `Downstream`
- [x] Translator: add downstream registry `HashMap<Pubkey, Sender<String>>`
- [x] Translator: parse locking pubkey from authorize password
- [x] Translator: register downstream pubkey→channel after authorize
- [x] Translator: spawn token_sender_task per downstream
- [x] Translator: route minted tokens from proof sweeper via registry
- [x] ESP32: handle `mining.token` notification in stratum_client
- [x] ESP32: decode cashuA token + store in nucula wallet
- [x] Unit test: token notification JSON parsing (40 assertions)
- [ ] Integration test: translator → token → ESP32 wallet (needs hardware)

### Phase 1C-HW: Memory Optimization + Mining Integration Test
- [x] Add service flags to config.h (sync_enabled, wifistr_enabled, local_relay_enabled, mint_health_enabled)
- [x] Add defaults + JSON parsing in config.c
- [x] Conditional task creation in tollgate_main.c
- [x] Bundle relay_selector with sync/wifistr — skip when both disabled
- [x] Unit tests pass
- [x] Build + flash mining config to working NerdAxe
- [x] Verify stratum_cli + sw_miner tasks created on boot
- [x] Write test-mining-token.mjs integration test
- [x] Add Makefile targets
- [x] Run integration test with real Cashu token
- [x] Commit + push

### Phase 1D: Hashpool Translator Pubkey Passthrough — COMPLETE (hashpool `6e13aa78`)
- [x] Parse locking pubkey from authorize password (handle_authorize)
- [x] Store per-downstream pubkey in Downstream.locking_pubkey (RefCell)
- [x] Add downstream registry (HashMap<Pubkey, Downstream>)
- [x] Add send_token_notification() for mining.token push
- [x] Route minted tokens from proof sweeper to registered downstreams
- [x] Fix E0521 lifetime error
- [x] Fix DownstreamMap visibility (pub type + re-export in main.rs)
- [x] Add [mint] section to translator config
- [x] Add locking_pubkey to SubmitShareWithChannelId
- [x] Populate locking_pubkey in handle_submit()
- [x] Use per-share pubkey in translate_submit()
- [x] Commit all translator changes (hashpool `6e13aa78`)
- [ ] Integration test: SV1 client with pubkey -> verify reaches pool (needs hardware)

### Phase 1E: Miner Auto-Discovery
- [ ] Scan for TollGate SSIDs
- [ ] Auto-configure stratum to AP IP

### Phase 1F: Integration Testing (CRITICAL PATH — needs hardware)
- [ ] 1F-1. Flash mining config to Board A + run test-mining-token
- [ ] 1F-2. Full E2E with translator: translator mints → mining.token → ESP32 wallet
- [ ] 1F-3. Full E2E: Board + NerdAxe + hashpool VPS → mining → token → internet
- [ ] 1F-4. Update documentation

---

### Phase 1G: Ship Phase 1 — Full Mining Chain Deployment (Session 2026-06-10)

**Goal:** Deploy hashpool mining stack on VPS1, connect ESP32 boards via stratum, verify full chain: miner → translator → pool → mint → token → wallet → internet.

**Architecture:**
```
NerdQAxe Miner              Laptop                     VPS1 (66.92.204.38)
    |                          |                            |
    +-- WiFi -> TollGate AP -->|                            |
    |   SV1 stratum :3334      |                            |
    |                          |                            |
    |   ESP32 stratum_client --+--- internet :34255 ------->|  bitcoind (regtest)
    |   (VPS translator)       |                            |  pool (SV2 :34254)
    |                          |                            |  jd-server (:34264)
    |                          |                            |  jd-client
    |                          |                            |  mint (:3338)
    |                          |                            |  translator (:34255)
    |                          |                            |
    |   Token -> wallet -> session -> internet             |
```

**Board mapping (verified 2026-06-10):**
- `/dev/ttyACM0`: Working NerdAxe (MAC `80:b5:4e:c7:7a:d0`) — Board A
- `/dev/ttyACM1`: Fan-damaged NerdAxe (MAC `80:b5:4e:c7:79:88`) — Board B
- WiFi: `studio` / `statek2017` (WPA2, 2.4GHz)
- TollGate-B96D80 AP visible at 82% signal

**VPS1 (66.92.204.38):**
- Debian 12, 2 CPUs, 7.8GB RAM, 29GB free
- bitcoind installed (not running), sudo access, Docker running
- No Nix/Rust yet — will install Nix + devenv

**VPS2 (23.182.128.51):**
- 7 CDK mints running, Nostr relay on :7777
- 23GB free, no Rust

**Local machine:**
- 13GB free (95% full), Rust 1.95.0, no Nix

**Deployment choice: VPS1 via Nix** — 30GB free, matches future Ansible workflows, devenv orchestrates all 9 services.

#### Phase 1G Checklist

##### Step 0: Update .env for new WiFi
- [ ] 1G-0-1. Change WIFI_SSID to `studio`, WIFI_PASSWORD to `statek2017` in `.env`

##### Step 1: Local code changes (no hardware)
- [ ] 1G-1-1. Fix display init timeout: move `display_enabled` check after `tollgate_config_init()` in `tollgate_main.c`
- [ ] 1G-1-2. Make `display_init()` fail gracefully (if axs15231b_init fails, set enabled=false and continue)
- [ ] 1G-1-3. Add `write-mining-config-vps` Makefile target (accepts STRATUM_HOST param)
- [ ] 1G-1-4. Run `make test-unit` — all tests pass
- [ ] 1G-1-5. Build firmware (`idf.py build`)

##### Step 2: Install Nix on VPS1
- [ ] 1G-2-1. Install Nix multi-user on VPS1 (66.92.204.38)
- [ ] 1G-2-2. Install devenv via `nix profile install`
- [ ] 1G-2-3. Verify: `devenv version`

##### Step 3: Deploy hashpool on VPS1
- [ ] 1G-3-1. Clone hashpool: `git clone https://github.com/vnprc/hashpool.git ~/hashpool`
- [ ] 1G-3-2. Change `devenv.nix`: `bitcoinNetwork = "regtest"`
- [ ] 1G-3-3. Change `mint.config.toml`: `listen_host = "0.0.0.0"` (currently localhost)
- [ ] 1G-3-4. Verify translator `tproxy.config.toml`: `downstream_address = "0.0.0.0"` (already correct)
- [ ] 1G-3-5. Verify pool `pool.config.toml`: `listen_address = "0.0.0.0:34254"` (already correct)
- [ ] 1G-3-6. Create state directories: `mkdir -p .devenv/state/{bitcoind,cln,translator,mint} logs`
- [ ] 1G-3-7. Run `devenv up` — start all 9 services
- [ ] 1G-3-8. Generate regtest blocks: `just generate-blocks 16`
- [ ] 1G-3-9. Verify translator listening on `0.0.0.0:34255`
- [ ] 1G-3-10. Verify mint listening on `0.0.0.0:3338`
- [ ] 1G-3-11. Verify pool listening on `0.0.0.0:34254`

##### Step 4: Open VPS1 firewall
- [ ] 1G-4-1. `sudo ufw allow 34255/tcp` (translator SV1 downstream)
- [ ] 1G-4-2. Verify connectivity: `nc -z 66.92.204.38 34255` from laptop

##### Step 5: Flash TollGate firmware to both boards
- [ ] 1G-5-1. `make lock-a PHASE="flash+config"`
- [ ] 1G-5-2. `make flash-a` — flash Board A (working NerdAxe, ACM0)
- [ ] 1G-5-3. `make write-mining-config-vps BOARD=a STRATUM_HOST=66.92.204.38` — write config A
- [ ] 1G-5-4. Verify Board A serial: boot + WiFi connect + stratum_client start
- [ ] 1G-5-5. `make lock-b PHASE="flash+config"`
- [ ] 1G-5-6. `make flash-b` — flash Board B (fan-damaged NerdAxe, ACM1)
- [ ] 1G-5-7. `make write-mining-config-vps BOARD=b STRATUM_HOST=66.92.204.38` — write config B
- [ ] 1G-5-8. Verify Board B serial: boot + WiFi connect + stratum_client start

##### Step 6: Verify ESP32 stratum connection to VPS translator
- [ ] 1G-6-1. Check Board A serial: stratum_client connects to 66.92.204.38:34255
- [ ] 1G-6-2. Check VPS translator logs: downstream connected from ESP32 IP
- [ ] 1G-6-3. Verify mining.subscribe + mining.authorize succeed
- [ ] 1G-6-4. Repeat for Board B

##### Step 7: Verify token flow with devenv test miner
- [ ] 1G-7-1. Check VPS translator logs: test miner submitting shares
- [ ] 1G-7-2. Check pool logs: shares accepted
- [ ] 1G-7-3. Check mint logs: quotes generated, tokens minted
- [ ] 1G-7-4. Check translator logs: token routed to downstream (test miner or ESP32)

##### Step 8: Flash NerdQAxe firmware (TOLLGATE-enabled)
- [ ] 1G-8-1. Reflash existing May 28 TOLLGATE build to Board A (NERDQAXEPLUS)
- [ ] 1G-8-2. Configure NerdQAxe stratum: NVS `stratumurl` = TollGate AP IP, `stratumport` = 3334
- [ ] 1G-8-3. Reflash existing May 28 TOLLGATE build to Board B
- [ ] 1G-8-4. Configure NerdQAxe stratum for Board B

##### Step 9: Full E2E — Mine → Token → Internet
- [ ] 1G-9-1. NerdQAxe connects to TollGate AP, SV1 handshake on port 3334
- [ ] 1G-9-2. TollGate proxy forwards to VPS translator
- [ ] 1G-9-3. Verify shares flow: NerdQAxe → TollGate → translator → pool
- [ ] 1G-9-4. Verify token: pool → mint → ehash token → mining.token → TollGate wallet
- [ ] 1G-9-5. Payment: token → session → internet access

##### Step 10: Smoke tests + cleanup
- [ ] 1G-10-1. Run `make smoke` integration tests
- [ ] 1G-10-2. Commit + push all local changes

---

## Phase 1H: Faucet-Based Token Delivery — Hardware Verification Plan

**Status:** IN PROGRESS
**Branch:** `master`
**Tag:** `v1.4.0` (SV1 submit fix + stable stratum), targeting `v1.5.0` (mining-for-internet MVP)
**Commit:** `f7f24fa` (faucet client module)

### Architecture: Faucet Polling Approach

Instead of the translator pushing `mining.token` notifications (which was never wired up in the VPS translator), the ESP32 **periodically polls** the translator's faucet HTTP endpoint to pull ehash tokens into its on-device wallet.

```
VPS Translator Faucet (:8083)
  POST /mint/tokens {"amount":10}
  → {"success":true, "token":"cashuA...", "amount":32}

ESP32 faucet_client (FreeRTOS task, every 120s)
  → HTTP POST to faucet URL
  → Parse JSON response
  → tls_worker_submit(token) → nucula_wallet_receive(token)
  → Proofs stored in on-device wallet
```

### Critical Blocker: Mint URL Mismatch — RESOLVED

**Problem:** Faucet tokens carry mint URL `http://localhost:3338` (hashpool mint's configured URL). ESP32 wallet is initialized with `https://testnut.cashu.exchange`. These are different mints (different pubkeys, different units: `ehash` vs `sat`).

**Resolution: Dual wallet + VPS mint URL fix**
1. VPS: Change `mint.config.toml` and `tproxy.config.toml` `[mint]` URL from `http://localhost:3338` → `http://66.92.204.38:3338`
2. Restart mint + translator on VPS
3. ESP32: Add `http://66.92.204.38:3338` to `accepted_mints` in config → wallet[1]
4. Token mint URL now matches wallet[1] URL → `receive()` succeeds

### What's Working

| Component | Status | Evidence |
|-----------|--------|----------|
| ESP32 stratum client → VPS translator | Working | 6,363+ shares, 0 `InvalidSubmission` errors |
| VPS hashpool chain | Running | bitcoind → SV2 pool → CDK mint → translator, all up |
| Faucet API | Working | `POST http://66.92.204.38:8083/mint/tokens` returns tokens (~660K ehash minted) |
| Faucet externally accessible | Working | Verified from dev machine: `curl` returns `{"success":true,"token":"cashuA...","amount":32}` |
| Faucet client code | Complete | `faucet_client.c/h`, 13 unit tests pass, firmware builds clean |
| 701+ unit tests | All passing | 34 test files |

### What's Not Yet Verified (Hardware)

1. Faucet client polls from ESP32 (HTTP client on ESP32 → VPS faucet)
2. Token `receive()` succeeds with ehash-denominated proofs (unit mismatch risk)
3. Wallet balance accumulates from repeated faucet polls
4. Wallet `send()` can produce a token from ehash proofs for payment
5. Captive portal accepts ehash token → creates session → grants internet
6. Full loop: mine → faucet → wallet → pay → session → internet

### Hardware Verification Plan

#### Phase 0: Fix Mint URL Mismatch (VPS + Code Changes)

**VPS changes:**
- [ ] 0-1. Edit `~/hashpool/config/mint.config.toml`: change `url = "http://localhost:3338"` → `url = "http://66.92.204.38:3338"`
- [ ] 0-2. Edit `~/hashpool/config/tproxy.config.toml` `[mint]` section: change `url = "http://localhost:3338"` → `url = "http://66.92.204.38:3338"`
- [ ] 0-3. Restart mint + translator on VPS
- [ ] 0-4. Verify faucet returns external URL: `curl -X POST http://66.92.204.38:8083/mint/tokens -d '{"amount":1}'` — token should contain `66.92.204.38:3338`

**ESP32 code changes:**
- [ ] 0-5. Update `write-mining-config-vps` Makefile target: add `http://66.92.204.38:3338` to `accepted_mints` array, set as `mint_url`
- [ ] 0-6. `make test-unit` passes
- [ ] 0-7. `idf.py build` succeeds

#### Phase 1: Build & Flash

- [ ] 1-1. `make lock-a PHASE="faucet-hw-test"`
- [ ] 1-2. `make write-mining-config-vps BOARD=a STRATUM_HOST=66.92.204.38 STRATUM_PORT=34255`
- [ ] 1-3. `make flash-a`
- [ ] 1-4. Start serial monitor (`idf.py -p /dev/ttyACM0 monitor`)

#### Phase 2: Boot Verification (Serial Monitor)

Watch for these log sequences within ~30s of boot:
```
I identity: Derived identity: npub=... MAC=... SSID=TollGate-B96D80 IP=10.185.47.1
I stratum_client: Connected to 66.92.204.38:34255
I stratum_client: Subscribe response: extranonce2_size=8
I stratum_client: Sent mining.authorize for user=tollgate_test (with locking pubkey)
I faucet_client: Faucet client started (url=http://66.92.204.38:8083/mint/tokens, interval=120s)
```
After 30s initial delay:
```
I faucet_client: Received 32 ehash from faucet
I nucula_wallet: Received 32 sat (N proofs) via wallet[http://66.92.204.38:3338], new balance=32
```

- [ ] 2-1. Board boots, connects to WiFi, gets upstream IP
- [ ] 2-2. Stratum client connects to VPS translator
- [ ] 2-3. Faucet client task starts
- [ ] 2-4. First faucet poll succeeds (token received, wallet receive OK)
- [ ] 2-5. **BLOCKER CHECK:** If `nucula_wallet_receive` fails with ehash proofs, investigate unit handling

#### Phase 3: Wallet Balance Verification (HTTP)

- [ ] 3-1. `curl http://10.185.47.1:2121/wallet` — verify `balance > 0`
- [ ] 3-2. Wait 2-3 faucet poll intervals (4-6 min), check again — balance increased
- [ ] 3-3. `curl http://10.185.47.1:2121/wallet` — verify `proof_count > 0`

#### Phase 4: Multiple Faucet Polls

- [ ] 4-1. Wait for 3+ faucet polls, verify balance grows each time
- [ ] 4-2. Check serial for any error patterns (faucet_client WARN/ERROR)

#### Phase 5: Payment → Session → Internet (Manual E2E)

- [ ] 5-1. `curl -X POST http://10.185.47.1:2121/wallet/send -d '21'` — get cashuA token from wallet
- [ ] 5-2. Submit token via captive portal: `curl -X POST http://10.185.47.1/ -d '<token>'`
- [ ] 5-3. Verify session created: `curl http://10.185.47.1:2121/usage` — not `-1/-1`
- [ ] 5-4. Connect to TollGate AP from laptop, verify internet access
- [ ] 5-5. Wait for session expiry, verify internet blocked
- [ ] 5-6. Repeat: wallet send → portal pay → session restored

#### Phase 6: Integration Tests

- [ ] 6-1. `TOLLGATE_IP=10.185.47.1 make test-mining-token`
- [ ] 6-2. `TOLLGATE_IP=10.185.47.1 make smoke`
- [ ] 6-3. Write `tests/integration/test-faucet-wallet.mjs` — faucet poll + wallet balance verification

#### Phase 7: Full E2E with NerdQAxe Miner (if hardware available)

- [ ] 7-1. Connect NerdQAxe to TollGate-B96D80 AP
- [ ] 7-2. Configure NerdQAxe stratum: `10.185.47.1:3334`
- [ ] 7-3. Verify shares flow: NerdQAxe → ESP32 proxy → VPS translator → pool
- [ ] 7-4. Faucet accumulates tokens from mining activity
- [ ] 7-5. Wallet balance grows from faucet polls
- [ ] 7-6. Full loop: mine → faucet → wallet → pay → session → internet

#### Phase 8: Cleanup & Tag

- [ ] 8-1. Commit all fixes and test results
- [ ] 8-2. Tag `v1.5.0` — mining-for-internet MVP
- [ ] 8-3. Update CHECKLIST.md, PLAN_MINING_INTERNET.md, AGENTS.md
- [ ] 8-4. `make unlock-a`
- [ ] 8-5. Push all repos

### Current Status (Session 2026-06-11)

**Phase 0 fixes all applied and verified on VPS.** Three bugs found and fixed:
1. Faucet returned `cashuB` (V4) → fixed to `cashuA` (V3)
2. Keyset ID mismatch (16-char vs 64-char) → fixed with prefix matching
3. (Still diagnosing) Faucet client produces no logs after second flash

**Board A running** with hashpool mint config, stratum connected to VPS, mining jobs flowing.
**BLOCKER:** `faucet_client` FreeRTOS task produces zero log output after the keyset-fix reflash.

#### Diagnosis Steps (Faucet Client Silence)

**Step 1: Clean reflash**
- Erase app partition + full rebuild + flash to ensure firmware is current
- Capture full boot sequence (first 60s) from serial

**Step 2: Verify config via HTTP**
- Connect laptop to `TollGate-B96D80` WiFi AP
- Query `GET /debug` for `mining_enabled` and faucet config
- Query `GET /wallet` for wallet state

**Step 3: Diagnose root cause**
- If `faucet_url` empty → config parsing bug
- If `mining_enabled` false → config JSON issue
- If task creation fails → RAM shortage, reduce task stack
- If task runs but no logs → log level or buffer issue

#### Remaining Steps (after diagnosis)

**Step 4: Verify faucet poll + wallet receive**
- Serial: `faucet_client: Received 32 ehash from faucet`
- Serial: `nucula_wallet: Received 32 sat (N proofs) via wallet[...], new balance=32`

**Step 5: Wallet balance accumulation** (2-3 polls, 4-6 min)

**Step 6: Manual E2E** (wallet send → portal pay → session → internet)

**Step 7: Integration tests**

**Step 8: Cleanup, tag v1.5.0**

### Key Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Faucet task can't be created (RAM) | No token polling | Reduce task stack to 4096, reduce sw_miner stack |
| `nucula_wallet_receive()` fails with ehash proofs | Can't store tokens | Check swap path for ehash unit support |
| `wallet send` produces ehash token, portal expects sat | Payment rejected | Bypass allotment check for self-wallet tokens |
| WiFi disconnect when switching APs | Lose connectivity | Use serial for critical steps |

### Log Tags Reference

| Tag | Key Messages |
|-----|-------------|
| `faucet_client` | `Received N ehash from faucet`, `Faucet request failed`, `Failed to parse faucet JSON` |
| `stratum_client` | `Connected to`, `Subscribe response: extranonce2_size=`, `Share submitted` |
| `nucula_wallet` | `Received N sat (X proofs) via wallet[URL]`, `Failed to decode token`, `No wallet found for mint` |
| `tls_worker` | `No wallet queue, receiving synchronously`, `Wallet queue full` |
| `tollgate_api` | Payment processing, session creation |

### Future (Phase 2+)

After 1H is complete:
- **Phase 1E:** NerdQAxe ASIC miner auto-discovery (scan for TollGate-* SSIDs)
- **Phase 1I:** Auto-payment — ESP32 auto-submits mined tokens for sessions (no manual step)
- **Phase 2A:** SV2 direct on ESP32 — bypass translator, Noise NX + ElligatorSwift
- **Phase 2B:** Multiple miners per TollGate, per-miner token accounting
- **Phase 3:** Production deployment — Ansible playbooks, monitoring, alerts
