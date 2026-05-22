# TollGate Core Extraction Plan

## Goal

Extract TollGate business logic into a **reusable standalone C library** (`tollgate_core/`) wrapped by a **thin ESP-IDF component** (`components/tollgate_esp/`), so that:

- **BitAxe/NerdQAxePlus** gets: full captive portal + Stratum V1 mining + eCash payment + session management
- **ESP32-TollGate** gets: all of the above + Nostr identity + relay + CVM + display
- **Desktop/CI** gets: pure logic tests with no ESP-IDF stubs needed

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│  Consumer Application (main/)                            │
│  esp32-tollgate: tollgate_main.c, tollgate_api.c, etc.  │
│  esp-miner: main.cpp, asic_result_task.cpp              │
└────────────────────┬────────────────────────────────────┘
                     │
        ┌────────────┴─────────────┐
        │  components/tollgate_esp │  ← ESP-IDF component wrapper
        │  (implements platform_t │     via idf_component.yml
        │   using ESP-IDF APIs)   │
        └────────────┬────────────┘
                     │
    ┌────────────────┴────────────────┐
    │  tollgate_core/                 │  ← Standalone C library
    │  (zero ESP-IDF deps)            │
    │                                 │
    │  Layer 0: Pure Logic            │
    │    cashu_decode, allotment,     │
    │    session state machine,       │
    │    mining_payment economics     │
    │                                 │
    │  Layer 1: Protocol Logic        │
    │    DNS parser + server logic,   │
    │    Stratum V1 protocol,         │
    │    beacon IE builder/parser     │
    │                                 │
    │  Layer 2: Platform-dependent    │
    │    (via tollgate_platform.h)    │
    │    HTTP client, task creation,  │
    │    socket I/O, time source,     │
    │    logging, wallet operations   │
    └─────────────────────────────────┘
```

## Module Classification

| Module | Layer | Goes into tollgate_core? | Notes |
|--------|-------|--------------------------|-------|
| `cashu.c` — token decode, allotment math | 0 | **Yes** | Pure C + cJSON + mbedtls base64/sha256 |
| `session.c` — lifecycle, expiry, extend | 0 | **Yes** | Pure state machine, time via callback |
| `mining_payment.c` — hashprice, allotment | 0 | **Yes** | Pure math (nbits→difficulty→hashprice→allotment) |
| `dns_server.c` — DNS protocol, per-client auth | 1 | **Yes** | Protocol parser pure; socket IO via platform |
| `stratum_proxy.c` — SV1 TCP server | 1 | **Yes** | JSON protocol pure; TCP via platform |
| `stratum_client.c` — SV1 pool client | 1 | **Yes** | JSON protocol pure; TCP via platform |
| `beacon_price.c` — WiFi IE builder/parser | 1 | **Yes** | IE struct packing pure; esp_wifi_set_vendor_ie via platform |
| `market.c` — IE scanner, cheapest finder | 1 | **Yes** | IE parser + price comparison pure; scan results via platform |
| `firewall.c` — client allowlist, NAT filter | 2 | **Yes** | Allowlist logic pure; lwIP hook + MAC resolve via platform |
| `config.c` — JSON config parsing | — | **No** | Stays in consumer. Core takes config via `platform_t` |
| `identity.c` — HMAC derivation from nsec | — | **No** | Nostr-specific, stays in esp32-tollgate |
| `nostr_event.c` — Schnorr signing | — | **No** | Nostr-specific |
| `wifistr.c` — kind 38787 events | — | **No** | Nostr-specific |
| `cvm_server.c` — MCP over Nostr | — | **No** | Nostr-specific |
| `relay_selector.c` — NIP-11 probing | — | **No** | Nostr-specific |
| `sync_manager.c` — REQ-diff sync | — | **No** | Nostr-specific |
| `local_relay.c` — NIP-01 server | — | **No** | Nostr-specific |
| `display.c` — TFT rendering | — | **No** | Hardware-specific |
| `tollgate_client.c` — downstream auto-pay | — | **No** | Consumes core API, but isn't core itself |
| `nucula_lib` — wallet C++ impl | — | **No** | External dep; core uses wallet via platform callback |
| `mint_health.c` — health probe + wallet queue | — | **Partial** | Health state machine extractable; wallet queue stays in consumer |

## Platform Interface (`tollgate_platform.h`)

```c
typedef struct {
    // --- Config access ---
    uint16_t     (*get_price_sats)(void);
    int32_t      (*get_step_ms)(void);
    int64_t      (*get_step_bytes)(void);
    const char*  (*get_mint_url)(void);
    const char*  (*get_metric)(void);

    // --- Time ---
    int64_t      (*get_time_ms)(void);

    // --- Logging ---
    void         (*log_info)(const char *tag, const char *fmt, ...);
    void         (*log_warn)(const char *tag, const char *fmt, ...);
    void         (*log_error)(const char *tag, const char *fmt, ...);

    // --- Wallet (can be NULL — accepts payment without spending) ---
    bool         (*wallet_receive)(const char *token);
    bool         (*wallet_send)(uint64_t amount, char *buf, size_t buf_len);
    uint64_t     (*wallet_balance)(void);

    // --- HTTP client (for checkstate) ---
    int          (*http_post)(const char *url, const char *headers,
                             const char *body, int body_len,
                             char *resp, int resp_len);

    // --- Task/thread creation ---
    bool         (*create_task)(void (*fn)(void*), void *arg,
                               const char *name, int stack_bytes, int priority);

    // --- Socket I/O (for DNS + Stratum) ---
    int          (*socket_udp)(void);
    int          (*socket_tcp)(void);
    int          (*socket_bind)(int fd, uint32_t ip, uint16_t port);
    int          (*socket_listen)(int fd, int backlog);
    int          (*socket_accept)(int fd, uint32_t *client_ip, uint16_t *client_port);
    int          (*socket_recvfrom)(int fd, void *buf, int len,
                                    uint32_t *src_ip, uint16_t *src_port);
    int          (*socket_sendto)(int fd, const void *buf, int len,
                                  uint32_t dest_ip, uint16_t dest_port);
    int          (*socket_read)(int fd, void *buf, int len);
    int          (*socket_write)(int fd, const void *buf, int len);
    void         (*socket_close)(int fd);
    void         (*socket_set_recv_timeout)(int fd, int ms);

    // --- WiFi/MAC (for firewall + beacon + market) ---
    bool         (*get_sta_mac_ip_list)(void *list_out, int max, int *count_out);
    bool         (*set_vendor_ie)(bool enable, const void *ie_data, int ie_len);
    int          (*arp_get_mac)(uint32_t ip, uint8_t *mac_out);
    void         (*napt_enable)(uint32_t ip, bool enable);

    // --- Mining config ---
    bool         (*mining_enabled)(void);
    const char*  (*get_stratum_host)(void);
    uint16_t     (*get_stratum_port)(void);
    const char*  (*get_stratum_user)(void);
    const char*  (*get_stratum_pass)(void);
    uint16_t     (*get_mining_port)(void);
    uint64_t     (*get_hashprice_override)(void);

    // --- Random ---
    void         (*fill_random)(void *buf, int len);

} tollgate_platform_t;
```

## Directory Structure

```
tollgate_core/                          ← standalone C library (CMakeLists.txt, no ESP-IDF)
  CMakeLists.txt
  include/
    tollgate_core.h
    tollgate_platform.h
  src/
    tollgate_core.c
    tollgate_cashu.c/h
    tollgate_session.c/h
    tollgate_dns.c/h
    tollgate_firewall.c/h
    tollgate_mining.c/h
    tollgate_stratum_client.c/h
    tollgate_stratum_proxy.c/h
    tollgate_beacon.c/h
    tollgate_market.c/h

components/tollgate_esp/                ← ESP-IDF component wrapper
  CMakeLists.txt
  idf_component.yml
  src/
    tollgate_esp_platform.c
    tollgate_esp_http.c
    tollgate_esp_sockets.c
    tollgate_esp_wifi.c
    tollgate_esp_wallet.c
    tollgate_esp_tasks.c

main/                                   ← esp32-tollgate specific (shrinks)
  tollgate_main.c, tollgate_api.c, captive_portal.c
  config.c, identity.c, nostr_event.c, wifistr.c
  cvm_server.c, mcp_handler.c, local_relay.c
  relay_selector.c, sync_manager.c
  display.c, font.c
  mint_health.c, tollgate_client.c
```

---

## Implementation Phases

### Phase 0: Housekeeping

- [x] Archive stale branches to `/home/c03rad0r/mining-work-backup/` as git bundles
- [x] Commit `PLAN_pytest_migration.md`
- [x] Fix `nucula_src` submodule drift
- [x] Fix missing unit test build rules (`test_relay_selector.c`, `test_relay_validator.c`)
- [x] Remove broken test binaries from TESTS list (`test_display`, `test_negentropy_adapter`)
- [x] Run `make test-unit` — confirm green baseline (22/22 pass)
- [x] Create branch `feature/tollgate-core-v2` from master

### Phase 1: Create Skeleton

- [x] Create `tollgate_core/` directory with `CMakeLists.txt`, `include/`, `src/`
- [x] Define `tollgate_platform.h` interface (all callbacks)
- [x] Define `tollgate_core.h` public API
- [x] Create `tollgate_core.c` with `tollgate_core_init()`, `tollgate_core_tick()`
- [x] Create `components/tollgate_esp/` with `tollgate_esp_platform.c`
- [x] Wire into build system (CMakeLists.txt)
- [x] Verify: standalone cmake build passes, ESP-IDF build passes

### Phase 2: Extract Layer 0 — Pure Logic

- [x] `tollgate_cashu.c` — token decode, allotment math, checkstate (via platform HTTP)
- [x] `tollgate_session.c` — lifecycle, expiry, extend, bytes/time support
- [x] `tollgate_mining.c` — hashprice, nbits→difficulty, shares-to-allotment, client stats
- [ ] Port unit tests to test `tollgate_core/` directly (no ESP-IDF stubs)
- [x] Verify: `make test-unit` passes (22/22), standalone cmake build passes

### Phase 3: Extract Layer 1 — Protocol Logic (STUBS — placeholder implementations)

- [x] `tollgate_firewall.c` — client allowlist, NAT filter, packet filter
- [ ] `tollgate_dns.c` — DNS protocol parser, hijack, forward, per-client auth (stub returns -1)
- [ ] `tollgate_stratum_client.c` — SV1 pool client (subscribe, authorize, submit) (stub returns -1)
- [ ] `tollgate_stratum_proxy.c` — SV1 local proxy (TCP server, job broadcast) (stub returns -1)
- [ ] `tollgate_beacon.c` — Vendor IE builder/parser (stub no-op)
- [ ] `tollgate_market.c` — price scanner, LRU entries, cheapest finder (stub no-op)
- [x] Wire `tollgate_core.c` orchestrator to all subsystems
- [ ] Verify: Build on hardware, run all integration tests

### Phase 4: ESP-IDF Wrapper

- [x] `tollgate_esp_platform.c` — implement all `tollgate_platform_t` callbacks
- [x] `components/tollgate_esp/CMakeLists.txt` — REQUIRES tollgate_core + ESP-IDF + nucula_lib
- [x] `components/tollgate_esp/idf_component.yml` — IDF Component Registry
- [x] Verify: Full ESP-IDF build passes (`idf.py build` succeeds)
- [ ] Verify: Flash + all tests pass on Board A (deferred to Phase 5)

### Phase 5: Wire main/ to Use tollgate_core

- [ ] Update `main/tollgate_main.c` — replace direct calls with tollgate_core API
- [ ] Update `main/tollgate_api.c` — payment → `tollgate_core_process_payment()`
- [ ] Update `main/captive_portal.c` — grant/reset → tollgate_core calls
- [ ] Remove old `main/session.c`, `main/firewall.c`, `main/dns_server.c`, `main/cashu.c`, etc.
- [ ] Verify: `make test-unit` passes, flash to board, run `make pytest-all-a`

### Phase 6: NerdQAxePlus Integration

- [ ] Update `main/tollgate_platform.cpp` to implement new `tollgate_platform_t`
- [ ] Wire `asic_result_task.cpp` → `tollgate_core_process_share()`
- [ ] Wire `main.cpp` → `tollgate_core_init()` + `tollgate_core_dns_start()` + `tollgate_core_tick()`
- [ ] Verify: `BOARD=NERDAXE TOLLGATE=1 idf.py build` passes

### Phase 7: Publishing + CI

- [ ] Publish `tollgate_esp` component to IDF Component Registry
- [ ] Add CI pipeline: build tollgate_core with host gcc + run pure-logic tests
- [ ] Add CI pipeline: build tollgate_esp for ESP32-S3 target
- [ ] Update documentation

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| RAM budget — tollgate_core statics consume more memory | Board crashes | Profile before/after with `heap_caps_get_free_size`. Keep static arrays same size. |
| Platform callback overhead on hot path (firewall) | Performance | Firewall filter already a function pointer. No extra overhead. |
| nucula_lib coupling — C++ but core is C | Build complexity | Wallet stays behind platform callback. Core never includes nucula headers. |
| Existing branches have useful work | Lost effort | Archived to `/home/c03rad0r/mining-work-backup/` as git bundles. |
| Test breakage during extraction | Regression | Run full test suite after each phase. Commit after each green phase. |

## Estimated Time

| Phase | Description | Time |
|-------|-------------|------|
| 0 | Housekeeping | 30 min |
| 1 | Skeleton | 2 hours |
| 2 | Layer 0: Pure logic | 4 hours |
| 3 | Layer 1: Protocol logic | 6 hours |
| 4 | ESP-IDF wrapper | 4 hours |
| 5 | Wire main/ | 6 hours |
| 6 | NerdQAxePlus integration | 4 hours |
| 7 | Publishing + CI | 2 hours |
| **Total** | | **~29 hours** |
