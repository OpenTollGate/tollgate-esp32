# Phase 5 Status — tollgate_core Extraction Verification

**Date:** 2026-07-06
**Repo:** `/home/c03rad0r/esp32-tollgate`
**Baseline commit:** `827133a` (tollgate_core extracted) → verified at HEAD `9c292c8`
**Verifier:** Hermes subagent (manager profile)

---

## 1. Executive Summary

The `tollgate_core` extraction is **essentially complete and correctly wired**.
Phase 5 verification found **one dead orphan file** (`main/tollgate_platform.c`)
that was not compiled, not referenced by any caller, and duplicated the
platform implementation already living in the `tollgate_esp` component.
It has been removed in this commit. **No other duplicates or missing wiring
were found.** All 801 unit tests pass.

---

## 2. Component Architecture (as found)

```
components/
├── tollgate_core/          ← abstracted core (public API in include/tollgate_core.h)
│   └── src/                ← 13 modules: core, cashu, dns, firewall, session,
│                             mining, stratum_proxy, stratum_client, beacon,
│                             market, portal, mint_health, client
└── tollgate_esp/           ← ESP-IDF platform implementation of tollgate_platform_t
    └── src/tollgate_esp_platform.c   ← provides tollgate_esp_get_platform()
```

- `tollgate_core` defines a `tollgate_platform_t` vtable (function pointers for
  price, step, mint URL, stratum config, hashprice, etc.).
- `tollgate_esp` implements that vtable by reading the `tollgate_config_t`
  singleton from `main/config.c`.
- `main/tollgate_main.c:249` wires them together:
  `tollgate_core_init(tollgate_esp_get_platform(), ap_ip_info.ip);`

---

## 3. main/ File Inventory — Classification

### 3a. Thin wrappers / callers of `tollgate_core_*` (13 files — CORRECT pattern)

These `main/*.c` files include `tollgate_core*.h` and call the core API:

| File | Role |
|------|------|
| `dns_server.c` | Pure wrapper → `tollgate_core_dns_*` |
| `captive_portal.c` | Calls `tollgate_core_*` for grant/reset/auth |
| `tollgate_api.c` | HTTP endpoints → `tollgate_core_*` |
| `tollgate_main.c` | Lifecycle: `tollgate_core_init()` + `tollgate_esp_get_platform()` |
| `mcp_handler.c` | MCP tools → `tollgate_core_get_*_json` |
| `cvm_server.c` | ContextVM → `tollgate_core_*` status |
| `beacon_price.c` | Price beacon → `tollgate_core_*` |
| `market.c` | Market data → `tollgate_core_*` |
| `mint_health.c` | Mint health → `tollgate_core_*` |
| `stratum_client.c` | Stratum → `tollgate_core_stratum_*` |
| `stratum_proxy.c` | Stratum proxy → `tollgate_core_stratum_*` |
| `sw_miner.c` | SW miner → `tollgate_core_*` |
| `tollgate_client.c` | Client helpers → `tollgate_core_*` |

### 3b. Standalone app-specific code (SUPPOSED to stay in main/ — no action)

These are ESP-app glue / hardware / protocol code that is correctly NOT in
`tollgate_core`:

`config.c`, `identity.c`, `nostr_event.c`, `geohash.c`, `wifistr.c`,
`lnurl_pay.c`, `lightning_payout.c`, `nip04.c`, `display.c`, `font.c`,
`touch.c`, `keyboard.c`, `wifi_setup.c`, `local_relay.c`, `relay_selector.c`,
`sync_manager.c`, `faucet_client.c`, `remote_miner.c`, `asic_miner.c`,
`tollgate_client.c` (wrapper, listed above), `tls_worker.c`,
`negentropy_adapter.c`.

### 3c. Duplicates / orphans found

| File | Status | Action |
|------|--------|--------|
| `main/tollgate_platform.c` | **DEAD ORPHAN** — not in `main/CMakeLists.txt` SRCS (never compiled); defines `tollgate_get_platform()` which is **called nowhere**; fully superseded by `tollgate_esp/src/tollgate_esp_platform.c` (`tollgate_esp_get_platform()`). | **DELETED in this commit** |

**No other duplicates.** Critically, the files the original task brief
suspected (`main/session.c`, `main/firewall.c`, `main/cashu.c`) **do not
exist in `main/`** — they were already fully migrated into
`components/tollgate_core/src/tollgate_core_{session,firewall,cashu}.c`
during the extraction commit `827133a`.

---

## 4. CMakeLists Wiring (verified correct)

- `main/CMakeLists.txt` lists `tollgate_esp` in `REQUIRES` (which itself
  `REQUIRES tollgate_core`), so the core is transitively linked. This is the
  intended ESP-IDF dependency direction.
- `main/CMakeLists.txt` SRCS does **not** and should **not** contain
  `tollgate_platform.c` (the deleted orphan).
- `components/tollgate_core/CMakeLists.txt` registers all 13 core modules.
- `components/tollgate_esp/CMakeLists.txt` registers `tollgate_esp_platform.c`
  with `PRIV_INCLUDE_DIRS "../../main"` so it can read `config.h`.

---

## 5. Test Results

Command: `make test-unit` (host gcc, no hardware required)

| Metric | Value |
|--------|-------|
| Test files | **37** (`tests/unit/test_*.c`) |
| Total assertions | **801 PASS / 0 FAIL** |
| Status | ✅ **ALL UNIT TESTS PASSED** |

Test coverage spans: geohash, identity, nostr_event, cashu, session,
tollgate_client, lnurl_pay, lightning_payout, mcp_handler (60 cases),
nip04, cvm_server (61 cases), beacon_price, market, mining_api,
mining_payment, mint_health, mint_health_core, negentropy_adapter,
portal, relay_selector, relay_types, relay_validator, remote_miner,
stratum_client, stratum_pow, stratum_proxy, sub_manager, deletion,
display, faucet_client, firewall_sandbox, keyboard, session_payment_method,
tollgate_client_mining, touch, wifi_setup, client_core.

Matches AGENTS.md target of "800+ test cases across 30+ files".

---

## 6. What's Done ✅

1. `tollgate_core` component fully extracted (13 modules) — commit `827133a`.
2. `tollgate_esp` platform component provides `tollgate_platform_t` impl.
3. `tollgate_main.c` wires core + platform via `tollgate_core_init()`.
4. 13 `main/*.c` files correctly call `tollgate_core_*` API (wrapper pattern).
5. No `session.c` / `firewall.c` / `cashu.c` left in `main/` — fully migrated.
6. Dead orphan `main/tollgate_platform.c` removed (this commit).
7. All 801 unit tests pass.

## 7. What Remains 🔲

- **Nothing blocking.** Extraction is functionally complete.
- Optional future cleanup (out of Phase 5 scope, low priority):
  - `tests/unit/test_relay_selector` (compiled ELF binary) is tracked by git
    and shows as modified after a rebuild. Consider adding
    `tests/unit/test_relay_selector` and other built test binaries to
    `.gitignore` to avoid noise. (Pre-existing condition, not introduced here.)
  - Historical design docs (`TOLLGATE_CORE_DESIGN.md`, `CONSOLIDATION.md`,
    `MINER_INTEGRATION_PLAN.md`, `PLAN_TOLLGATE_CORE_EXTRACTION.md`,
    `SESSION_PROGRESS.md`) still reference the now-deleted
    `main/tollgate_platform.c`. These are historical records and were left
    untouched; they accurately describe the design evolution.

---

## 8. Commit

This commit:
- Deletes `main/tollgate_platform.c` (dead orphan duplicate).
- Adds `docs/PHASE5_STATUS.md` (this report).
- Re-ran `make test-unit`: **801 PASS / 0 FAIL** (unchanged after deletion,
  confirming the file was never compiled or tested).
