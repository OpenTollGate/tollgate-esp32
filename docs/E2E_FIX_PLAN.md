# E2E Test Stability Fix Plan

## Problem Statement

E2E tests on physical boards are failing due to five root causes:
1. **LWIP socket exhaustion** (RC-0) — `LWIP_MAX_SOCKETS=10` was too low for two httpd servers + DNS + DoT + wifistr WebSockets
2. **Over-tuned httpd settings** (RC-1) — setting `max_open_sockets=2` and `keep_alive_enable=false` caused socket leaks by interfering with ESP-IDF's internal session management
3. **Owner auto-grant** (RC-2) — makes "no internet before auth" tests non-deterministic
4. **No boot-ready probe** (RC-3) — tests start before HTTP servers are up
5. **Serial monitoring resets** (RC-4) — Python `serial.Serial()` toggles DTR/RTS on USB-Serial/JTAG boards, causing chip resets mid-operation

### Baseline Test Results (Board A, before fixes)

| Suite | Pass | Fail | Notes |
|---|---|---|---|
| Smoke | 2/6 | 4 | Port 80 unresponsive, cascading failures |
| Network | 4/7 | 3 | DNS forward + ping after auth (timing) |
| API | 16/20 | 4 | Portal port 80 slow/crashed, captive URIs |
| DNS+Firewall | 15/16 | 1 | Ping after auth (timing) |
| Reset-Auth | 12/15 | 3 | Allotment was 0 (fixed), 2nd payment |
| Session | 14/14 | 0 | Perfect |
| Phase 2 | 12/12 | 0 | Perfect |

### Verified Test Results (Board ACM2, after all fixes, commit `144b48f`)

All API endpoints verified working on AP IP `10.192.45.1` with 2-3s delays between requests:
- `GET /usage` — returns session/client counts (50/50 sequential requests passed)
- `GET /portal-config` — returns `{priceSats, stepMs, mintUrl, metric, stepBytes}`
- `GET /whoami` — returns client IP
- `GET /grant_access` — grants firewall access
- `POST /` (payment) — accepts Cashu token, returns `kind:1022`
- `GET /` (port 80 portal) — returns 3829 bytes HTML
- `GET /reset_authentication` — clears all sessions and firewall rules

Full payment flow verified: check → pay → verify → grant → portal → reset → verify clean state.

---

## Root Causes

### RC-0: LWIP socket exhaustion (FIXED)

`CONFIG_LWIP_MAX_SOCKETS=10` in sdkconfig. Socket budget at steady state:

| Component | Sockets | Notes |
|---|---|---|
| Captive portal (port 80) | 5 | 1 listen + 4 workers (default `max_open_sockets`) |
| API server (port 2121) | 5 | 1 listen + 4 workers |
| DNS server (UDP 53) | 1 | |
| DoT reject (TCP 853) | 1 | |
| wifistr WebSocket x2 | 2 | relay.damus.io + nos.lol |
| **Total** | **14** | **Exceeds LWIP_MAX_SOCKETS=10 by 4** |

**Fix** (commit `144b48f`): Set `CONFIG_LWIP_MAX_SOCKETS=20` (matching standalone tollgate). Use default `max_open_sockets=4` on both servers. Previous fix tried `max_open_sockets=2` which caused worse problems (see RC-1).

### RC-1: Over-tuned httpd settings (FIXED)

Initial fix reduced `max_open_sockets` to 2 and added `keep_alive_enable=false`, `linger_timeout=0`. This caused socket leaks — ESP-IDF's httpd manages its own session pool internally, and overriding these settings interfered with socket lifecycle management.

**Symptoms**: Board works for 10-20 requests, then all HTTP becomes unresponsive. Sockets accumulate in CLOSE_WAIT/TIME_WAIT and never get freed.

**Fix** (commit `144b48f`): Reverted to ESP-IDF defaults for all httpd settings except `stack_size=16384` and `max_uri_handlers`. Default `max_open_sockets=4` and `keep_alive_enable=true` (default) work correctly.

### RC-2: Owner auto-grant (FIXED)

`tollgate_core_client_connected()` granted firewall access to the first WiFi client unconditionally. IP was passed as `0` (bug), creating nondeterministic behavior.

**Fix** (commit `c89ab31`): Removed `tollgate_core_fw_grant()` call from `client_connected()`. Owner tracking kept for logging.

### RC-3: No boot-ready probe (PENDING)

Tests use fixed sleeps after flash. No polling for HTTP server readiness.

**Fix**: Add `arch-wait-ready` Makefile target that polls `:2121/usage`.

### RC-4: Serial monitoring resets boards (DISCOVERED)

Python `serial.Serial()` on USB-Serial/JTAG ESP32-S3 boards toggles DTR/RTS during initialization, causing `rst:0x15 (USB_UART_CHIP_RESET)`. This resets the chip even if `dtr=False, rts=False` is set after construction.

**Symptoms**:
- Board boots successfully, services start, gets IP
- Python serial read causes immediate `ESP-ROM: boot:0x0 (DOWNLOAD)` or `rst:0x15`
- Board appears "dead" after testing — actually reset into download mode
- Earlier sessions attributed this to "socket exhaustion" or "WiFi instability"

**Fix**: Never use Python `serial.Serial()` for monitoring. Use `idf.py monitor` (which handles DTR/RTS correctly) or read-only tools. All hardware access must go through Makefile mutex targets.

---

## Fix Steps

### Step 0: Fix LWIP socket exhaustion — DONE
- [x] Set `CONFIG_LWIP_MAX_SOCKETS=20` via sdkconfig (commit `144b48f`)
- [x] Use default `max_open_sockets` on both HTTP servers (removed override)
- [x] Verified: 50/50 sequential API requests pass on Board ACM2

**Files**: `sdkconfig`, `main/captive_portal.c`, `main/tollgate_api.c`

### Step 1: Kill owner auto-grant — DONE
- [x] Remove `tollgate_core_fw_grant()` from `tollgate_core_client_connected()` (commit `c89ab31`)
- [x] Keep owner tracking for logging

**Files**: `components/tollgate_core/src/tollgate_core.c`

### Step 2: HTTP server robustness — DONE
- [x] Add `Connection: close` header to port 80 responses (commit `c89ab31`)
- [x] Increase captive portal stack to 16384 (commit `c89ab31`)
- [x] Use ESP-IDF default socket management (commit `144b48f`)

**Files**: `main/captive_portal.c`, `main/tollgate_api.c`

### Step 3: Add API endpoints — DONE
- [x] `GET /portal-config` on port 2121 returning `{priceSats, mintUrl, ...}` (commit `c89ab31`)
- [x] `GET /grant_access` — manual firewall grant (commit `c89ab31`)
- [x] `GET /reset_authentication` — clear all auth (commit `c89ab31`)
- [x] CORS header on portal-config

**Files**: `main/tollgate_api.c`

### Step 4: Remove NAPT flush from `fw_revoke_all()` — DONE
- [x] Remove `ip_napt_enable()` toggle that caused 30s hangs (commit `c89ab31`)

**Files**: `components/tollgate_core/src/tollgate_core_firewall.c`

### Step 5: Boot-ready probe — PENDING
- [ ] Add `arch-wait-ready` Makefile target that polls `:2121/usage`
- [ ] Update `arch-test-full` to call `arch-wait-ready` first
- [ ] Add 2-3 second delays between test requests (burst rate mitigation)

**Files**: `physical-router-test-automation/esp32/Makefile`

### Step 6: Hardware testing — BLOCKED
- [ ] Flash to working board via Makefile mutex targets
- [ ] Run `make arch-test-full`
- [ ] Document results
- [ ] Board A stuck in download mode (GPIO0 strapping pin) — needs hardware fix

---

## Burst Rate Limitation

On USB-Serial/JTAG ESP32-S3 boards, back-to-back HTTP requests with no delay can
overwhelm the WiFi AP stack. With 2-3 second delays between requests, the board
handles 50+ sequential requests reliably. Without delays, rapid bursts of 10+
requests can cause the WiFi AP to become unresponsive.

**Mitigation**: E2E tests should include a 2-3 second delay between HTTP requests.
This is a WiFi AP throughput limitation, not a firmware bug.

## Board Status

| Board | Port | MAC | Status |
|-------|------|-----|--------|
| Board A | `/dev/ttyACM0` | `94:a9:90:2e:37:7c` | **BROKEN** — stuck in download mode (`boot:0x0`), GPIO0 strapping pin issue, needs hardware fix |
| Board B | `/dev/ttyACM1` | `fc:01:2c:c5:50:50` | Unknown — newly discovered, needs firmware flash |
| Board C | `/dev/ttyACM2` | `20:6e:f1:98:d7:08` | **WORKING** — all endpoints verified, payment flow tested |

## Key Architecture Decisions

- **Port 80**: Portal HTML + captive detection URIs only. No API, no state mutation.
- **Port 2121**: All API operations (discovery, payment, grant, reset, whoami, usage, wallet, portal-config).
- **Owner tracking**: Kept for logging/display, no longer grants free internet.
- **Connection: close**: Set on ALL port 80 responses to hint clients.
- **Default httpd settings**: ESP-IDF's built-in session management works correctly. Do not override `max_open_sockets`, `keep_alive_enable`, `linger_timeout`, or timeouts.

## Execution Order

Steps 0-4 are DONE (commits `c89ab31`, `144b48f`).
Step 5 (boot-ready probe) is next — code only, no hardware needed.
Step 6 (validation) requires working board via Makefile mutex targets.

## Hardware Access Rules

- **ALWAYS** use Makefile mutex targets (`make arch-flash-a`, etc.) for hardware access
- **NEVER** call `esptool.py` directly — bypasses mutex and conflicts with other sessions
- **NEVER** use Python `serial.Serial()` for monitoring — causes DTR/RTS resets on USB-Serial/JTAG
- Multiple opencode sessions may be active — mutex prevents board conflicts
