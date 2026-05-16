# TollGate Interop Test Plan — ESP32 ↔ OpenWRT

## Overview

Cross-platform interoperability tests between ESP32-based TollGate firmware and OpenWRT-based TollGate (tollgate-module-basic-go). Tests verify that Cashu e-cash tokens work across both implementations, and that the OpenWRT Go daemon can auto-pay the ESP32 for upstream internet access.

## Device Inventory

| Device | Access | AP SSID | API | Mint | Metric | Price |
|--------|--------|---------|-----|------|--------|-------|
| OpenWRT (alpha) | SSH `root@10.47.41.1` | `TollGate-EVXZ-2.4GHz` / `TollGate-EVXZ-5GHz` | `http://10.47.41.1:2121/` | `nofee.testnut.cashu.space` | bytes (21MB/step) | 1 sat/step |
| ESP32 Board A | WiFi `10.192.45.1`, Serial `/dev/ttyACM0` | `TollGate-C0E9CA` | `http://10.192.45.1:2121/` | `testnut.cashu.space` | time (21 sats/60s) | 21 sats/step |
| ESP32 Board B | Serial `/dev/ttyACM1` | TBD | TBD | TBD | TBD | TBD |

## Network Topology

```
                    ┌──────────────────────────────────────────────────┐
                    │                 Internet                         │
                    └───────┬──────────────────────┬───────────────────┘
                            │                      │
                 EnterSSID-5GHz (upstream)    EnterSSID-2.4GHz (upstream)
                            │                      │
                   ┌────────┴────────┐    ┌────────┴────────┐
                   │  OpenWRT Router │    │  ESP32 Board A  │
                   │  (alpha)        │    │  (TollGate-C0E9CA)
                   │  10.47.41.1     │    │  10.192.45.1    │
                   └────────┬────────┘    └────────┬────────┘
                            │                      │
                   TollGate-EVXZ-2.4GHz    TollGate-C0E9CA
                   TollGate-EVXZ-5GHz     (open AP)
                            │                      │
                   ┌────────┴────────┐    ┌────────┴────────┐
                   │  Laptop (eth0)  │    │  Laptop (wlan0) │
                   │  10.47.41.106   │    │  10.192.45.2    │
                   └─────────────────┘    └─────────────────┘
```

## Mint Alignment Strategy

Both mints are test mints that auto-pay lightning invoices. For cross-platform interop, both devices accept tokens from either mint.

| Mint | Auto-pay | Used by |
|------|----------|---------|
| `testnut.cashu.space` | Yes | ESP32 (native), added to OpenWRT |
| `nofee.testnut.cashu.space` | Yes | OpenWRT (native), added to ESP32 |

### Configuration Changes

**OpenWRT** — add `testnut.cashu.space` to `accepted_mints` in `/etc/tollgate/config.json` via SSH + jq.

**ESP32** — add `nofee.testnut.cashu.space` to `mint_url` in `config.json` on SPIFFS. Requires rebuild + reflash.

## Token Format Compatibility

| Platform | V3 (cashuA) | V4 (cashuB/CBOR) |
|----------|-------------|-------------------|
| ESP32 | **Accepted** (only format supported) | Not supported |
| OpenWRT | Accepted | Accepted |

Token generation:
- **For ESP32**: `cashu --env-mint testnut.cashu.space send --legacy 21` → V3
- **For OpenWRT**: `mint-token` Go binary → V4 (preferred), or `cashu --legacy` → V3

## Test Scenarios

### Scenario 1: Laptop → ESP32 (Already Works)

Laptop connects to ESP32 AP, mints V3 token, pays ESP32 TollGate API, verifies internet.

This is the existing `make test-payment` flow, wrapped into the interop Makefile for consistency.

### Scenario 2: Laptop → OpenWRT

Laptop connects to OpenWRT AP (or uses existing ethernet connection), mints V4 token, pays OpenWRT TollGate API, verifies internet.

**Steps:**
1. Verify laptop can reach OpenWRT at `10.47.41.1`
2. Check API advertisement at `http://10.47.41.1:2121/` (kind=10021)
3. Mint V4 token via `mint-token` binary
4. POST token to `http://10.47.41.1:2121/`
5. Verify kind=1022 session response
6. Verify internet via ping

### Scenario 3: OpenWRT → ESP32 (Reseller)

OpenWRT connects its STA to ESP32's TollGate AP. OpenWRT's Go daemon auto-detects the TollGate upstream and pays with its wallet. ESP32 grants session.

**Steps:**
1. Verify both devices accessible
2. Fund ESP32 wallet (for receiving payment)
3. Fund OpenWRT wallet (for paying upstream)
4. Save OpenWRT's current upstream SSID
5. Connect OpenWRT STA to `TollGate-C0E9CA` (ESP32's AP)
6. Wait for DHCP + upstream detection
7. Watch for auto-payment logs on OpenWRT
8. Verify session on ESP32 (via serial or API)
9. Restore OpenWRT upstream
10. Restore production configs

### Scenario 4: ESP32 → OpenWRT (Future)

ESP32 connects its STA to OpenWRT's TollGate AP. Requires ESP32 firmware to have TollGate client detection + auto-payment logic — **not yet implemented**.

### Scenario 5: ESP32 ↔ ESP32

Board A connects to Board B's AP (or vice versa), cross-payment test. Requires Board B to be flashed with unique nsec + funded wallet.

**Steps:**
1. Flash Board B with different nsec
2. Configure and fund both boards
3. Board A connects STA to Board B's AP
4. Manual curl payment test (POST token)
5. Verify session + internet

## Makefile Target Reference

| Target | Scenario | Description |
|--------|----------|-------------|
| `interop-status` | — | Show TollGate status for all devices |
| `interop-setup-mints` | — | Add both mints to both devices |
| `interop-fund-esp32` | — | Fund ESP32 wallet with test tokens |
| `interop-fund-openwrt` | — | Fund OpenWRT wallet with test tokens |
| `interop-setup` | — | Full setup: mints + fund both |
| `interop-laptop-esp32` | 1 | Laptop pays ESP32 |
| `interop-laptop-openwrt` | 2 | Laptop pays OpenWRT |
| `interop-openwrt-esp32` | 3 | OpenWRT auto-pays ESP32 for upstream |
| `interop-esp32-esp32` | 5 | Cross-board payment |
| `interop-cleanup` | — | Restore original configs on all devices |

## Prerequisites

- Laptop connected to OpenWRT via ethernet (`enx00e04c683d2d`, `10.47.41.106`)
- Laptop connected to ESP32 via WiFi (`wlp59s0`, `10.192.45.2`)
- `mint-token` binary built: `cd physical-router-test-automation/scripts/mint-token && go build -o /tmp/mint-token .`
- `cashu` CLI installed: `pip install cashu`
- SSH key auth to OpenWRT: `ssh-copy-id root@10.47.41.1`
- ESP32 Board A flashed and running with funded wallet

## Key Technical Notes

- OpenWRT uses `tollgate upstream connect <ssid>` CLI to switch upstream
- OpenWRT's daemon auto-detects TollGate upstream via HTTP GET to `:2121/` (kind=10021)
- ESP32 only accepts V3 tokens (`cashuA` prefix); OpenWRT accepts both V3 and V4
- The `mint-token` binary mints from `nofee.testnut.cashu.space` and produces V4 tokens
- The `cashu` CLI with `--legacy` flag produces V3 tokens
- ESP32 has no TollGate client logic — cannot auto-pay upstream TollGates (future work)
- OpenWRT's `tollgate wallet fund` accepts piped V4 tokens
- ESP32's `POST /wallet/receive` accepts V3 tokens (via nucula)
