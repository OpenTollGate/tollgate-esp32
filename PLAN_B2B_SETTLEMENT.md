# Board-to-Board ehash Settlement Plan

## Overview

Two ESP32 boards settle internet access payments using hashpool ehash tokens. Board A is upstream (has internet via "studio" WiFi). Board B connects to Board A's AP and pays for internet access with ehash from its wallet. Both boards faucet ehash from the VPS.

## Network Topology

```
Internet ← "studio" WiFi ← Board A (AP: TollGate-B96D80, 10.185.47.1)
                                    ↑
                          Board B STA connects here
                                    │
                              Board B (AP: TollGate-C0E9CA, 10.192.45.1)
                              STA → Board A's AP for upstream
                              Faucets from VPS via Board A's internet
```

## Phase 0: Code Fixes

- [ ] **0a. Fix sw_miner task creation** (`sw_miner.c`)
  - Replace `xTaskCreate()` with `xTaskCreateStatic()` + PSRAM-allocated stack
  - Stack from `heap_caps_malloc(4096, MALLOC_CAP_SPIRAM)`

- [ ] **0b. Fix nucula_wallet_send unit handling** (`nucula_wallet.cpp`)
  - Derive unit from wallet's active keyset instead of hardcoding "sat"
  - Pass derived unit to `w->swap()` call
  - Set `token.unit` to derived unit instead of "sat"

- [ ] **0c. Add unit to discovery response** (`tollgate_api.c`)
  - Change hardcoded "sat" in price_per_step tag to wallet's actual unit
  - Need to query wallet unit from loaded keysets

- [ ] **0d. Parse unit in client discovery** (`tollgate_core_client.h/c`)
  - Add `char unit[16]` to `tollgate_discovery_t`
  - Parse from price_per_step tag index 3
  - Default to "sat" if absent (backward compat)

- [ ] **0e. Unit tests for new behavior**
  - Test parse_discovery with unit field
  - Test parse_discovery without unit field
  - Run `make test-unit`

## Phase 1: Config & Flash

- [ ] **1a. Add Makefile targets** (`Makefile`)
  - `write-b2b-config-a` — Board A upstream config
  - `write-b2b-config-b` — Board B downstream config with client_enabled

- [ ] **1b. Flash Board A** with B2B config
  - Verify: boots, connects to studio, faucets ehash, balance grows
  - Verify: `GET /2121/` returns discovery with unit="hash"

- [ ] **1c. Flash Board B** with B2B config
  - Verify: connects to studio first, faucets ehash
  - Verify: accumulates >= 42 ehash (2 steps worth)

## Phase 2: Hardware Test

- [ ] **2a. Board B connects to Board A's AP**
  - Board B STA connects to TollGate-B96D80
  - `tollgate_client_on_sta_connected()` fires
  - Discovery detects TollGate at gateway

- [ ] **2b. Board B pays Board A**
  - `tollgate_client_pay()` creates 21 ehash token
  - POSTs to Board A's :2121
  - Board A validates mint, creates session, absorbs token
  - Serial log: "payment accepted: allotment=60000ms"

- [ ] **2c. Verify settlement loop**
  - Board A balance increases by 21 ehash
  - Board B balance decreases by 21 ehash
  - Session renews when remaining < 20%
  - Both faucets continue polling

## Phase 3: Integration Test

- [ ] **3a. Write `tests/integration/b2b_settlement.mjs`**
- [ ] **3b. Run and verify pass**

## Key Config

| | Board A (upstream) | Board B (downstream) |
|---|---|---|
| NSEC | `9af479...ca968` | `a1b2c3...a1b2` |
| SSID | TollGate-B96D80 | TollGate-C0E9CA |
| AP IP | 10.185.47.1 | 10.192.45.1 |
| STA connects to | studio | TollGate-B96D80 (primary), studio (fallback) |
| client_enabled | false | true |
| mint_url | http://66.92.204.38:3338 | http://66.92.204.38:3338 |
| accepted_mints | [http://66.92.204.38:3338] | [http://66.92.204.38:3338] |
| faucet_url | http://66.92.204.38:8083/mint/tokens | http://66.92.204.38:8083/mint/tokens |
| price_per_step | 21 | 21 |
| step_size_ms | 60000 | 60000 |
