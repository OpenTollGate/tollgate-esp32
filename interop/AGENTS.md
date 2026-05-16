# AGENTS.md — Interop Test Standing Instructions

## Overview

Cross-platform interoperability tests for ESP32 TollGate firmware vs OpenWRT TollGate (tollgate-module-basic-go). Makefile-driven tests that verify Cashu e-cash token compatibility and upstream payment flows between the two implementations.

## Standing Instructions

1. **Always maintain these files:**
   - `INTEROP_PLAN.md` — up-to-date interop test plan
   - `PROGRESS.md` — checklist of done/pending items

2. **Testing requirements:**
   - All interop targets must be idempotent — safe to re-run
   - Cleanup targets must restore original state
   - Never leave a device in a broken state

3. **Commit discipline:**
   - Commit every time a test scenario passes end-to-end
   - Push after each commit

4. **No comments in code** unless explicitly requested

5. **No secrets in git** — all secrets in `routers.env` (gitignored) or `.env`

6. **Device safety:**
   - Always save upstream state before changing it
   - Always restore upstream state after tests
   - Use router mutex (if shared with other sessions)

## Repository Context

This directory lives inside `esp32-tollgate/interop/`. The parent repo has its own `AGENTS.md` with firmware testing rules. This file covers interop testing only.

## Device Access

| Device | Transport | Address | Notes |
|--------|-----------|---------|-------|
| OpenWRT (alpha) | SSH | `root@10.47.41.1` | Ethernet at `enx00e04c683d2d` |
| ESP32 Board A | WiFi API | `10.192.45.1` | WiFi at `wlp59s0`, SSID `TollGate-C0E9CA` |
| ESP32 Board A | Serial | `/dev/ttyACM0` | USB serial, 115200 baud |
| ESP32 Board B | Serial | `/dev/ttyACM1` | USB serial, 115200 baud |

## Token Generation

| Target | Tool | Format | Command |
|--------|------|--------|---------|
| ESP32 | `cashu` CLI | V3 (cashuA) | `cashu --env-mint testnut.cashu.space send --legacy 21` |
| OpenWRT | `mint-token` | V4 | `/tmp/mint-token nofee.testnut.cashu.space 1` |

**ESP32 only accepts V3 tokens.** OpenWRT accepts both V3 and V4.

## Mint URLs

| Mint | URL | Auto-pay |
|------|-----|----------|
| testnut | `https://testnut.cashu.space` | Yes |
| nofee-testnut | `https://nofee.testnut.cashu.space` | Yes |

Both must be in both devices' accepted_mints for cross-platform payment.

## Key Commands

```bash
# Show all device status
make interop-status

# Full setup (mints + wallets)
make interop-setup

# Run individual scenarios
make interop-laptop-esp32
make interop-laptop-openwrt
make interop-openwrt-esp32
make interop-esp32-esp32

# Cleanup after tests
make interop-cleanup
```

## Network Interfaces

| Interface | Device | Purpose |
|-----------|--------|---------|
| `enx00e04c683d2d` | Laptop | Ethernet to OpenWRT LAN (`10.47.41.x`) |
| `wlp59s0` | Laptop | WiFi to ESP32 AP (`10.192.45.x`) |

The laptop has simultaneous connectivity to both devices via different interfaces.

## Troubleshooting

- **OpenWRT unreachable**: Check ethernet cable, `ip addr show enx00e04c683d2d`
- **ESP32 unreachable**: Check WiFi connection, `nmcli dev wifi connect TollGate-C0E9CA`
- **Token rejected**: Check mint URL matches accepted_mints on target device
- **OpenWRT won't auto-pay**: Check wallet balance > 0, check daemon logs `logread -e tollgate-wrt -f`
- **ESP32 serial**: `python3 -m serial.tools.miniterm /dev/ttyACM0 115200`
