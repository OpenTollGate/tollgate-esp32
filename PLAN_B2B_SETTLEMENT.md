# Board-to-Board ehash Settlement Plan

## Overview

Two ESP32 boards settle internet access payments using hashpool ehash tokens. Board A is upstream (has internet via WiFi router). Board B connects to Board A's AP, mines through Board A's HTTP API to earn initial internet time, faucets ehash from VPS, then pays for ongoing access with ehash tokens.

## Network Topology

```
Internet ← "EnterSSID-2.4GHz" ← Board A (AP: TollGate-B96D80, 10.185.47.1)
                 (ch10, WPA2)       ↑ runs stratum proxy + sw_miner + faucet
                                    ↑ sandbox allows mining API for unauthenticated clients
                                    ↑
                          Board B STA connects here
                                    │
                              Board B (AP: TollGate-C0E9CA, 10.192.45.1)
                              STA → Board A's AP (only, no fallback)
                              Remote miner → Board A's /mining/job + /mining/share
                              Earns internet via mining → faucets ehash → pays with ehash
```

## Pre-Funding: Remote Miner

Board B cannot faucet ehash without internet. It cannot pay for internet without ehash. Solution:

1. Board B connects to Board A's AP (no password, open)
2. Board B's **remote miner** task starts automatically (`client_enabled: true`)
3. Remote miner polls `GET http://<gw>:2121/mining/job` → gets block template
4. Remote miner runs `sha256d` locally on 1000 nonces per iteration
5. Remote miner posts valid shares to `POST http://<gw>:2121/mining/share`
6. Board A forwards shares to upstream stratum pool
7. On accepted share: Board A creates mining session for Board B's IP → internet access granted
8. Board B's faucet runs → accumulates ehash from VPS
9. Board B pays Board A with ehash token → ehash-based session replaces mining session
10. Remote miner continues running as backup (always-on per user preference)

### Why this works

The sandbox firewall (`is_sandbox_allowed()` in `tollgate_core_firewall.c:86`) allows **unauthenticated** clients to access:
- Port 2121 (`/mining/job`, `/mining/share`) — mining API
- Port 3334 (stratum proxy TCP)
- Port 80 (captive portal), 4869 (local relay)
- All UDP (DNS), all ICMP (ping)

No code changes needed on Board A — the infrastructure already supports remote miners.

## Board Port Mapping

| Board | MAC | Port |
|-------|-----|------|
| A (working NerdAxe) | `80:b5:4e:c7:7a:d0` | verify before flash |
| B (fan-damaged) | `80:b5:4e:c7:79:88` | verify before flash |

**IMPORTANT:** Ports change on every USB replug. Always verify with `esptool --port <port> chip-id`.

---

## Phase 0: Code Fixes — COMPLETED ✓

- [x] **0a. Fix sw_miner task creation** (`sw_miner.c`) — committed `fb2d51e`
- [x] **0b. Fix nucula_wallet_send unit handling** (`nucula_wallet.cpp`) — committed `fb2d51e`
- [x] **0c. Add unit to discovery response** (`tollgate_api.c`) — committed `fb2d51e`
- [x] **0d. Parse unit in client discovery** (`tollgate_core_client.h/c`) — committed `fb2d51e`
- [x] **0e. Unit tests** — committed `fb2d51e`, all passing
- [x] **0f. Fix faucet_client task creation** (`faucet_client.c`) — PSRAM stack
- [x] **0g. Skip Lightning payout for non-sat wallets** (`lightning_payout.c`)
- [x] **0h. Add B2B Makefile targets** (`Makefile`)

---

## Phase 1: Wallet Storage Migration (SPIFFS) — COMPLETED ✓

- [x] All wallet data (proofs, keysets, counters, mint_url) moved from NVS to SPIFFS files
- [x] Seed/mnemonic stay in NVS
- [x] Atomic write-then-rename for power-fail safety
- [x] Auto-migration from NVS on first load
- [x] Committed `769f5e3`

---

## Phase 2: Remote Miner Implementation

### New Files

- `main/remote_miner.c` — HTTP-based remote miner task
- `main/remote_miner.h` — public API

### Design

```
remote_miner_task (PSRAM stack, 4096 words):
  loop:
    GET http://<gw_ip>:2121/mining/job → {job_id, prevhash, merkle_root, version, nbits, ntime}
    cache job locally
    for nonce in random_range(1000):
      build 80-byte header
      sha256d(header) → hash
      if hash <= target:
        POST http://<gw_ip>:2121/mining/share {job_id, nonce, ntime, version}
        log accepted/rejected
    yield
```

### API

```c
esp_err_t remote_miner_start(const char *gw_ip);  // starts task, gw_ip copied
void remote_miner_stop(void);                      // stops task
bool remote_miner_is_running(void);
double remote_miner_get_hashrate(void);
```

### Integration Points

- `tollgate_client.c:tollgate_client_on_sta_connected()` → start remote miner after STA connects
- `tollgate_client.c:tollgate_client_on_sta_disconnected()` → stop remote miner
- Remote miner runs continuously regardless of payment state (mining session is independent)
- Board B config: `mining.enabled: false` (no local stratum), remote miner handles everything

### Checklist

- [x] **2a. Create `main/remote_miner.h`** — public API header
- [x] **2b. Create `main/remote_miner.c`** — remote HTTP miner implementation
- [x] **2c. Update `main/tollgate_client.c`** — start/stop remote miner on STA connect/disconnect
- [x] **2d. Update `main/CMakeLists.txt`** — add `remote_miner.c` to SRCS
- [x] **2e. Write `tests/unit/test_remote_miner.c`** — 86 tests: nbits→target, header layout, sha256d, genesis block POW
- [x] **2f. Update `tests/unit/Makefile`** — add test target + stubs for tollgate_client tests
- [x] **2g. Run `make test-unit`** — all 29 test files pass (790+ cases, 0 failures)
- [x] **2h. Build firmware** — builds cleanly, no warnings
- [x] **2i. Commit** (committed `619b5b8`)

---

## Phase 2b: Bug Fixes (Pre-Flash Stability)

### Bug 1: Price Parsing — Mining Tag Overwrites Cashu Values

**Root cause:** `tollgate_core_client.c:49` checks `tag[2]` for "mining" but payment type is at `tag[1]`.

Board A's actual discovery response:
- Cashu tag: `["price_per_step", "cashu", "21", "hash", "http://...", "1"]` — tag[1]="cashu"
- Mining tag: `["price_per_step", "mining", "3334", "GH/s", "sv1"]` — tag[1]="mining"

Parser reads tag[2] as payment_type. For mining tag, tag[2]="3334" ≠ "mining" → falls into else → parses "3334" as price, "GH/s" as unit, "sv1" as mint → **overwrites correct cashu values**.

Board B sees price=3334, mint=sv1 instead of price=21, mint=http://66.92.204.38:3338.

**Impact:** Board B cannot parse correct price or mint URL from Board A's discovery → payment fails.

### Bug 2: Board B Crash — Concurrent SPIFFS Access + PSRAM Cache

**Root cause:** No mutex protection on `nucula_wallet_*` calls. Concurrent wallet operations (mint_health's `nucula_wallet_receive` from faucet + tollgate_client's `nucula_wallet_send` for payment) cause concurrent SPIFFS flash writes. ESP32 disables flash cache during erase/write → if another task's code is executing from PSRAM stack → `Cache disabled but cached memory region accessed`.

Contributing factors:
- Remote miner starts on STA-connect event, before `start_services()` finishes → races wallet init
- Task stacks on PSRAM (`MALLOC_CAP_SPIRAM`) → code on PSRAM is vulnerable to cache-disable
- No SPIFFS corruption recovery → crash during SPIFFS write → corrupt metadata → boot loop

**Impact:** Board B crashes ~31s after boot, corrupts SPIFFS, enters boot loop.

### Checklist

- [x] **2b-1. Fix price parsing**: `tollgate_core_client.c:49` — check `tag[1]` not `tag[2]`
- [x] **2b-2. Update mining test**: `test_client_core.c:44` — use real format `["price_per_step","mining","3333","GH/s","sv1"]`
- [x] **2b-3. Add both-tags test**: cashu + mining tags together, verify cashu values not overwritten
- [x] **2b-4. Add wallet mutex**: `nucula_wallet.cpp` — RAII WalletLock wraps all public API calls
- [x] **2b-5. Internal RAM stacks**: `remote_miner.c` + `faucet_client.c` — `MALLOC_CAP_INTERNAL` instead of SPIRAM
- [x] **2b-6. Delay remote miner**: `tollgate_client.c` — start after discovery succeeds, not before
- [x] **2b-7. SPIFFS corruption recovery**: `wallet.cpp` — delete corrupted proofs file on JSON parse failure
- [x] **2b-8. Run `make test-unit`** — all 30 test files pass (800+ cases, 0 failures)
- [x] **2b-9. Build firmware** — clean build, no warnings
- [x] **2b-10. Commit** all bug fixes (committed `9258dc4`, pushed to orangesync)
- [x] **2b-11. Fix stack overflow** — `token_buf[8192]` moved from stack to heap (committed `179f1f0`)
- [x] **2b-12. PSRAM CAPS_ALLOC mode** — `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` to keep SPIFFS in internal RAM (committed `179f1f0`)

---

## Phase 3: Config & Flash

- [x] **3a. Update `.env`** — `WIFI_SSID=EnterSSID-2.4GHz`, `WIFI_PASSWORD=c03rad0r123!`
- [x] **3b. Update `Makefile` B2B Board B config** — remove upstream WiFi fallback, only `TollGate-B96D80`
- [x] **3c. Update `Makefile` B2B Board B config** — `mining.enabled: false` (remote miner instead)
- [x] **3d-extra. Move `faucet_client_start()` out of `mining_enabled` block** — faucet now runs when `faucet_url` is set regardless of mining
- [x] **3d-extra2. Open AP auth fix** — `config.c` sets `WIFI_AUTH_OPEN` when password is empty (committed `45dd10d`)
- [x] **3e. Board A verified working**: boots, connects to EnterSSID-2.4GHz (192.168.2.28), faucet accumulates ehash, mining active, API at :2121
- [x] **3f. Board B connects to Board A's AP**: DHCP IP 10.185.47.2, remote miner starts, TollGate detected
- [x] **3g. Re-flash both boards** with bug fixes (Phase 2b + stack overflow + PSRAM CAPS_ALLOC)
- [x] **3h. Verify Board B no longer crashes** after 31s — STABLE 90s+ (committed `179f1f0`)
- [x] **3i. Verify Board B faucet**: received 32 ehash at 43s uptime
- [x] **3j. Verify price parsing**: Board B sees price=21, mint=http://66.92.204.38:3338 ✓

---

## Phase 4: Hardware Test — Board B pays Board A

- [x] **4a. Board B detects TollGate** at gateway 10.185.47.1 ✓
- [ ] **4b. Board B pays Board A** — creates 21 ehash token, POSTs to Board A's :2121
- [ ] **4c. Board A validates mint, creates session, absorbs token**
- [ ] **4d. Verify settlement loop** — Board A balance +21, Board B balance -21, session renews
- [ ] **4e. Verify remote miner still running** — backup internet path

---

## Phase 5: Integration Test

- [ ] **5a. Write `tests/integration/b2b_settlement.mjs`**
- [ ] **5b. Run and verify pass**

---

## Key Config

| | Board A (upstream) | Board B (downstream) |
|---|---|---|
| NSEC | `9af47906...` | `a1b2c3d4...` |
| SSID | TollGate-B96D80 | TollGate-C0E9CA |
| AP IP | 10.185.47.1 | 10.192.45.1 |
| STA connects to | EnterSSID-2.4GHz (WPA2) | TollGate-B96D80 (open, no fallback) |
| client_enabled | false | true |
| mining.enabled | true (local stratum + sw_miner) | false (remote miner instead) |
| remote_miner | N/A | auto-starts on STA connect |
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
