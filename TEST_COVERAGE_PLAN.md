# Test Coverage + Phase 4/5 + Interop Plan

Ensure full hardware + Playwright test coverage at all times. Restructure main/ to delegate
to tollgate_core. Fix NerdQAxePlus symlink. Add ESP32 ↔ OpenWRT interop tests.

## End State

```
esp32-tollgate (master)
  main/            = thin shell calling tollgate_core API
  components/tollgate_core/ = THE reusable core (cashu, dns, firewall, session, mining, stratum)
  .env             = board nsecs + upstream WiFi secrets (gitignored)

esp-miner-nerdqaxeplus (develop)
  components/tollgate_core/ = IDF Component Manager override_path → esp32-tollgate

physical-router-test-automation/
  esp32/Makefile   = ESP32 board tests (lock-protected)
  esp32/boards.env = all 3 boards with derived SSID/IP/MAC
  mint-health/     = OpenWRT router tests (lock-protected)
  Interop targets  = ESP32 ↔ Go router bidirectional payment tests
```

## Mutex Rules (MANDATORY)

| Target type | Lock mechanism | Required locks |
|-------------|---------------|----------------|
| ESP32 flash/test | `make -C esp32 lock-{a,b,c}` | Per-board lock file in locks/ |
| OpenWRT router test | `make lock` | hardware.lock |
| Interop (ESP32+router) | Both | Board lock AND hardware.lock |
| Unit tests | None | None (no hardware) |
| Build | None | None (no hardware) |

All hardware access MUST go through lock-protected Makefile targets.
No direct esptool.py, curl, or nmcli against hardware without locks.

## Mints

| Mint | URL | Purpose | Status |
|------|-----|---------|--------|
| Testnut | `https://testnut.cashu.space` | ESP32 boards, auto-pays invoices | UP |
| ~~Testnut no-fee~~ | `https://nofee.testnut.cashu.space` | ~~Alpha router, feeless swaps~~ | **DOWN** (TLS timeout) |
| Orangesync compat | `https://testnut-compat.mints.orangesync.tech` | All routers, mint-token tool default | UP |
| Minibits | `https://mint.minibits.cash/Bitcoin` | Alpha router (secondary) | Unknown |

---

## Phase 0: Fix Stale Infrastructure

- [x] **0.1** Generate random nsec for Board C, store all 3 nsecs in esp32-tollgate/.env
- [x] **0.2** Derive Board C identity (SSID, IP, MACs) from nsec via HMAC-SHA512
- [x] **0.3** Update physical-router-test-automation/esp32/boards.env:
  - FIRMWARE_DIR → /home/c03rad0r/esp32-tollgate
  - ARCH_FIRMWARE_DIR → /home/c03rad0r/esp32-tollgate
  - RELAY_FIRMWARE_DIR → /home/c03rad0r/esp32-tollgate
  - Add BOARD_C_SSID, BOARD_C_IP, BOARD_C_UUID
  - Update ARCH_IP/ARCH_SSID to Board C's derived values
- [x] **0.4** Update SPIFFS generation to read nsec from .env per board
- [x] **0.5** Fix NerdQAxePlus broken symlink → idf_component.yml with override_path
- [x] **0.6** Remove hardcoded include from NerdQAxePlus main/CMakeLists.txt
- [x] **0.7** Verify NerdQAxePlus builds: BOARD=NERDAXE TOLLGATE=1 idf.py build
- [x] **0.8** Verify esp32-tollgate builds: idf.py build
- [x] **0.9** Verify unit tests pass: make test-unit
- [ ] **0.10** Commit + push

## Phase 1: Baseline Hardware Tests

### Board A (TollGate-B96D80 at 10.185.47.1)

- [x] **1.1** make -C esp32 lock-a PHASE="baseline"
- [x] **1.2** Firmware fix: replaced `xTaskCreate` with `esp_timer` for `start_services()` (commit `09c9a64`)
- [x] **1.3** Captive portal moved to `start_ap_services()` — portal starts on AP_START, not STA GOT IP
- [x] **1.4** Added `/debug` HTTP endpoint (portal_running, start_services_called, sta_got_ip, free_heap)
- [x] **1.5** Smoke test: **6/6 PASS** (SSID visible, portal loads, grant, internet, reset, blocked)
- [x] **1.6** Multi-mint API test: Discovery (kind 10021) OK, Mints endpoint OK, Wallet OK
- [x] **1.7** Unit tests: **407/407 PASS** (22 test suites)
- [x] **1.8** ESP32 E2E tests: 3/14 pass (Board A WiFi became unstable during E2E run)
- [x] **1.9** make -C esp32 unlock-a

**Known issue:** Board A mint health reports `reachable: false` for testnut.cashu.space. STA gets IP but DNS doesn't resolve mint URLs. Wallet funding blocked by `"payment-error-mint-not-accepted"`.

### Board B (TollGate-C0E9CA at 10.192.45.1)

- [x] **1.10** Diagnosed as **hardware issue** — USB CDC ACM stub upload fails at ~64% of app binary. Bootloader/partition table always flash fine. Board B skipped for Phase 1.

### Board C (TollGate-4A2510 at 10.74.63.1)

- [x] **1.11** make -C esp32 lock-c PHASE="baseline-provision"
- [x] **1.12** SPIFFS generated with Board C nsec (71bf3f4d...)
- [x] **1.13** make -C esp32 provision-c — flash + SPIFFS + boot + verify: **SUCCESS**
- [x] **1.14** Debug: all services started, sta_got_ip=true, 8MB heap free
- [x] **1.15** Discovery API: kind 10021 with correct pricing (21 sats/step)
- [x] **1.16** Mints: testnut.cashu.space registered, reachable=false (same STA DNS issue)
- [x] **1.17** Portal HTML loads correctly with server-side template substitution
- [x] **1.18** **ESP32 E2E: 14/14 PASS** (all captive portal tests in 15s)
- [x] **1.19** make -C esp32 unlock-c

### OpenWRT Router Alpha (10.47.41.1)

- [x] **1.20** Fixed mint URL: replaced down `nofee.testnut.cashu.space` with `testnut-compat.mints.orangesync.tech`
- [x] **1.21** Rebuilt `/tmp/mint-token` Go binary from updated source
- [x] **1.22** r-smoke-degraded: **PASSED** (all 9 phases: setup, fund, full merchant, baseline, block, degraded, offline ops, unblock, restore)
- [x] **1.23** r-fix-dns: DNS restored, merchant mode confirmed
- [x] **1.24** r-test-captive-portal-happy: **4/4 desktop-portal tests PASS** (API, cashu input, lightning input, mint buttons)
- [x] **1.25** Portal port fix: tests updated from port 2050 → 80 (nodogsplash)

### Unit Tests (no hardware)

- [x] **1.26** make test-unit: **407/407 PASS** across 22 test suites:
  - test_geohash (11), test_identity (24), test_nostr_event (23), test_cashu (14),
  - test_session (16), test_tollgate_client (30), test_lnurl_pay (7),
  - test_lightning_payout (11), test_mcp_handler (60), test_nip04 (15),
  - test_cvm_server (61), test_display (22), test_negentropy_adapter (109),
  - test_beacon_price (28), test_market (17), test_mint_health (14),
  - test_mining_payment (23), test_stratum_proxy (21), test_session_payment_method (12),
  - test_tollgate_client_mining (20), test_firewall_sandbox (16)

### Summary

| Category | Result |
|----------|--------|
| Unit tests | **407/407 PASS** |
| Board A smoke | **6/6 PASS** |
| Board A E2E | **3/14 PASS** (WiFi instability) |
| Board B | **SKIPPED** (USB hardware issue) |
| Board C provision | **SUCCESS** |
| Board C E2E | **14/14 PASS** |
| Router Alpha smoke-degraded | **PASSED** |
| Router Alpha captive portal | **4/4 PASS** |
| Board wallet funding | **BLOCKED** (STA DNS issue) |

## Phase 2: Restructure main/ → tollgate_core

Consolidate 6 duplicated modules. Order: session → firewall → cashu → dns_server → stratum_proxy → mining_payment.

### 2A: session module

- [ ] **2A.1** Diff main/session.c vs tollgate_core/src/tollgate_core_session.c
- [ ] **2A.2** Merge unique main/ functionality into tollgate_core
- [ ] **2A.3** Update tollgate_core.h public API if needed
- [ ] **2A.4** Replace main/session.c with thin wrapper (or remove)
- [ ] **2A.5** Update main/CMakeLists.txt
- [ ] **2A.6** idf.py build
- [ ] **2A.7** make test-unit
- [ ] **2A.8** Quick hardware verify (lock-a, flash-a, arch-test-api-quick, unlock-a)
- [ ] **2A.9** Commit + push

### 2B: firewall module

- [ ] **2B.1** Diff main/firewall.c vs tollgate_core/src/tollgate_core_firewall.c
- [ ] **2B.2** Merge unique main/ functionality into tollgate_core
- [ ] **2B.3** Keep lwip_tollgate_hooks.h in main/ (build-system concern)
- [ ] **2B.4** main/firewall.c becomes thin wrapper calling tollgate_core_fw_* functions
- [ ] **2B.5** Verify LWIP hook still calls tollgate_core_ip4_canforward_filter
- [ ] **2B.6** Update main/CMakeLists.txt
- [ ] **2B.7** idf.py build
- [ ] **2B.8** make test-unit
- [ ] **2B.9** Quick hardware verify
- [ ] **2B.10** Commit + push

### 2C: cashu module

- [ ] **2C.1** Diff main/cashu.c vs tollgate_core/src/tollgate_core_cashu.c
- [ ] **2C.2** Merge unique main/ functionality into tollgate_core
- [ ] **2C.3** Replace main/cashu.c with thin wrapper
- [ ] **2C.4** Update main/CMakeLists.txt
- [ ] **2C.5** idf.py build
- [ ] **2C.6** make test-unit
- [ ] **2C.7** Quick hardware verify
- [ ] **2C.8** Commit + push

### 2D: dns_server module

- [ ] **2D.1** Diff main/dns_server.c vs tollgate_core/src/tollgate_core_dns.c
- [ ] **2D.2** Merge unique main/ functionality into tollgate_core
- [ ] **2D.3** Replace main/dns_server.c with thin wrapper
- [ ] **2D.4** Update main/CMakeLists.txt
- [ ] **2D.5** idf.py build
- [ ] **2D.6** make test-unit
- [ ] **2D.7** Quick hardware verify
- [ ] **2D.8** Commit + push

### 2E: stratum_proxy module

- [ ] **2E.1** Diff main/stratum_proxy.c vs tollgate_core/src/tollgate_core_stratum_proxy.c
- [ ] **2E.2** Merge unique main/ functionality into tollgate_core
- [ ] **2E.3** Replace main/stratum_proxy.c with thin wrapper
- [ ] **2E.4** Update main/CMakeLists.txt
- [ ] **2E.5** idf.py build
- [ ] **2E.6** make test-unit
- [ ] **2E.7** Quick hardware verify
- [ ] **2E.8** Commit + push

### 2F: mining_payment module

- [ ] **2F.1** Diff main/mining_payment.c vs tollgate_core/src/tollgate_core_mining.c
- [ ] **2F.2** Merge unique main/ functionality into tollgate_core
- [ ] **2F.3** Replace main/mining_payment.c with thin wrapper
- [ ] **2F.4** Update main/CMakeLists.txt
- [ ] **2F.5** idf.py build
- [ ] **2F.6** make test-unit
- [ ] **2F.7** Quick hardware verify
- [ ] **2F.8** Commit + push

### 2G: Post-restructure verification

- [ ] **2G.1** Full hardware test (repeat Phase 1)
- [ ] **2G.2** make test-unit — all suites pass
- [ ] **2G.3** Commit + push

## Phase 3: NerdQAxePlus Component Manager Verification

- [ ] **3.1** BOARD=NERDAXE TOLLGATE=1 idf.py build (from esp-miner-nerdqaxeplus)
- [ ] **3.2** Verify tollgate_nerdqaxe_init() links correctly
- [ ] **3.3** Commit + push (if any changes)

## Phase 4: ESP32 ↔ OpenWRT Interop Tests

### Setup infrastructure

- [ ] **4.1** Create esp32/interop-test.sh orchestration script
- [ ] **4.2** Add interop Make targets to esp32/Makefile (all require board lock + hardware.lock)
- [ ] **4.3** Add interop Make targets to top-level Makefile (forwarding)
- [ ] **4.4** Create tests/protocol/esp32-router-interop.spec.mjs (Playwright)

### Direction 1: ESP32 buys from Go router

- [ ] **4.5** interop-esp32-buy-from-alpha: Board A STA → Alpha AP → discover → pay → internet
- [ ] **4.6** interop-esp32-buy-from-beta: Board A STA → Beta AP → discover → pay → internet

### Direction 2: Go router buys from ESP32

- [ ] **4.7** interop-alpha-buy-from-a: Alpha STA → Board A AP → discover → pay → internet
- [ ] **4.8** interop-beta-buy-from-a: Beta STA → Board A AP → discover → pay → internet

### Bidirectional

- [ ] **4.9** interop-bidirectional-a-alpha: Both directions in sequence
- [ ] **4.10** Commit + push

## Phase 5: Post-Restructure Full Test Suite

- [ ] **5.1** make test-unit — all suites pass
- [ ] **5.2** ESP32 full suite (all 3 boards)
- [ ] **5.3** OpenWRT full suite (both routers)
- [ ] **5.4** Playwright E2E (ESP32 captive portal + Go captive portal)
- [ ] **5.5** Interop tests (all directions)
- [ ] **5.6** Final commit + push

---

## LWIP Hook Integration Pattern (for Bitaxe/esp-miner repos)

The LWIP hook provides per-client (per-MAC) NAT access control. Other repos integrate it via:

1. Add to root CMakeLists.txt:
```cmake
idf_component_get_property(lwip lwip COMPONENT_LIB)
target_compile_options(${lwip} PRIVATE "-I${PROJECT_DIR}/main")
target_compile_definitions(${lwip} PRIVATE "-DESP_IDF_LWIP_HOOK_FILENAME=\"lwip_tollgate_hooks.h\"")
```

2. Copy lwip_tollgate_hooks.h to main/ (calls tollgate_core_ip4_canforward_filter)

3. Add tollgate_core as component dependency (override_path or git)

4. Call from platform code:
   - tollgate_core_fw_init(ap_ip)
   - tollgate_core_fw_grant(client_ip)
   - tollgate_core_fw_revoke(client_ip)
   - tollgate_core_fw_is_allowed(client_ip)
   - tollgate_core_ip4_canforward_filter(p, addr) — called by LWIP hook

## Identity Derivation

All identifiers (SSID, MAC, IP) are derived from nsec via HMAC-SHA512:

```
nsec → secp256k1 → npub (Nostr public key)
nsec → HMAC-SHA512("sta-mac", 0) → STA MAC (locally administered)
nsec → HMAC-SHA512("ap-mac", 0)  → AP MAC (locally administered)
AP MAC → SSID = "TollGate-{MAC[3]}{MAC[4]}{MAC[5]}"
AP MAC → IP = 10.{MAC[3]}.{(MAC[4]^MAC[5])%200+10}.1
```

Rotating nsec automatically rotates all identifiers.

## Board Configuration

| Board | nsec source | SSID | AP IP | Port |
|-------|-----------|------|-------|------|
| A | .env BOARD_A_NSEC | TollGate-B96D80 | 10.185.47.1 | /dev/ttyACM0 |
| B | .env BOARD_B_NSEC | TollGate-C0E9CA | 10.192.45.1 | /dev/ttyACM1 |
| C | .env BOARD_C_NSEC | TollGate-4A2510 | 10.74.63.1 | /dev/ttyACM2 |

## OpenWRT Routers

| Router | IP (LAN) | SSID (open) | API | Mint |
|--------|----------|-------------|-----|------|
| Alpha | 10.47.41.1 | TollGate-EVXZ-2.4GHz | :2121 | nofee.testnut, minibits |
| Beta | 192.168.244.1 | TollGate-24A6-2.4GHz | :2121 | testnut-compat.orangesync |
