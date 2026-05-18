# Mining-for-Bandwidth Implementation Plan

## Overview

Add Bitcoin mining (Stratum v1 + v2) to the TollGate firmware so that devices earn internet access by mining real Bitcoin blocks. A BitAxe (ESP32-S3 + BM1366 ASIC) running TollGate firmware becomes a plug-and-play mesh node — no e-cash, Nostr identity, or prior setup required.

## Design Decisions

### Why real Bitcoin mining instead of arbitrary proof-of-work?

The work must be **useful** — mining against a Stratum v2 block template means every share contributes to Bitcoin's security. Even at negligible hashrate (ESP32 software mining at ~10-50 kH/s), the work is real. With a BM1366 ASIC (~500 GH/s), the device produces meaningful hashrate.

### Why Stratum v2 upstream + Stratum v1 local?

- **SV2 upstream (to pool)**: Binary framing is bandwidth-efficient, Noise encryption prevents hash hijacking, and the encrypted tunnel uses minimal megabytes on the paid internet link
- **SV1 local (to downstream miners)**: JSON-RPC is trivial to implement, no handshake overhead, works over local WiFi with negligible latency
- BitAxe already has both implementations — we reuse them

### Why Braiins Pool as default SV2 pool?

- Native SV2 support with published authority pubkey
- 0% PPLNS fee option
- Lightning Network payouts (useful for converting mining revenue to e-cash)
- The authority pubkey is known: `024e031a0b63c7885b19e48f76d49ddbcda9bf3d7f1d6b05df8b71569e2c2f7ff0`

### Why dual payout mode (Lightning sats vs e-cash)?

A TollGate's position in the mesh determines what it earns:

| Position | Hashrate goes to | Earns |
|----------|-----------------|-------|
| Standalone (has direct internet) | Braiins pool | Lightning sats for operator |
| Mesh node (upstream TollGate detected) | Upstream TollGate's proxy | e-cash / megabytes / minutes |
| Relay (no ASIC, no internet) | Nothing locally | Just proxies for downstream miners |

The `mining_payout_mode` config field controls this: `auto` (default) detects upstream TollGate and chooses accordingly.

### Why mine with CPU too?

UX. We don't care if CPU mining is profitable. A plain ESP32-S3 without an ASIC can still produce non-zero hashrate (~10-50 kH/s via hardware SHA256 accelerator). This means ANY ESP32 running TollGate firmware can bootstrap itself — even one byte per hour is better than zero.

### Why sandbox mint access?

Miners who earn bandwidth via hashrate should also reach the Cashu mint URLs the TollGate accepts. This way they can receive e-cash from mobile wallets without first having internet access. The firewall whitelists mint URLs for unauthenticated clients.

### Why hashprice from block template?

The conversion from hashrate → bandwidth needs a price signal:

```
hashprice = (block_subsidy * blocks_per_day) / (difficulty * 2^32)  [sat/GH/day]
allotment = (hashrate_ghs * hashprice_per_s * duration_s) / price_per_step * step_size
```

Calculating hashprice from the `nbits` field in the SV2 block template is automatic, requires no external API, and is always accurate. A config override (`hashprice_sats_per_ghs_day`) is available as fallback. A ContextVM MCP tool (`get_hashprice`) is planned for future dynamic pricing.

### Why BitAxe as git submodule?

The BitAxe ESP-Miner firmware (GPL-3.0) runs on the same ESP-IDF v5.x on ESP32-S3. It contains production-quality implementations of:
- `components/stratum_v2/` — SV2 protocol + Noise handshake
- `components/stratum/` — SV1 protocol
- `components/asic/` — BM1366/BM1368 serial drivers

Rather than copying code that will diverge, we reference it as a submodule and compile selected components.

## Architecture

```
[Bitcoin SV2 Pool (Braiins)]
       ↑ SV2 + Noise (encrypted, binary)
[TollGate Gateway] (ESP32-S3, has internet via STA)
  ├── stratum_client.c — SV2 upstream to Braiins (Noise handshake, job reception, share forwarding)
  ├── stratum_proxy.c — Local SV1 TCP :3333 (distribute jobs, collect shares, per-IP hashrate meter)
  ├── mining_payment.c — Share validation, hashprice calc, session_create() calls
  ├── sw_miner.c — ESP32-S3 hardware SHA256 accelerator (~10-50 kH/s, always runs)
  ├── asic_miner.c — BM1366/BM1368 via SPI (~500 GH/s, if detected)
  ├── Existing: captive portal, Cashu, firewall, sessions, wifistr, CVM

[TollGate Miner] (BitAxe ESP32-S3 + BM1366, no internet)
  ├── SV1 client → connects to gateway's :3333 via local WiFi
  ├── ASIC driver → BM1366 via SPI
  ├── tollgate_client.c → mining mode (instead of Cashu payment)
  └── Also runs its own AP for downstream devices

[Plain ESP32 TollGate]
  ├── SV1 client → connects to gateway's :3333
  ├── sw_miner.c only (~10-50 kH/s)
  └── Also runs its own AP for downstream devices
```

## Config Fields (config.json)

```json
{
  "mining_enabled": true,
  "mining_payout_mode": "auto",
  "stratum_host": "v2.pool.braiins.com",
  "stratum_port": 3333,
  "stratum_user": "bc1q...TollGate",
  "stratum_pass": "x",
  "stratum_sv2_authority_pubkey": "024e031a0b63c7885b19e48f76d49ddbcda9bf3d7f1d6b05df8b71569e2c2f7ff0",
  "stratum_fallback_host": "public-pool.io",
  "stratum_fallback_port": 21496,
  "mining_port": 3333,
  "hashprice_sats_per_ghs_day": 0,
  "mining_sandbox_mint_access": true
}
```

| Field | Default | Description |
|-------|---------|-------------|
| `mining_enabled` | `false` | Enable mining subsystem |
| `mining_payout_mode` | `"auto"` | `"auto"`, `"pool"`, `"upstream"`, `"proxy_only"` |
| `stratum_host` | `"v2.pool.braiins.com"` | SV2 pool hostname |
| `stratum_port` | `3333` | SV2 pool port |
| `stratum_user` | `""` | Bitcoin/Lightning address for pool payout |
| `stratum_pass` | `"x"` | Pool password |
| `stratum_sv2_authority_pubkey` | Braiins key | Pool authority pubkey for Noise verification |
| `stratum_fallback_host` | `"public-pool.io"` | SV1 fallback pool hostname |
| `stratum_fallback_port` | `21496` | SV1 fallback pool port |
| `mining_port` | `3333` | Local mining proxy listen port |
| `hashprice_sats_per_ghs_day` | `0` | Manual hashprice override (0 = auto from nbits) |
| `mining_sandbox_mint_access` | `true` | Allow unauthenticated clients to reach mint URLs |

## Mining Payout Modes

### `auto` (default)
```
TollGate boots → connects to WiFi (STA)
  → tollgate_client_detect(gw_ip)
    → GET http://gw_ip:2121/
      → If upstream TollGate detected → mine to upstream proxy → earn e-cash/bytes
      → If regular router → mine to Braiins pool → earn Lightning sats
```

### `pool`
Always mine to Braiins/public-pool via SV2. Never mine to upstream TollGate. Gateway's own hashrate earns Lightning sats for the operator.

### `upstream`
Always mine to upstream TollGate's proxy. Fail if no upstream TollGate detected. Gateway's own hashrate earns e-cash/bytes/minutes.

### `proxy_only`
Don't mine locally at all. Only run the local proxy for downstream miners. Useful for plain ESP32 relay nodes without ASICs that don't want to waste CPU on mining.

## Sandbox / Firewall Changes

Unauthenticated clients (no Cashu, no session) get access to:
- `TCP :3333` — mining proxy (get jobs, submit shares)
- `TCP :2121` — tollgate API (`GET /mining/job`, `POST /mining/share`)
- `TCP :80` — captive portal
- `TCP/443` to `mint_url` — so miners can receive e-cash from mobile wallets

## Hashrate-to-Bandwidth Conversion

```
difficulty = nbits_to_difficulty(job.nbits)
hashprice_sats_per_ghs_day = (312500000 * 144) / (difficulty * 2^32)
hashprice_sats_per_ghs_s = hashprice_sats_per_ghs_day / 86400

allotment_ms = (client_hashrate_ghs * hashprice_sats_per_ghs_s * measurement_window_s)
                 / price_per_step * step_size_ms
```

The measurement window is a sliding interval (e.g., 30 seconds). Shares submitted during the window are counted, hashrate is estimated, and allotment is calculated and granted via `session_create()` or `session_extend()`.

## New Files

| File | Purpose |
|------|---------|
| `main/stratum_client.c/h` | SV2 upstream client (connect to Braiins, Noise handshake, receive jobs, submit shares) |
| `main/stratum_proxy.c/h` | Local SV1 TCP server on :3333 (distribute jobs, collect shares, per-IP hashrate meter) |
| `main/mining_payment.c/h` | Share validation (SHA256d check), hashprice calculation, session creation |
| `main/sw_miner.c/h` | Software SHA256 miner using ESP32-S3 hardware SHA256 accelerator |
| `main/asic_miner.c/h` | BM1366/BM1368 ASIC detection + driver wrapper |
| `tests/unit/test_mining_payment.c` | Hashprice calculation tests, share validation tests |
| `tests/unit/test_stratum_proxy.c` | SV1/SV2 frame parsing tests |

## Modified Files

| File | Changes |
|------|---------|
| `main/tollgate_api.c` | Add `GET /mining/job`, `POST /mining/share`; add mining tag to `GET /` discovery |
| `main/tollgate_client.c` | Add mining mode (detect upstream mining support, mine instead of Cashu) |
| `main/tollgate_client.h` | New states: `TG_CLIENT_MINING`, `TG_CLIENT_MINING_ACTIVE` |
| `main/firewall.c/h` | Quarantine allowlist: mining port + mint URLs for unauthenticated clients |
| `main/dns_server.c/h` | Resolve mint URLs for unauthenticated clients |
| `main/session.c/h` | Add `payment_method` field (Cashu vs mining) |
| `main/config.c/h` | Parse all new mining config fields |
| `main/captive_portal.c` | "Mine for Access" tab in portal HTML |
| `main/tollgate_main.c` | Start mining tasks, init ASIC detection |
| `CMakeLists.txt` | Add new source files, reference BitAxe submodule |
| `.gitmodules` | Add `bitaxeorg/ESP-Miner` submodule |

## Implementation Phases

### Phase 1: Foundation (config + submodule + build)
- [ ] Add `bitaxeorg/ESP-Miner` as git submodule at `components/esp-miner/`
- [ ] Add mining config fields to `config.c/h`
- [ ] Update `CMakeLists.txt` to compile new sources
- [ ] Verify build compiles cleanly

### Phase 2: Stratum client (SV2 upstream)
- [ ] Create `main/stratum_client.c/h`
- [ ] SV2 connection lifecycle: TCP connect → Noise handshake → SetupConnection → OpenChannel → receive jobs
- [ ] Job reception: parse NewMiningJob, SetNewPrevHash, SetTarget
- [ ] Share submission: forward shares from local proxy to upstream pool
- [ ] Fallback: if SV2 fails, try SV1 to public-pool.io

### Phase 3: Stratum proxy (local SV1 server)
- [ ] Create `main/stratum_proxy.c/h`
- [ ] TCP listener on `:3333`
- [ ] SV1 JSON-RPC: `mining.subscribe`, `mining.authorize`, `mining.notify`, `mining.submit`
- [ ] Distribute current job to connected miners
- [ ] Collect shares, forward to stratum_client for upstream submission
- [ ] Per-client IP hashrate meter (shares / time window)

### Phase 4: Mining payment
- [ ] Create `main/mining_payment.c/h`
- [ ] `nbits_to_difficulty()` conversion
- [ ] `calculate_hashprice()` from difficulty + block subsidy
- [ ] `validate_share()` — SHA256d(header) < target check
- [ ] `shares_to_allotment()` — hashrate → bandwidth conversion
- [ ] Integration with `session_create()` / `session_extend()`

### Phase 5: API endpoints
- [ ] `GET /mining/job` — return current block template as JSON
- [ ] `POST /mining/share` — accept share, validate, create/extend session
- [ ] Add mining tag to `GET /` discovery response
- [ ] `GET /mining/stats` — current hashrate, total shares, hashprice

### Phase 6: Firewall sandbox
- [ ] `firewall.c` — quarantine allowlist for `:3333`, `:2121`, `:80`
- [ ] `firewall.c` — conditional allow mint URL hostnames if `mining_sandbox_mint_access`
- [ ] `dns_server.c` — resolve mint URLs for unauthenticated clients

### Phase 7: Client mining mode
- [ ] `tollgate_client.c` — detect upstream mining support via discovery tag
- [ ] New state machine: `TG_CLIENT_MINING` → `TG_CLIENT_MINING_ACTIVE`
- [ ] Connect to upstream `:3333` mining proxy
- [ ] Submit shares to earn bandwidth

### Phase 8: Software miner
- [ ] Create `main/sw_miner.c/h`
- [ ] ESP32-S3 hardware SHA256 accelerator via `esp_sha.h` / mbedtls
- [ ] Get job from local stratum proxy, iterate nonces, check against target
- [ ] Low-priority FreeRTOS task (don't starve WiFi/routing)
- [ ] Expected: ~10-50 kH/s

### Phase 9: ASIC miner
- [ ] Create `main/asic_miner.c/h`
- [ ] Probe SPI bus at boot for BM1366/BM1368
- [ ] If ASIC found: use BitAxe driver (`BM1366_send_work`, `BM1366_process_work`)
- [ ] If no ASIC: fall back to software miner
- [ ] Expected: ~500 GH/s (BM1366) or ~120 GH/s (BM1368)

### Phase 10: Portal UI
- [ ] Add "Mine for Access" tab to captive portal HTML
- [ ] Show current hashrate, shares submitted, time earned
- [ ] Auto-start mining when tab is opened (JavaScript Web Crypto SHA256 in browser)
- [ ] Show progress bar / earnings counter

### Phase 11: CVM integration
- [ ] Add `get_hashprice` MCP tool to `mcp_handler.c/h`
- [ ] Returns current hashprice, difficulty, estimated earnings
- [ ] `set_mining_config` tool for remote configuration

### Phase 12: Main integration
- [ ] `tollgate_main.c` — start stratum_client task on boot
- [ ] `tollgate_main.c` — start stratum_proxy task on boot
- [ ] `tollgate_main.c` — start sw_miner task on boot
- [ ] `tollgate_main.c` — start asic_miner task if ASIC detected
- [ ] Mining task lifecycle: start/stop with services

### Phase 13: Tests
- [ ] Unit test: `test_mining_payment.c` — hashprice calc, nbits→difficulty, share validation
- [ ] Unit test: `test_stratum_proxy.c` — SV1 frame parsing, SV2 frame encode/decode
- [ ] Integration test: `mining.mjs` — submit share, verify session, check bandwidth
- [ ] E2E test: `mining.spec.mjs` — portal mining tab, hashrate display

## Checklist — Implementation Progress

### Phase 1: Foundation
- [ ] Add BitAxe git submodule
- [ ] Mining config fields in config.c/h
- [ ] CMakeLists.txt updated
- [ ] Clean build verified

### Phase 2: Stratum Client
- [ ] stratum_client.c/h created
- [ ] SV2 Noise handshake
- [ ] Job reception
- [ ] Share submission
- [ ] SV1 fallback

### Phase 3: Stratum Proxy
- [ ] stratum_proxy.c/h created
- [ ] SV1 JSON-RPC server
- [ ] Job distribution
- [ ] Share collection
- [ ] Per-IP hashrate meter

### Phase 4: Mining Payment
- [ ] mining_payment.c/h created
- [ ] nbits_to_difficulty
- [ ] hashprice calculation
- [ ] share validation (SHA256d)
- [ ] shares_to_allotment conversion
- [ ] session_create integration

### Phase 5: API Endpoints
- [ ] GET /mining/job
- [ ] POST /mining/share
- [ ] Mining discovery tag
- [ ] GET /mining/stats

### Phase 6: Firewall Sandbox
- [ ] Quarantine allowlist for mining ports
- [ ] Mint URL access for unauthenticated clients
- [ ] DNS resolution in sandbox

### Phase 7: Client Mining Mode
- [ ] Mining support detection in tollgate_client.c
- [ ] TG_CLIENT_MINING states
- [ ] Upstream proxy connection
- [ ] Share submission for bandwidth

### Phase 8: Software Miner
- [ ] sw_miner.c/h created
- [ ] ESP32-S3 HW SHA256
- [ ] Job dequeue → nonce iteration → target check
- [ ] Low-priority task

### Phase 9: ASIC Miner
- [ ] asic_miner.c/h created
- [ ] BM1366/BM1368 SPI detection
- [ ] ASIC mining loop
- [ ] Software fallback

### Phase 10: Portal UI
- [ ] "Mine for Access" tab
- [ ] Hashrate display
- [ ] Earnings counter

### Phase 11: CVM Integration
- [ ] get_hashprice MCP tool
- [ ] set_mining_config MCP tool

### Phase 12: Main Integration
- [ ] Mining tasks started on boot
- [ ] ASIC detection at boot
- [ ] Task lifecycle management

### Phase 13: Tests
- [ ] test_mining_payment.c
- [ ] test_stratum_proxy.c
- [ ] integration/mining.mjs
- [ ] e2e/mining.spec.mjs
