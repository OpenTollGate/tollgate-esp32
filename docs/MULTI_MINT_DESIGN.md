# Multi-Mint Support — Design Document

**Branch**: `feature/multi-mint-support`
**Date**: 2026-05-18
**Status**: Implementation Phase

---

## 1. Overview

Extend the ESP32 TollGate firmware to accept Cashu ecash payments from **multiple mints** instead of a single hardcoded mint URL. The system must:

- Accept tokens from any of 4 configured mints
- Track mint reachability via periodic health probes
- Only accept payments from mints that are currently reachable (successful swap)
- Expose all reachable mints in the discovery endpoint and captive portal
- Manage per-mint wallets with independent keysets and proof storage

### Supported Mints

| Mint | URL |
|------|-----|
| Minibits | `https://mint.minibits.cash/Bitcoin` |
| CoinOS | `https://mint.coinos.io` |
| 21mint | `https://21mint.me` |
| LNVoltz | `https://mint.lnvoltz.com` |

All verified reachable via `GET /v1/info` (HTTP 200).

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────┐
│                    config.json                       │
│  "accepted_mints": ["url1", "url2", "url3", "url4"] │
└──────────────────────┬──────────────────────────────┘
                       │
          ┌────────────┼────────────────┐
          ▼            ▼                ▼
   ┌──────────┐  ┌──────────┐   ┌───────────────┐
   │  Config   │  │  Health  │   │  Multi-Wallet │
   │  Layer    │  │  Tracker │   │  (Nucula)     │
   │           │  │          │   │               │
   │ accepted_ │  │ probe    │   │ Wallet[0] →   │
   │ mints[]   │  │ every    │   │  mint A       │
   │           │  │ 5min     │   │ Wallet[1] →   │
   │           │  │          │   │  mint B       │
   │           │  │ recovery │   │ ...           │
   │           │  │ thresh=3 │   │               │
   └─────┬─────┘  └────┬─────┘   └───────┬───────┘
         │              │                  │
         ▼              ▼                  ▼
   ┌─────────────────────────────────────────────────┐
   │              cashu_is_mint_accepted()            │
   │  in config AND reachable → accept               │
   └────────────────────┬────────────────────────────┘
                        │
          ┌─────────────┼──────────────┐
          ▼             ▼              ▼
   ┌──────────┐  ┌───────────┐  ┌───────────┐
   │Discovery │  │  Captive  │  │  Payment  │
   │ Endpoint │  │  Portal   │  │  Handler  │
   │          │  │           │  │           │
   │ 1 tag    │  │ mint list │  │ find right│
   │ per      │  │ with      │  │ wallet,   │
   │ reachable│  │ indicators│  │ receive() │
   │ mint     │  │           │  │           │
   └──────────┘  └───────────┘  └───────────┘
```

---

## 3. Phase Details

### Phase 1: Config Layer — Multi-Mint Array

**Files**: `main/config.h`, `main/config.c`

**Changes**:

- Increase `TOLLGATE_MAX_MINT_URLS` from `3` to `8`
- Add to `tollgate_config_t`:
  ```c
  char accepted_mints[TOLLGATE_MAX_MINT_URLS][256];
  int accepted_mint_count;
  ```
- Keep existing `mint_url[256]` for backward compatibility
- Parse new `"accepted_mints"` JSON array from config.json
- If `"accepted_mints"` absent, populate from `"mint_url"` (backward compat)
- Update default config.json generation to include `"accepted_mints"`

**Config.json format** (new):
```json
{
  "nsec": "...",
  "accepted_mints": [
    "https://mint.minibits.cash/Bitcoin",
    "https://mint.coinos.io",
    "https://21mint.me",
    "https://mint.lnvoltz.com"
  ],
  "mint_url": "https://mint.minibits.cash/Bitcoin"
}
```

The `"mint_url"` field is kept as fallback / primary mint identifier.

---

### Phase 2: Mint Acceptance — Multi-Mint Check

**Files**: `main/cashu.c`, `main/cashu.h`

Replace single-mint check in `cashu_is_mint_accepted()`:

```c
bool cashu_is_mint_accepted(const char *mint_url) {
    if (!mint_url || mint_url[0] == '\0') return false;
    const tollgate_config_t *cfg = tollgate_config_get();
    for (int i = 0; i < cfg->accepted_mint_count; i++) {
        if (strstr(mint_url, cfg->accepted_mints[i]) != NULL)
            return true;
    }
    return false;
}
```

This is the config-only check. Phase 4 adds health gating.

---

### Phase 3: Mint Health Tracker

**New files**: `main/mint_health.h`, `main/mint_health.c`

**Data structures**:

```c
#define MINT_HEALTH_MAX 8
#define MINT_HEALTH_PROBE_INTERVAL_S  300
#define MINT_HEALTH_PROBE_TIMEOUT_MS  15000
#define MINT_HEALTH_RECOVERY_THRESHOLD 3

typedef struct {
    char url[256];
    bool reachable;
    uint8_t consecutive_successes;
    int64_t last_probe_ms;
    int last_http_status;
} mint_status_t;

typedef void (*mint_health_changed_cb)(void);
```

**Public API**:

```c
esp_err_t mint_health_init(const char urls[][256], int count);
void mint_health_start(void);
void mint_health_stop(void);
const mint_status_t *mint_health_get_all(int *out_count);
bool mint_health_is_reachable(const char *url);
void mint_health_mark_unreachable(const char *url);
void mint_health_register_callback(mint_health_changed_cb cb);
```

**Probing logic** (FreeRTOS task):

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Endpoint | `GET {url}/v1/info` | Lightweight, no auth required |
| Timeout | 15 seconds | ESP32 resource-constrained, 30s too long |
| Interval | 5 minutes (`vTaskDelay`) | Matches Go reference |
| Failure | Immediate | Single failed probe → unreachable |
| Recovery | 3 consecutive successes | 15 min sustained health (matches Go) |
| Initial | Success → reachable immediately | Set `consecutive_successes = threshold` |

**Thread safety**: Single FreeRTOS mutex protecting the status array. Callbacks dispatched after releasing the mutex.

**Reference**: Modeled after Go `MintHealthTracker` in `tollgate-module-basic-go/src/merchant/mint_health_tracker.go`.

---

### Phase 4: Health-Aware Acceptance

**Files**: `main/cashu.c`

Update `cashu_is_mint_accepted()` to gate on health:

```c
bool cashu_is_mint_accepted(const char *mint_url) {
    if (!mint_url || mint_url[0] == '\0') return false;
    const tollgate_config_t *cfg = tollgate_config_get();
    for (int i = 0; i < cfg->accepted_mint_count; i++) {
        if (strstr(mint_url, cfg->accepted_mints[i]) != NULL)
            return mint_health_is_reachable(mint_url);
    }
    return false;
}
```

On cold start with no internet: no mints reachable → no tokens accepted (matches Go degraded behavior). Once first probe succeeds, that mint becomes reachable and tokens are accepted.

---

### Phase 5: Multi-Mint Discovery Endpoint

**File**: `main/tollgate_api.c`

Replace single `price_per_step` tag in `api_get_discovery()` with one per reachable mint:

```c
int count;
const mint_status_t *mints = mint_health_get_all(&count);
for (int i = 0; i < count; i++) {
    if (!mints[i].reachable) continue;
    cJSON *price_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("price_per_step"));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("cashu"));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString(price_str));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("sat"));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString(mints[i].url));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("1"));
    cJSON_AddItemToArray(tags, price_tag);
}
```

If no mints are reachable, include a single tag with the primary `mint_url` as fallback (degraded mode signal).

---

### Phase 6: Multi-Mint Captive Portal UI

**File**: `main/captive_portal.c`

**Changes**:

1. Replace `__MINT_URL__` template placeholder with `__MINT_LIST__`
2. Generate HTML list of reachable mints with green dot indicators
3. Unreachable mints shown greyed out (informative but not selectable)
4. New API endpoint `GET /api/mints` → JSON array of mint status

**Portal mint list HTML**:
```html
<div class="mints">
  <div class="mints-title">SUPPORTED MINTS</div>
  <div class="mint-item reachable">
    <span class="mint-dot green"></span>
    <span class="mint-url">mint.minibits.cash/Bitcoin</span>
  </div>
  <div class="mint-item unreachable">
    <span class="mint-dot grey"></span>
    <span class="mint-url">mint.coinos.io</span>
  </div>
</div>
```

**Auto-refresh**: JS polls `GET /api/mints` every 30s to update indicators.

---

### Phase 7: Multi-Mint Wallet (Nucula)

**Files**: `components/nucula_lib/nucula_wallet.h`, `components/nucula_lib/nucula_wallet.cpp`

**Approach**: Multi-wallet — one `cashu::Wallet` instance per mint.

**Why multi-wallet vs refactoring Wallet class**:
- Each mint has its own keysets, proofs, NVS slot — natural isolation
- No risk of cross-mint proof confusion
- `cashu::Wallet` class unchanged — zero regression risk
- NVS slot allocation already supported: `Wallet(url, ctx, nvs_slot)`
- `MAX_MINTS = 3` constant already defined in `wallet.hpp`

**Internal structure**:
```cpp
static const int MAX_WALLETS = 4;
static cashu::Wallet *s_wallets[MAX_WALLETS];
static int s_wallet_count = 0;
```

**API changes**:

| Old | New | Behavior |
|-----|-----|----------|
| `nucula_wallet_init(url)` | `nucula_wallet_init_multi(urls, count)` | Create wallet per mint |
| `nucula_wallet_init(url)` | Keep as compat wrapper | Creates single-wallet array |
| `nucula_wallet_receive(token)` | Same | Decode mint from token, route to correct wallet |
| `nucula_wallet_balance()` | Same | Sum across all wallets |
| `nucula_wallet_send(amount, ...)` | Same | Select wallet with sufficient balance |
| `nucula_wallet_swap_all()` | Same | Swap all wallets |
| `nucula_wallet_proof_count()` | Same | Sum across all wallets |

**Token routing in `receive()`**:
1. Decode token to extract `mint_url` from the token JSON
2. Find matching wallet by URL
3. Call `wallet->receive(token, proofs_out)` on that wallet
4. If no matching wallet found, try first wallet as fallback

**NVS slot mapping**:

| Mint index | NVS slot | NVS keys |
|-----------|----------|----------|
| 0 | 0 | `url_0`, `proofs_0`, `kn_0`, `k_0_0`..`k_0_9` |
| 1 | 1 | `url_1`, `proofs_1`, `kn_1`, `k_1_0`..`k_1_9` |
| 2 | 2 | `url_2`, `proofs_2`, `kn_2`, `k_2_0`..`k_2_9` |
| 3 | 3 | `url_3`, `proofs_3`, `kn_3`, `k_3_0`..`k_3_9` |

---

### Phase 8: Service Startup Integration

**File**: `main/tollgate_main.c`

**Changes to `start_services()`**:

```
1. firewall_init()
2. session_manager_init()
3. mint_health_init(cfg->accepted_mints, cfg->accepted_mint_count)
4. mint_health_start()  ← async probing begins
5. nucula_wallet_init_multi(cfg->accepted_mints, cfg->accepted_mint_count)
6. lightning_payout_init()
7. dns_server_start()
8. captive_portal_start()
9. tollgate_api_start()
10. wifistr_publish()
11. cvm_server_start()
```

**Health callback**: When reachable set changes, trigger wifistr re-publish to update Nostr kind 38787 event with current mint list.

---

## 4. Data Flow

### Payment Flow (Multi-Mint)

```
Client POST cashuA token
  │
  ▼
api_post_payment()
  ├── cashu_decode_token() → extract mint_url from token
  ├── cashu_is_mint_accepted(mint_url)
  │     ├── Check in cfg->accepted_mints[] → config match
  │     └── Check mint_health_is_reachable(mint_url) → health gate
  ├── cashu_check_proof_states(mint_url, token) → POST {mint_url}/v1/checkstate
  ├── session_create(client_ip, allotment)
  └── nucula_wallet_receive(token_str)
        ├── Decode token → extract mint_url
        ├── Find wallet for that mint_url
        └── wallet->receive(token, proofs_out)
```

### Health Probe Flow

```
mint_health_task (FreeRTOS, 5min interval)
  │
  for each mint in accepted_mints[]:
  │
  ├── GET {url}/v1/info (15s timeout)
  │
  ├── Success?
  │     ├── YES → consecutive_successes++
  │     │         if >= RECOVERY_THRESHOLD → mark reachable
  │     └── NO  → mark unreachable, reset consecutive_successes = 0
  │
  └── Reachable set changed? → fire callback
```

---

## 5. Error Handling

| Scenario | Behavior |
|----------|----------|
| No internet at boot | No mints reachable, no tokens accepted until probe succeeds |
| All mints unreachable | Discovery shows primary mint (degraded), portal shows "Checking mints..." |
| Mint goes down mid-operation | `cashu_check_proof_states` fails → 502 Bad Gateway to client |
| Wallet init fails for one mint | Skip that mint, log error, continue with others |
| NVS full for multi-wallet | Fallback to single wallet, log warning |
| Probe timeout | Treat as unreachable (same as connection refused) |

---

## 6. Memory Budget

| Component | Estimated RAM | Notes |
|-----------|--------------|-------|
| `mint_status_t[8]` | ~2 KB | 256-byte URLs + metadata |
| Health probe task stack | 8 KB | HTTP client needs stack |
| `cashu::Wallet` per mint | ~4 KB each | Keysets + proofs in NVS, not RAM |
| 4 wallets total | ~16 KB | Within ESP32-S3 512KB SRAM budget |
| Health task TLS | ~40 KB | esp_http_client TLS buffer |
| **Total new overhead** | **~66 KB** | Acceptable with 512KB SRAM + 8MB PSRAM |

---

## 7. Testing Strategy

### Unit Tests (host, `tests/unit/`)

| Test File | Covers |
|-----------|--------|
| `test_cashu.c` | Multi-mint acceptance (config-only) |
| `test_mint_health.c` | Health state machine, recovery, callbacks |
| `test_config.c` | Config parsing of `accepted_mints` array |

### Integration Tests (device)

1. Flash to Board A, verify discovery shows multiple mints
2. Send token from each mint, verify accepted
3. Block one mint at firewall level, verify becomes unreachable
4. Verify recovery after unblocking

### E2E Tests (Playwright)

1. Captive portal shows mint list with indicators
2. Pay with token from mint A → success
3. Pay with token from unreachable mint → error shown in portal

---

## 8. Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| TLS memory pressure with 4 wallets | Medium | High | Each wallet shares single TLS context; only probe makes concurrent HTTP |
| NVS key namespace collision | Low | High | Use distinct `nvs_slot` per wallet (0-3) |
| Keyset loading OOM on multiple mints | Medium | Medium | Cap keysets per wallet at `MAX_KEYSETS=10` |
| Health probe blocks other tasks | Low | Medium | Dedicated FreeRTOS task, low priority |
| Backward compatibility break | Low | High | `mint_url` field still works as fallback |

---

## 9. Backward Compatibility

- Existing `config.json` with only `"mint_url"` → works (populates `accepted_mints[0]` from it)
- Existing SPIFFS images → no change needed
- NVS data → compatible (single wallet stays at slot 0)
- API endpoints → same paths, discovery just has more tags
- Captive portal → same UI flow, more mints shown

---

## 10. Git Worktree Strategy

Multiple LLM sessions work on this repo simultaneously. To avoid conflicts:

### Setup

```
# Main worktree stays on master for other sessions
git -C /home/c03rad0r/esp32-tollgate checkout master

# Dedicated worktree for this feature
git -C /home/c03rad0r/esp32-tollgate worktree add /home/c03rad0r/esp32-tollgate-multi-mint feature/multi-mint-support
```

### Worktree Locations

| Path | Branch | Purpose |
|------|--------|---------|
| `/home/c03rad0r/esp32-tollgate` | `master` | Main worktree, shared with other sessions |
| `/home/c03rad0r/esp32-tollgate-multi-mint` | `feature/multi-mint-support` | This feature's isolated workspace |

### Conflict Avoidance Rules

| Rule | Why |
|------|-----|
| All edits happen in `/home/c03rad0r/esp32-tollgate-multi-mint` | Other sessions keep their own checkout untouched |
| Push after every green test | Other sessions can `git pull` to see progress |
| Never modify `master` directly | Merge only when feature is complete and tested |
| `git pull --rebase` before push | Avoid merge commits if others pushed to same branch |

### Cleanup (after merge)

```
git -C /home/c03rad0r/esp32-tollgate worktree remove /home/c03rad0r/esp32-tollgate-multi-mint
```

---

## 11. Implementation Checklist

- [x] Create feature branch `feature/multi-mint-support`
- [x] Write design document `docs/MULTI_MINT_DESIGN.md`
- [x] Set up git worktree at `/home/c03rad0r/esp32-tollgate-multi-mint`
- [x] Phase 1: Config layer (`config.h`, `config.c`) — multi-mint array
- [x] Phase 2: Multi-mint acceptance (`cashu.c`) — iterate accepted_mints
- [x] Phase 3: Mint health tracker (`mint_health.h`, `mint_health.c`) — FreeRTOS probing task
- [x] Phase 4: Health-aware acceptance integration — gate on reachability
- [x] Phase 5: Multi-mint discovery endpoint (`tollgate_api.c`) — one tag per reachable mint
- [x] Phase 6: Multi-mint captive portal UI (`captive_portal.c`) — mint list with indicators
- [x] Phase 7: Multi-mint wallet (`nucula_wallet.h`, `nucula_wallet.cpp`) — multi-wallet approach
- [x] Phase 8: Service startup integration (`tollgate_main.c`) — init health + multi-wallet
- [x] Unit tests: update `test_cashu.c` for multi-mint acceptance (14/14 pass)
- [x] Unit tests: all 256 existing tests pass
- [x] Build verification (ESP-IDF compiles cleanly, no errors)
- [ ] Unit tests: `test_mint_health.c` — health state machine, recovery, callbacks
- [ ] Flash Board A and verify multi-mint discovery
- [ ] Flash Board B and verify multi-mint discovery
- [ ] Payment test with token from each supported mint
- [ ] Health probe test (verify reachable/unreachable transitions)
- [ ] Captive portal multi-mint display verification
- [ ] Push after every passing test (blocked: Nostr relay down)
- [ ] Merge to master
