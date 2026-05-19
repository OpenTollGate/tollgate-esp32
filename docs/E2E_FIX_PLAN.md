# E2E Test Stability Fix Plan

## Problem Statement

E2E tests on physical Board A are failing due to four root causes:
1. **LWIP socket exhaustion** (RC-0) — wifistr WebSockets consume all 10 LWIP sockets
2. **Port 80 captive portal crashes** under load (RC-1)
3. **Owner auto-grant** makes "no internet before auth" tests non-deterministic (RC-2)
4. **No boot-ready probe** — tests start before HTTP servers are up (RC-3)

### Baseline Test Results

| Suite | Pass | Fail | Notes |
|---|---|---|---|
| Smoke | 2/6 | 4 | Port 80 unresponsive, cascading failures |
| Network | 4/7 | 3 | DNS forward + ping after auth (timing) |
| API | 16/20 | 4 | Portal port 80 slow/crashed, captive URIs |
| DNS+Firewall | 15/16 | 1 | Ping after auth (timing) |
| Reset-Auth | 12/15 | 3 | Allotment was 0 (fixed), 2nd payment |
| Session | 14/14 | 0 | Perfect |
| Phase 2 | 12/12 | 0 | Perfect |

---

## Root Causes

### RC-0: LWIP socket exhaustion (CRITICAL)

`CONFIG_LWIP_MAX_SOCKETS=10` in sdkconfig. Socket budget at steady state:

| Component | Sockets | Notes |
|---|---|---|
| Captive portal (port 80) | 5 | 1 listen + 4 workers (`max_open_sockets=4` default) |
| API server (port 2121) | 5 | 1 listen + 4 workers |
| DNS server (UDP 53) | 1 | |
| DoT reject (TCP 853) | 1 | |
| wifistr WebSocket x2 | 2 | relay.damus.io + nos.lol |
| **Total** | **14** | **Exceeds LWIP_MAX_SOCKETS=10 by 4** |

When wifistr opens WebSocket connections (~17s after boot), it exhausts all
available LWIP sockets. The httpd listening sockets are already bound but
worker sockets can't accept new connections. TCP SYN gets RST. ICMP (ping)
still works because it doesn't use LWIP sockets.

**Symptoms observed**:
- Serial log shows "Captive portal started on port 80" and "API started on port 2121"
- `ping 10.192.45.1` works but `curl` gets "Connection refused"
- `nmap -p 80,2121` shows both ports "closed"
- Board is alive (serial shows wifistr publishing) but HTTP servers are non-functional
- Even the original (unmodified) firmware exhibits this after erase+reflash

**Fix**: Increase `CONFIG_LWIP_MAX_SOCKETS` to 16. Reduce `max_open_sockets` to 2
on both servers (saves 4 sockets: 2+2 instead of 4+4 workers).

### RC-1: Port 80 captive portal crashes

The `portal_handler` does per-request `malloc` (~4KB) + two-pass `strstr()`
template substitution. No caching. OS captive detection probes flood 4 sockets
simultaneously. No `Connection: close` header means clients hold sockets open.

**Fix**: Add `Connection: close` to all handlers. Increase stack to 16384.

### RC-2: Owner auto-grant

`tollgate_core_client_connected()` grants firewall access to the first WiFi
client unconditionally. IP is passed as `0` (bug), creating nondeterministic
behavior for "no internet before auth" tests.

**Fix**: Remove `tollgate_core_fw_grant()` call. Keep owner tracking for logging.

### RC-3: No boot-ready probe

Tests use fixed sleeps after flash. No polling for HTTP server readiness.

**Fix**: Add `arch-wait-ready` Makefile target that polls `:2121/usage`.

---

## Fix Steps

### Step 0: Fix LWIP socket exhaustion (PREREQUISITE)
- [x] Set `CONFIG_LWIP_MAX_SOCKETS=16` via sdkconfig
- [x] Set `max_open_sockets = 2` on both HTTP servers
- [ ] Verify TCP connections work after rebuild + flash

**Files**: `sdkconfig`, `main/captive_portal.c`, `main/tollgate_api.c`

### Step 1: Kill owner auto-grant
- [x] Remove `tollgate_core_fw_grant()` from `tollgate_core_client_connected()`
- [x] Keep owner tracking for logging
- [ ] Verify tests pass without auto-grant

**Files**: `components/tollgate_core/src/tollgate_core.c`

### Step 2: Add `Connection: close` to all port 80 handlers
- [x] Add `httpd_resp_set_hdr(req, "Connection", "close")` to every handler
- [x] Increase captive portal stack from 8192 to 16384
- [ ] Verify port 80 stability under load

**Files**: `main/captive_portal.c`

### Step 3: Add `/portal-config` API endpoint
- [x] Add `GET /portal-config` on port 2121 returning `{priceSats, mintUrl, ...}`
- [x] Returns CORS header `Access-Control-Allow-Origin: *`
- [ ] Verify endpoint returns correct JSON

**Files**: `main/tollgate_api.c`

### Step 4: Remove NAPT flush from `fw_revoke_all()`
- [x] Remove `ip_napt_enable()` toggle that was causing 30s hangs
- [ ] Verify `/reset_authentication` responds instantly

**Files**: `components/tollgate_core/src/tollgate_core_firewall.c`

### Step 5: Boot-ready probe in test infrastructure
- [ ] Add `arch-wait-ready` Makefile target that polls `:2121/usage`
- [ ] Update `arch-test-full` to call `arch-wait-ready` first
- [ ] Verify tests work immediately after flash

**Files**: `physical-router-test-automation/esp32/Makefile`

### Step 6: Rebuild and validate
- [ ] Rebuild firmware with all fixes
- [ ] Flash to Board A
- [ ] Run `make arch-test-full`
- [ ] Document results in this file

---

## Key Architecture Decisions

- **Port 80**: Portal HTML + captive detection URIs only. No API, no state mutation.
- **Port 2121**: All API operations (discovery, payment, grant, reset, whoami, usage, wallet, portal-config).
- **Owner tracking**: Kept for logging/display, no longer grants free internet.
- **Connection: close**: Set on ALL port 80 responses to free sockets immediately.
- **max_open_sockets = 2**: Conservative to leave headroom for DNS, DoT, wifistr.

## Target Test Results

| Suite | Target | Stretch |
|---|---|---|
| Smoke | 6/6 | 6/6 |
| Network | 6/7 | 7/7 |
| API | 18/20 | 20/20 |
| DNS+Firewall | 15/16 | 16/16 |
| Reset-Auth | 15/15 | 15/15 |
| Session | 14/14 | 14/14 |
| Phase 2 | 12/12 | 12/12 |

## Execution Order

0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6
(Socket fix -> Owner fix -> Connection close -> Portal config API -> NAPT fix -> Boot probe -> Validate)

## Blockers

### Board A needs physical power cycle (ACTIVE)

After ~20 erase/flash cycles during debugging, Board A's WiFi AP stopped
routing TCP traffic. ICMP (ping) works but all TCP ports return RST
(connection refused). Serial shows HTTP servers starting successfully
but connections from the host are rejected.

**Symptoms**:
- `ping 10.192.45.1` works
- `curl http://10.192.45.1:2121/` → "Connection refused"
- `nmap -p 80,2121` → both "closed"
- Serial: "Captive portal started on port 80", "API started on port 2121"
- Even the original (unmodified) firmware exhibits this issue
- Board produces serial output (not fully crashed) but TCP is broken

**Fix**: Physical power cycle (unplug USB, wait 10s, reconnect).
RTS/DTR reset via esptool is NOT sufficient.

### LWIP socket exhaustion still needs validation

`CONFIG_LWIP_MAX_SOCKETS=16` and `max_open_sockets=2` have been set in the
code but not yet validated on a working board. The theory is correct
(14 sockets needed, 10 available) but the actual fix hasn't been verified.

### All code changes are committed (c89ab31)

Steps 0-4 are implemented in the codebase. Steps 5-6 require a working board.
