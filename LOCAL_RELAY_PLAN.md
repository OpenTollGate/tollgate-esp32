# Local Nostr Relay + Negentropy Sync — Implementation Plan

## Overview

Integrate a local Nostr relay (wisp-esp32) into the TollGate firmware so that all Nostr events are published locally first, then efficiently synced to public relays using negentropy (NIP-77). This reduces WebSocket connection churn, enables true offline operation, and provides smart relay selection via NIP-11 probing.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      TollGate ESP32-S3                        │
│                                                               │
│  ┌─────────────┐     ┌──────────────────────────────────┐    │
│  │ Publishers  │     │  Local Relay :4869                │    │
│  │             │     │  (LittleFS 4MB, 5000 events)      │    │
│  │ wifistr     │────►│                                  │    │
│  │ CEP-6 anns  │────►│                                  │    │
│  │ CVM resp    │─┬──►│                                  │    │
│  │ relay list  │─┤   └──────────┬───────────────────────┘    │
│  └─────────────┘ │              │                            │
│                  │   ┌──────────┴──────────────────────┐     │
│                  │   │  Relay Selector (NIP-11 probes)  │     │
│                  │   │  Seeds: orangesync, damus,       │     │
│                  │   │          nos.lol, nostr.band     │     │
│                  │   │  Re-probe: every 6h              │     │
│                  │   │  Auto-failover: 3 disconnects    │     │
│                  │   └──────────┬──────────────────────┘     │
│                  │              │                            │
│                  │   ┌──────────┴──────────────────────┐     │
│                  │   │  Selected Relays                 │     │
│                  │   │                                  │     │
│                  │   │  primary (best NIP-77):          │     │
│                  │   │    → CVM persistent WS           │     │
│                  │   │    → Negentropy sync (30min)     │     │
│                  │   │                                  │     │
│                  │   │  fallbacks (top 2 others):       │     │
│                  │   │    → REQ-diff sync (6h)          │     │
│                  │   └─────────────────────────────────┘     │
│                  │                                          │
│   ┌──────────────┴──┐                                      │
│   │  CVM Server     │  Persistent WS to primary relay       │
│   │  (subscribe +   │  Real-time receive requests,          │
│   │   respond)      │  send responses immediately           │
│   └─────────────────┘  Also stores in local relay           │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## Design Decisions

### 1. Local-First Publishing (not dual-publish)

**Decision**: All events (wifistr, CEP-6, CVM responses, relay lists) are published to the local relay first. Only CVM responses are also sent in real-time via the persistent CVM WebSocket.

**Reasoning**: Maintaining simultaneous WebSocket connections to N public relays is expensive on ESP32 — each connection consumes a TCP socket, TLS state, and RAM. By publishing locally and syncing later, we reduce the steady-state connection count from 1 persistent + N temporary to 1 persistent + 1 periodic. The local relay always accepts events, even when offline.

**Trade-off**: wifistr service discovery events and CEP-6 announcements appear on public relays with a delay (up to 30 minutes). This is acceptable because these are not time-critical — they're informational and update infrequently (every 6h).

### 2. Negentropy (NIP-77) for Sync

**Decision**: Use the negentropy set-reconciliation protocol for syncing local events to public relays, with REQ-diff as a fallback for relays that don't support NIP-77.

**Reasoning**: Negentropy uses Merkle-tree-based fingerprint comparison to determine exactly which events are missing, transferring only the delta. A typical sync round-trip when nothing is missing is ~100 bytes. This is orders of magnitude more efficient than downloading all events via REQ and comparing locally. The negentropy C++ library is only ~660 lines of header-only code with no external dependencies — feasible for ESP32 with C++ compilation support.

**Trade-off**: Negentropy only works with strfry-based relays (NIP-77). Most major public relays (damus, nos.lol) don't support it. We mitigate this by maintaining REQ-diff as a fallback and prioritizing relays that support NIP-77.

### 3. NIP-11 HTTP Probing for Relay Selection

**Decision**: On boot and every 6 hours, probe seed relays via NIP-11 HTTP requests (not WebSocket). Rank by latency and NIP-77 support. Auto-failover after 3 consecutive disconnects.

**Reasoning**: ESP32 WebSocket connections are expensive. We need to ensure every persistent connection goes to the best available relay. NIP-11 is a simple HTTP GET with `Accept: application/nostr+json` — no WebSocket, no subscription, minimal overhead. From the response we get: liveness, latency, supported NIPs, and connection limits. This lets us select the best relay for each role without maintaining any connections.

**Trade-off**: NIP-11 probes add ~5 seconds of HTTP traffic every 6 hours. Negligible. Relay rankings may not reflect real-time conditions perfectly, but re-probing every 6h is a good balance.

### 4. Approach C: Rewrite Wisp Validator for TollGate Crypto

**Decision**: Port only the relay "core" (ws_server, storage_engine, sub_manager, broadcaster) from wisp-esp32. Rewrite the validator and router to use TollGate's existing secp256k1 + mbedtls instead of wisp's libnostr-c/noscrypt dependencies.

**Reasoning**: TollGate already has secp256k1 linked (via nucula) and mbedtls (via ESP-IDF). Adding libnostr-c + noscrypt + secp256k1-frost would introduce symbol conflicts and bloated flash usage. The validator only needs two crypto operations: SHA-256 event ID computation and Schnorr signature verification — both already available in TollGate's existing libraries.

**Trade-off**: More upfront work than just including libnostr-c, but cleaner build, no symbol conflicts, and smaller binary.

### 5. 4MB LittleFS Relay Storage Partition

**Decision**: Allocate a 4MB LittleFS partition at offset 0x500000 for relay event storage (up to 5000 events with 21-day TTL).

**Reasoning**: TollGate has 16MB flash with 11MB currently unused. 4MB matches wisp's tested capacity. LittleFS is better suited than SPIFFS for the relay's write pattern (many small sequential writes). The SPIFFS partition for config.json remains unchanged.

**Trade-off**: Uses 4MB of the 11MB free space. Still leaves 7MB for future use.

### 6. Client-Accessible Relay

**Decision**: The local relay listens on port 4869 and is accessible to authenticated WiFi clients.

**Reasoning**: Connected clients (who have paid for access) can subscribe to events on the local relay. This enables local CVM tool calls, local service discovery, and mesh scenarios without internet. The firewall ensures only authenticated clients can reach it.

**Trade-off**: Slightly increased attack surface, but limited to authenticated clients and the relay has rate limiting.

### 7. Minimal Seed Relay List (4 relays)

**Decision**: Hardcode 4 seed relays: `relay.orangesync.tech`, `relay.damus.io`, `nos.lol`, `relay.nostr.band`.

**Reasoning**: Fewer relays means fewer NIP-11 probes and simpler selection logic. Orangesync is the user's strfry relay (NIP-77). Damus and nos.lol are major reliable relays. Nostr.band is a strfry backup with NIP-77. Users can override in config.json.

**Trade-off**: Less diversity in relay selection. If all 4 are degraded simultaneously, the device has no alternatives. Mitigated by allowing user-configured relay lists.

### 8. Negentropy as Git Submodule

**Decision**: Include `hoytech/negentropy` as a git submodule, referencing the `cpp/` directory.

**Reasoning**: Matches how nucula and secp256k1 are already included. Easy to update upstream. The library is MIT licensed, header-only C++ with no external dependencies.

**Trade-off**: Requires `git submodule update --init` on clone. Standard for this project.

## Connection Budget

| Connection | Type | Frequency | Duration |
|-----------|------|-----------|----------|
| CVM subscribe/respond | Persistent WS | Always | Continuous |
| Negentropy sync | Temporary WS | Every 30min | ~2-5s |
| REQ-diff fallback sync | Temporary WS | Every 6h | ~5-10s |
| NIP-11 probes | Plain HTTPS | Every 6h | ~5s total |
| Local relay (loopback) | HTTP/WS | Always | No network cost |

**Steady state: 1 persistent WS + brief periodic temporary connections.**

## Flash Layout

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| nvs | 0x9000 | 24KB | Wallet proofs, settings |
| phy_init | 0xf000 | 4KB | PHY calibration |
| factory | 0x10000 | ~4MB | Application firmware |
| storage (SPIFFS) | 0x410000 | 960KB | config.json |
| **relay_store (LittleFS)** | **0x500000** | **4MB** | **Relay event storage** |
| Free | 0x900000 | 7MB | Future use |

## New Files

```
components/wisp_relay/           # From wisp-esp32 (adapted)
  CMakeLists.txt
  ws_server.c/h
  storage_engine.c/h             # Adapted: partition label, LittleFS
  sub_manager.c/h
  broadcaster.c/h
  rate_limiter.c/h
  nip11.c/h                      # Customized for TollGate
  deletion.c/h
  flash_monitor.c/h
  router.c/h                     # REWRITTEN for TollGate cJSON
  relay_validator.c/h            # REWRITTEN for TollGate secp256k1+mbedtls
  relay_core.h

components/negentropy/           # Git submodule → hoytech/negentropy/cpp/
  negentropy.h
  encoding.h
  types.h
  storage/Vector.h
  storage/base.h

main/
  local_relay.c/h               # Thin wrapper for publishing to local relay
  relay_selector.c/h             # NIP-11 HTTP probe + ranking + auto-failover
  sync_manager.c/h               # Negentropy + REQ-diff sync engine
  negentropy_storage.c/h         # Adapter: wisp storage → negentropy storage API

tests/unit/
  test_relay_validator.c
  test_negentropy_sync.c

tests/integration/
  test_local_relay.mjs
  test_sync.mjs
```

## Modified Files

| File | Change |
|------|--------|
| `partitions.csv` | Add 4MB LittleFS partition at 0x500000 |
| `sdkconfig.defaults` | Enable HTTPD WS support, bump LWIP sockets to 20 |
| `CMakeLists.txt` | Add negentropy submodule, wisp_relay component |
| `.gitmodules` | Add negentropy submodule |
| `config.c/h` | Add seed_relays, sync_interval fields |
| `tollgate_main.c` | Init local relay, selector, sync manager in start_services() |
| `wifistr.c` | Publish to local relay only |
| `cvm_server.c` | Use dynamic relay from selector; store responses locally |

## Config Additions

```json
{
  "nostr_seed_relays": [
    "wss://relay.orangesync.tech",
    "wss://relay.damus.io",
    "wss://nos.lol",
    "wss://relay.nostr.band"
  ],
  "nostr_sync_interval_s": 1800,
  "nostr_fallback_sync_interval_s": 21600
}
```

---

## Implementation Checklist

### Phase 0: Branch & Infrastructure
- [ ] Create `feature/local-relay` branch
- [ ] Create git worktree at `../esp32-tollgate-relay`
- [ ] Add `hoytech/negentropy` git submodule

### Phase 1: Partition & Build System
- [ ] Update `partitions.csv` with 4MB LittleFS relay partition
- [ ] Update `sdkconfig.defaults`: `CONFIG_HTTPD_WS_SUPPORT=y`, `CONFIG_LWIP_MAX_SOCKETS=20`
- [ ] Update `CMakeLists.txt` with negentropy + wisp_relay components
- [ ] Verify build compiles (may not link yet)

### Phase 2: Port Wisp Relay Core
- [ ] Create `components/wisp_relay/` directory
- [ ] Port `ws_server.c/h` (minimal adaptation, port 4869)
- [ ] Port `storage_engine.c/h` (partition label → `"relay_store"`, LittleFS)
- [ ] Port `sub_manager.c/h` (as-is)
- [ ] Port `broadcaster.c/h` (as-is)
- [ ] Port `rate_limiter.c/h` (as-is)
- [ ] Port `nip11.c/h` (customize NIP-11 JSON for TollGate)
- [ ] Port `deletion.c/h` (as-is)
- [ ] Port `flash_monitor.c/h` (as-is)
- [ ] Port `relay_core.h` (adapt types)
- [ ] Create `components/wisp_relay/CMakeLists.txt`
- [ ] Verify relay component compiles

### Phase 3: Rewrite Validator & Router
- [ ] Write `relay_validator.c/h` using TollGate's secp256k1 + mbedtls
  - [ ] Event ID verification (SHA-256 of serialized event)
  - [ ] Schnorr signature verification (secp256k1_schnorrsig_verify)
  - [ ] Event age check, expiration check
- [ ] Write `router.c/h` using TollGate's cJSON (not libnostr-c)
  - [ ] Parse CLIENT messages (EVENT, REQ, CLOSE)
  - [ ] Dispatch to handlers
  - [ ] Serialize relay messages (OK, EVENT, EOSE, NOTICE, CLOSED)
- [ ] Write `handlers.c` (event handler, REQ handler, CLOSE handler)

### Phase 4: Local-First Publishing
- [ ] Create `main/local_relay.c/h` — thin wrapper
- [ ] Modify `wifistr.c` — publish to local relay only
- [ ] Modify `cvm_server.c` — store CEP-6 announcements locally
- [ ] Modify `tollgate_main.c` — init local relay in `start_services()`

### Phase 5: Relay Selector (NIP-11)
- [ ] Create `main/relay_selector.c/h`
- [ ] Implement NIP-11 HTTP probe (esp_http_client + Accept header)
- [ ] Implement relay scoring (latency + NIP-77 bonus)
- [ ] Implement selection: primary (best NIP-77) + fallbacks
- [ ] Implement auto-failover (3 disconnects → re-probe + switch)
- [ ] Implement periodic re-probe (every 6h)
- [ ] Add seed relay config to `config.c/h`

### Phase 6: CVM Dynamic Relay
- [ ] Modify `cvm_server.c` to connect to `primary_relay` from selector
- [ ] Keep real-time publish via persistent WS for CVM responses
- [ ] Also store CVM responses in local relay
- [ ] Handle relay switch (disconnect old, connect new)

### Phase 7: Negentropy Sync Manager
- [ ] Create `main/negentropy_storage.c/h` — adapter from wisp storage to negentropy API
- [ ] Create `main/sync_manager.c/h`
- [ ] Implement negentropy sync with primary relay (every 30min)
  - [ ] Build negentropy set from local relay events
  - [ ] Open WS to primary relay
  - [ ] Run negentropy protocol (NEG_OPEN/NEG_MSG)
  - [ ] Re-publish missing events
- [ ] Implement REQ-diff fallback with fallback relays (every 6h)
  - [ ] REQ own events from public relay
  - [ ] Diff event IDs against local relay
  - [ ] Re-publish missing events
- [ ] Implement on-reconnect immediate sync trigger

### Phase 8: Tests
- [ ] Unit test: relay validator (event ID + Schnorr verify with known vectors)
- [ ] Unit test: negentropy sync logic (mock ID sets)
- [ ] Unit test: relay selector scoring
- [ ] Integration test: local relay (publish + subscribe via `nak`)
- [ ] Integration test: negentropy sync (local → orangesync)
- [ ] Integration test: REQ-diff fallback
- [ ] Integration test: CVM through local relay
- [ ] E2E test: CVM tool call via relay

### Phase 9: Documentation
- [ ] Update AGENTS.md with local relay info
- [ ] Update config format documentation
- [ ] Add relay selection documentation
- [ ] Update test instructions
