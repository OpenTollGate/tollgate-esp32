# Mining-for-Internet via Hashpool Ecash — Implementation Plan

**Created:** 2026-05-29
**Updated:** 2026-05-30
**Status:** Phase 1A — Debugging TCP listen failure
**Branch:** `feature/tollgate-core-v2` (esp32-tollgate)

---

## Architecture Overview

```
BitAxe Miner          ESP32 TollGate                    Hashpool Translator          Hashpool Pool + CDK Mint
    |                        |                                |                              |
    |--1. WiFi connect ---->| AP (sandbox mode)               |                              |
    |                        |                                |                              |
    |--2. SV1 stratum ------>| SV1 Proxy (port 3334)           |                              |
    |   subscribe/auth       | - Full SV1 handshake            |                              |
    |   mining.submit        | - Local PoW validation          |                              |
    |                        | - Per-client hashrate tracking   |                              |
    |                        |                                |                              |
    |                        |--3. SV1 upstream -------------->| Translator (SV1->SV2)          |                              |
    |                        |   authorize password includes   | Extracts TollGate's           |
    |                        |   TollGate's locking_pubkey     | locking_pubkey, forwards      |
    |                        |                                | as own in SubmitSharesExtended|
    |                        |                                |--4. SV2 SubmitSharesExtended->|
    |                        |                                |                              |
    |                        |                                |<--5. Pool mints ehash quote ---|
    |                        |                                |    for TollGate's pubkey      |
    |                        |                                |                              |
    |                        |--6. Poll CDK mint HTTP API ---------------------------------->|
    |                        |   GET /v1/mint/quote (paid quotes for our pubkey)             |
    |                        |   POST /v1/mint (blinded minting -> ecash proofs)             |
    |                        |<--7. Ecash proofs stored in nucula wallet -------------------|
    |                        |                                                              |
    |                        |   8. Miner submits ecash token to TollGate (existing          |
    |                        |      Cashu payment endpoint, same as manual payment)          |
    |                        |   9. Session created, firewall grants NAT                     |
    |                        |                                                              |
    |<--10. Internet ---------| NAT open                                                     |
```

### SV2 Future Path (Phase 2)

```
BitAxe Miner          ESP32 TollGate (SV1 downstream, SV2 upstream)     Hashpool Pool + CDK Mint
    |                        |                                              |
    |-- SV1 stratum -------->| SV1 Proxy (downstream miners)                |
    |                        |                                              |
    |                        |-- SV2 direct (Noise NX encrypted) ---------->|
    |                        |   SubmitSharesExtended with locking_pubkey   |
    |                        |   MintQuoteRequest/Response                  |
    |                        |<-- NewMiningJob, SetNewPrevHash -------------|
    |                        |                                              |
    |                        |-- Poll mint HTTP for ecash ----------------->|
    |                        |<-- Ecash tokens -----------------------------|
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
- [ ] 1A-15. Make self-test conditional on config flag (currently runs every boot)

---

### 1B: Upstream Connection + Locking Pubkey — IN PROGRESS

**Goal:** Derive a deterministic Cashu locking keypair from the device nsec, and include the pubkey in upstream stratum authorize so the hashpool translator can attribute ehash minting to this specific TollGate.

**Design:**
```
HMAC-SHA512(nsec_bytes, "tollgate-cashu-locking-key") -> 64 bytes
  -> first 32 bytes = locking_privkey (secp256k1 scalar)
  -> secp256k1 compressed pubkey (33 bytes) = locking_pubkey
  -> hex string (66 chars) included in authorize password: "worker_name.locking_pubkey_hex"
```

**Files to modify:**
- `main/identity.c/h` — add `identity_get_locking_pubkey()` / `identity_get_locking_privkey()`
- `main/stratum_client.c` — modify `send_authorize()` to append pubkey
- `main/tollgate_api.c` — add `GET /mining/pubkey` endpoint

**Tasks:**
- [ ] 1B-1. Implement `identity_derive_locking_key()` in `identity.c`:
  - HMAC-SHA512(nsec_bytes, "tollgate-cashu-locking-key") using mbedtls
  - First 32 bytes → secp256k1 scalar (verify < curve order)
  - Compute compressed pubkey (33 bytes)
  - Store in static, return on demand
- [ ] 1B-2. Add `identity_get_locking_pubkey_hex()` — returns 66-char hex string
- [ ] 1B-3. Add `GET /mining/pubkey` endpoint in `tollgate_api.c`
- [ ] 1B-4. Modify `stratum_client send_authorize()` to include `.locking_pubkey_hex` in password
- [ ] 1B-5. Unit test: known nsec → expected HMAC output → expected pubkey
- [ ] 1B-6. Verify on hardware: check `/mining/pubkey` returns correct hex

---

### 1C: Ecash Token Poller

**Goal:** The TollGate periodically polls the hashpool CDK mint for paid ehash quotes and claims tokens into its nucula wallet.

**Files to create:**
- New: `components/tollgate_core/src/tollgate_core_mint_poller.c`
- New: `components/tollgate_core/include/tollgate_core_mint_poller.h`

**Files to modify:**
- `main/tollgate_main.c` — start poller in services
- `components/nucula_lib/nucula_wallet.h` — add blinded minting functions if needed

**Tasks:**
- [ ] 1C-1. Investigate CDK mint REST API for "list paid quotes" endpoint
- [ ] 1C-2. If no endpoint exists, add custom endpoint to CDK mint (server-side) OR implement quote ID relay via translator
- [ ] 1C-3. Create FreeRTOS poller task (configurable interval, default 60s)
- [ ] 1C-4. HTTP GET paid quotes for TollGate's locking pubkey
- [ ] 1C-5. Implement Cashu blinded minting flow:
  - Generate random blinding factors (secp256k1)
  - Create blinded messages (NUT-00)
  - POST `/v1/mint` with blinded messages + quote_id
  - Receive blinded signatures, unblind to create proofs
- [ ] 1C-6. Store proofs in nucula wallet
- [ ] 1C-7. Error handling: mint unreachable, no quotes, network timeout
- [ ] 1C-8. Unit tests: blinding/unblinding with known test vectors
- [ ] 1C-9. Integration test: claim tokens from test CDK mint

**Effort:** 5-7 days (may increase if CDK mint needs modification)

**Open items:**
- [ ] Investigate whether CDK mint has a "list quotes by pubkey" endpoint
- [ ] Investigate whether nucula wallet supports blinded minting (generate blinding factors, unblind signatures)
- [ ] Determine ehash token format: standard Cashu (cashuA...) or custom?

---

### 1D: Hashpool Translator Pubkey Passthrough (server-side Rust)

**Goal:** The hashpool translator extracts the TollGate's locking pubkey from downstream SV1 authorize and uses it in `SubmitSharesExtended`.

**Files to modify (in ehash-setup/hashpool):**
- `roles/translator/src/lib/downstream_sv1/downstream.rs`
- `roles/translator/src/lib/proxy/bridge.rs`
- `roles/translator/src/lib/mod.rs`

**Tasks:**
- [ ] 1D-1. Parse `mining.authorize` password for locking pubkey (format: `worker.66_char_hex_compressed_pubkey`)
- [ ] 1D-2. Store per-downstream pubkey in connection state
- [ ] 1D-3. Use downstream's pubkey in `translate_submit()` instead of translator's configured pubkey
- [ ] 1D-4. Handle multiple downstreams with different pubkeys
- [ ] 1D-5. Unit tests: password parsing with/without pubkey
- [ ] 1D-6. Integration test: SV1 client with pubkey -> verify reaches pool

**Effort:** 3-5 days

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
- [ ] **CDK mint API**: Does it have a "list paid quotes by pubkey" endpoint? If not, need custom endpoint or alternative.
- [ ] **Blinded minting on ESP32**: Does nucula wallet support generating blinding factors and unblinding signatures? Or do we need to add this?
- [ ] **ehash token format**: Are ehash tokens standard Cashu v3 tokens (cashuA...) that `tollgate_core_cashu_decode_token()` can parse?
- [ ] **Translator quote ID relay**: How does the TollGate learn its quote IDs? Options:
  - a) Translator relays in SV1 submit response
  - b) Poll mint for all paid quotes
  - c) Custom HTTP endpoint on translator
- [ ] **secp256k1 ellswift compatibility**: Will updating secp256k1 break nucula?

---

## Global Checklist

### Phase 1A: SV1 Stratum Proxy — COMPLETE
- [x] SV1 message parser (subscribe, authorize, submit)
- [x] PoW validation (double-SHA256, target generation)
- [x] Unit tests (35 assertions, Bitcoin genesis block vector)
- [x] Heap allocation for all proxy buffers
- [x] Double-init guard
- [x] TCP listen verified working (loopback + AP client)
- [x] Loopback self-test at boot
- [x] End-to-end: laptop stratum handshake on port 4033
- [ ] Integration test with real BitAxe/NerdQAxe miner
- [ ] Make self-test conditional on config flag

### Phase 1B: Upstream + Locking Pubkey — IN PROGRESS
- [ ] Implement `identity_derive_locking_key()` (HMAC-SHA512 + secp256k1)
- [ ] Add `identity_get_locking_pubkey_hex()` accessor
- [ ] Add `GET /mining/pubkey` HTTP endpoint
- [ ] Modify `stratum_client` authorize to include pubkey
- [ ] Unit test: known nsec → expected pubkey
- [ ] Verify on hardware

### Phase 1C: Ecash Token Poller
- [ ] Investigate CDK mint REST API
- [ ] Implement poller task
- [ ] Cashu blinded minting flow
- [ ] Store proofs in nucula wallet

### Phase 1D: Hashpool Translator (server-side Rust)
- [ ] Parse locking pubkey from authorize password
- [ ] Use downstream pubkey in SubmitSharesExtended

### Phase 1E: Miner Auto-Discovery
- [ ] Scan for TollGate SSIDs
- [ ] Auto-configure stratum to AP IP

### Phase 1F: Integration Testing
- [ ] Full E2E test
- [ ] Update documentation
