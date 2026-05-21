# Plan: Lazy Keyset Loading + Jittered Exponential Backoff

## Problem
`nucula_wallet_init()` calls `load_keysets()` which does HTTPS to the mint. At boot (~3s after STA gets IP), other services are initializing, internal RAM is fragmented, and TLS allocation fails. Wallet ends up with zero keysets → `receive()` fails because `active_keyset()` returns null.

## Solution
Lazy keyset loading (load on first use) + exponential backoff with jitter on failure. Blocking approach — no background task.

## Checklist

### Implementation — DONE
- [x] Add per-slot state tracking (`s_keysets_loaded`, `s_keyset_attempts`, delay constants)
- [x] Add `ensure_keysets(int slot)` helper with exponential backoff + jitter
- [x] Remove eager `load_keysets()` from `init_wallet()`
- [x] Add `ensure_keysets()` guard to `nucula_wallet_receive()`
- [x] Add `ensure_keysets()` guard to `nucula_wallet_send()`
- [x] Add `ensure_keysets()` guard to `nucula_wallet_melt()`
- [x] Add `ensure_keysets()` guard to `nucula_wallet_swap_all()`
- [x] Add required includes (`freertos/FreeRTOS.h`, `freertos/task.h`, `esp_random.h`)
- [x] Fix lazy keyset loading success log (save attempts before reset)

### TLS Fixes — DONE
- [x] Skip checkstate on TLS failure, let wallet swap verify (tollgate_api.c)
- [x] Pin HTTP server to core 0 (fixes checkstate and keyset TLS)
- [x] Rewrite nucula http.c to use manual open/write/read path (same as working health probe)
- [x] Disable hardware MPI (CONFIG_MBEDTLS_HARDWARE_MPI=n, did not fix but kept for safety)
- [x] Add 2s delay after ensure_keysets before receive to let TLS resources free

### Verification — DONE
- [x] `make test-unit` passes (407/407)
- [x] Firmware builds and flashes to Board A
- [x] Payment accepted (session created with correct allotment)
- [x] Keyset loading succeeds on first use (lazy loading confirmed via serial)
- [x] Checkstate succeeds (Certificate validated via serial)

### Remaining Issue — TLS Progressive Failure
- [ ] After 2-3 TLS connections, subsequent connections fail with `PK verify failed 0x4290`
- [ ] This affects the wallet's swap POST (3rd TLS connection in payment flow)
- [ ] Root cause: internal RAM fragmentation after multiple TLS handshakes
- [ ] Workaround tried: core pinning, manual HTTP client, hardware MPI disable, delays
- [ ] **Next step**: Investigate mbedTLS internal allocations, consider forcing SSL context to PSRAM, or use `mbedtls_ssl_session_reset()` to reuse SSL context

## Design Details

### Parameters
- `BASE_DELAY_MS = 1000` (1s initial delay)
- `MAX_DELAY_MS = 30000` (30s cap)
- `JITTER_MS = 500` (random 0-500ms added)
- No max attempts — retries indefinitely until success

### ensure_keysets(slot) logic
1. If `s_keysets_loaded[slot]` → return true (no-op)
2. If `s_wallets[slot]->keysets()` non-empty → set loaded flag, return true (NVS cache hit)
3. Compute `delay = min(BASE_DELAY_MS << attempts, MAX_DELAY_MS) + (esp_random() % JITTER_MS)`
4. `vTaskDelay(delay / portTICK_PERIOD_MS)`
5. Call `s_wallets[slot]->load_keysets()`
   - Success → set loaded flag, log success, return true
   - Failure → increment attempts counter, return false

### Key Changes
| File | Change |
|------|--------|
| `components/nucula_lib/nucula_wallet.cpp` | Lazy keysets + ensure_keysets guards |
| `main/tollgate_api.c` | Skip checkstate on failure, core_id=0 |
| `nucula_src/main/http.c` | Manual open/write/read (same as health probe) |
| `sdkconfig.defaults` | MBEDTLS_HARDWARE_MPI=n |
| `tests/unit/stubs/http.h` | Stub for unit tests |
