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

## Board Port Mapping (verified 2026-06-11)

| Board | MAC | Port |
|-------|-----|------|
| A (working NerdAxe) | `80:b5:4e:c7:7a:d0` | `/dev/ttyACM1` |
| B (fan-damaged) | `80:b5:4e:c7:79:88` | `/dev/ttyACM0` |

**IMPORTANT:** Ports change on every USB replug. Always verify with `esptool --port <port> chip-id`.

---

## Phase 0: Code Fixes — COMPLETED

- [x] **0a. Fix sw_miner task creation** (`sw_miner.c`) — committed `fb2d51e`
- [x] **0b. Fix nucula_wallet_send unit handling** (`nucula_wallet.cpp`) — committed `fb2d51e`
- [x] **0c. Add unit to discovery response** (`tollgate_api.c`) — committed `fb2d51e`
- [x] **0d. Parse unit in client discovery** (`tollgate_core_client.h/c`) — committed `fb2d51e`
- [x] **0e. Unit tests** — committed `fb2d51e`, all passing

### Uncommitted fixes (in working tree)

- [x] **0f. Fix faucet_client task creation** (`faucet_client.c`) — PSRAM stack, same as sw_miner
- [x] **0g. Skip Lightning payout for non-sat wallets** (`lightning_payout.c`) — hashpool doesn't support Lightning
- [x] **0h. Add B2B Makefile targets** (`Makefile`) — `write-b2b-config-a`, `write-b2b-config-b`

---

## Phase 1: Wallet Storage Migration (SPIFFS)

### Problem

NVS blobs are capped at ~4KB. Each proof is ~140 bytes serialized. After ~28 proofs the blob overflows: `save_proofs failed: ESP_ERR_NVS_NOT_ENOUGH_SPACE`. The faucet adds proofs every 120s, so this hits in under an hour.

### Solution: Move all wallet data (except seed) to SPIFFS files

SPIFFS partition is 960KB at `/spiffs`, already mounted by `config.c`. Currently only holds `config.json` (~1KB).

#### File layout

```
/spiffs/
  config.json                  (existing)
  wallet/
    0/
      url.txt                  (mint URL)
      proofs.json              (proofs array — unbounded, no NVS limit)
      keysets.json             (keyset array)
      counters.json            ({"keyset_id": counter, ...})
    1/
      url.txt
      proofs.json
      keysets.json
      counters.json
```

#### What stays in NVS

Only `seed` and `mnemonic` — tiny, written once, security-sensitive.

#### Migration path

On boot, `load_proofs()` / `load_keysets_nvs()` try file first, fall back to NVS. If NVS had data, write to file immediately and erase NVS entry. Subsequent boots always read from file.

#### Power-fail safety

Write to temp file → fsync → atomic rename. Never lose the old file if power cuts mid-write.

### Checklist

- [ ] **1a. Add file I/O helpers to wallet.cpp** — `ensure_dir()`, `read_file()`, `write_file_atomic()`
- [ ] **1b. Migrate `save_proofs()`** — write to `/spiffs/wallet/<slot>/proofs.json` via temp+rename
- [ ] **1c. Migrate `load_proofs()`** — try file first, fall back to NVS, auto-migrate
- [ ] **1d. Migrate `save_keysets()`** — write to `/spiffs/wallet/<slot>/keysets.json`
- [ ] **1e. Migrate `load_keysets_nvs()`** — try file first, fall back to NVS, auto-migrate
- [ ] **1f. Migrate `save_mint_url()` / `load_mint_url_for_slot()`** — file-based
- [ ] **1g. Migrate `save_counter()` / `load_counter()`** — file-based (counters.json)
- [ ] **1h. Remove NVS dependency** for wallet data (keep only seed/mnemonic)
- [ ] **1i. Test: `make test-unit`** passes
- [ ] **1j. Test: build firmware** compiles
- [ ] **1k. Commit**

---

## Phase 2: Config & Flash

- [ ] **2a. Commit uncommitted Phase 0 fixes** (faucet PSRAM, payout guard, Makefile targets)
- [ ] **2b. Verify board ports** with `esptool --port <port> chip-id`
- [ ] **2c. Erase NVS on both boards** — `esptool erase_region 0x9000 0x6000` (clears old wallet proofs that would conflict with file migration)
- [ ] **2d. Flash Board A** with new firmware + `make write-b2b-config-a`
- [ ] **2e. Flash Board B** with new firmware + `make write-b2b-config-b`
- [ ] **2f. Verify Board A**: boots, connects to studio, faucet works, balance grows, proofs save to file
- [ ] **2g. Verify Board B**: boots, connects to studio, faucet works, balance grows

---

## Phase 3: Hardware Test — Board B pays Board A

- [ ] **3a. Board B STA connects to Board A's AP** (TollGate-B96D80)
- [ ] **3b. `tollgate_client_on_sta_connected()` fires** — detects TollGate at gateway
- [ ] **3c. Board B pays Board A** — creates 21 ehash token, POSTs to Board A's :2121
- [ ] **3d. Board A validates mint, creates session, absorbs token**
- [ ] **3e. Verify settlement loop** — Board A balance +21, Board B balance -21, session renews

---

## Phase 4: Integration Test

- [ ] **4a. Write `tests/integration/b2b_settlement.mjs`**
- [ ] **4b. Run and verify pass**

---

## Key Config

| | Board A (upstream) | Board B (downstream) |
|---|---|---|
| NSEC | `9af47906b45aca5e238390f3d03c8274e154198e81aa2095065627d1e61ca968` | `a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2` |
| SSID | TollGate-B96D80 | TollGate-C0E9CA |
| AP IP | 10.185.47.1 | 10.192.45.1 |
| STA connects to | studio | TollGate-B96D80 (primary), studio (fallback) |
| client_enabled | false | true |
| mint_url | http://66.92.204.38:3338 | http://66.92.204.38:3338 |
| accepted_mints | [http://66.92.204.38:3338] | [http://66.92.204.38:3338] |
| faucet_url | http://66.92.204.38:8083/mint/tokens | http://66.92.204.38:8083/mint/tokens |
| price_per_step | 21 | 21 |
| step_size_ms | 60000 | 60000 |

## VPS Services

| Service | URL | Purpose |
|---------|-----|---------|
| Hashpool mint | http://66.92.204.38:3338 | ehash mint, unit="hash" |
| Faucet API | http://66.92.204.38:8083/mint/tokens | POST {"amount":10} → cashuA token |
| Stratum | 66.92.204.38:34255 | SV2 mining proxy |
