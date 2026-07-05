# Car Solar TollGate — Architecture Document

> DECIDED 2026-07-06: GL.iNet Mudi V2 (GL-E750V2) as primary TollGate gateway
> Pi Zero 2 W, ESP32-S3 boards, and BitAxe miners as downstream clients/resellers

## Overview

An old car equipped with a 200W solar panel and Anker Solix C1000 (1000Wh) power bank
becomes a self-sustaining TollGate node. Excess solar energy (when battery is full)
powers BitAxe Bitcoin miners. The miners earn ecash via Stratum V2.

**Primary gateway:** GL-E750 Mudi V2 (OpenWrt router with built-in 4G LTE, battery)
**TollGate software:** tollgate-module-basic-go (Go daemon on OpenWrt)
**Clients:** BitAxe miners + ESP32-S3 boards buy internet via Cashu
**Resellers:** ESP32-S3 boards can re-sell internet inside the car
**Interop:** Go-based TollGate (GL-E750) and C-based TollGate (ESP32) must interoperate

## System Architecture

```
                    ┌─────────────────────────────────┐
                    │         SOLAR (200W)             │
                    │    ┌─────────────────────┐       │
                    │    │   Solar Panel       │───────┼──→ Anker Solix C1000 (1000Wh)
                    │    │   (roof-mounted)    │       │    ├── 220V AC output
                    │    └─────────────────────┘       │    ├── Bluetooth (BLE monitoring)
                    └─────────────────────────────────┘    └── ContextVM wrapper
                                          │
                                    Excess energy
                                    (battery full, >80%)
                                          │
                                          ▼
                    ┌──────────────────────────────────────────┐
                    │  GL-E750 MUDI V2 (PRIMARY GATEWAY)       │
                    │  ┌────────────────────────────────────┐  │
                    │  │ OpenWrt + TollGate Go              │  │
                    │  │ ├── 4G LTE (EM060K-G, Cat-4)       │  │
                    │  │ ├── WiFi AP (TollGate captive portal)│  │
                    │  │ ├── Cashu payment processing        │  │
                    │  │ ├── ndsctl MAC authorization        │  │
                    │  │ ├── Reseller/client mode (to upstream)│  │
                    │  │ ├── 7000mAh battery (UPS)           │  │
                    │  │ └── Ethernet WAN/LAN                │  │
                    │  └────────────────────────────────────┘  │
                    └──────────────────┬───────────────────────┘
                                       │
                              WiFi AP (TollGate-XXXX, open)
                              Cashu for data (time or bytes)
                                       │
         ┌─────────────────────┬───────┴───────┬─────────────────────┐
         │                     │               │                     │
    BitAxe #1             BitAxe #2        ESP32-S3 #A          ESP32-S3 #B
    (miner)               (miner)          (TollGate             (sensors/
     Stratum V2                              client +              relays)
     Pays GL-E750                            reseller)             GPS, IMU,
         │                     │               │                   Hall, IRF520
         │                     │          Creates own AP
         │                     │          TollGate-Group-XXXX       │
         │                     │               │                   │
         └─────────────────────┴───────┬───────┴───────────────────┘
                                       │
                               ┌───────▼────────┐
                               │    INTERNET     │
                               │                 │
                               │ Mining pool     │
                               │ Nostr relays    │
                               │ Cashu mints     │
                               └─────────────────┘
```

## Repositories

| Repo | Path | Role |
|------|------|------|
| tollgate-module-basic-go | /home/c03rad0r/tollgate-module-basic-go | Primary TollGate on GL-E750 (Go, OpenWrt) |
| esp32-tollgate | /home/c03rad0r/esp32-tollgate | ESP32-S3 TollGate client/reseller (C, ESP-IDF) |
| esp-miner-tollgate | /home/c03rad0r/esp-miner-tollgate | BitAxe miner fork with TollGate client |
| contextvm-anker-solix | /home/c03rad0r/contextvm-anker-solix | Anker Solix battery monitoring via BLE + MCP |

## Device Roles

| Device | Role | Runs TollGate? | Upstream | Downstream |
|--------|------|----------------|----------|------------|
| GL-E750 Mudi V2 | Primary gateway | ✅ Go (OpenWrt) | 4G LTE (EM060K-G) | WiFi AP + Ethernet |
| ESP32-S3 #A | Client / reseller | ✅ C (ESP-IDF) | WiFi → GL-E750 | WiFi AP (subnet) |
| ESP32-S3 #B | Sensors / relays | ❌ (no TollGate) | WiFi → GL-E750 or #A | GPS, IMU, Hall, relays |
| Pi Zero 2 W | Car-local Hermes | ❌ (Hermes only) | WiFi → GL-E750 | — |
| BitAxe #1 | Miner + TollGate client | ✅ C (esp-miner) | Stratum V2 (internet) | BM1397 ASIC |
| BitAxe #2 | Miner + TollGate client | ✅ C (esp-miner) | Stratum V2 (internet) | BM1397 ASIC |

## Network Topology (Car)

```
[4G SIM (SilentLink eSIM)] → GL-E750 Mudi V2
  IP: 192.168.1.1 (or GL.iNet default 192.168.8.1)
  SSID: TollGate-XXXX  (open WiFi, no WPA)
  Captive portal: ndsctl + TollGate Go daemon
  Clients: BitAxe, ESP32, Pi, phone
       │
       ├── BitAxe #1 (10.0.0.2)
       ├── BitAxe #2 (10.0.0.3)
       ├── ESP32 #A  (10.0.0.4)
       │   └── WiFi AP: TollGate-Group-XXXX (subnet)
       │       ├── devices inside car
       │       └── pays GL-E750, resells to its own clients
       ├── ESP32 #B  (10.0.0.5)
       └── Pi Zero 2 W (10.0.0.6)
           └── Hermes agent, ContextVM MCP for sensors
```

## Interoperability Test (CTG-INTEROP)

**Goal:** Verify Go-based TollGate (GL-E750) and C-based TollGate (ESP32) interoperate:
1. A BitAxe connects to GL-E750's WiFi → hits TollGate portal → pays Cashu → internet
2. An ESP32 connects to GL-E750's WiFi → pays Cashu → gets internet
3. The ESP32 resells internet on its own AP → another device connects and pays the ESP32
4. The ESP32's Cashu tokens are valid against the same mints used by GL-E750

**Test cases:**
- `test_go_portal_serves_portal_html` — GL-E750 serves captive portal
- `test_go_accepts_cashu_payment` — GL-E750 processes Cashu token
- `test_go_authorizes_mac` — GL-E750 opens gate for paying client
- `test_esp32_client_buys_from_gl` — ESP32 connects as TollGate client
- `test_esp32_client_resells` — ESP32 creates its own AP and resells
- `test_bitaxe_buys_via_esp32` — BitAxe pays ESP32's portal
- `test_go_esp32_cross_mint` — Both use same Cashu mint

## GL-E750 Setup Path

```
1. Flash GL-E750 with OpenWrt (or confirm GL.iNet firmware supports opkg)
2. Install nds (NoDogSplash): opkg install nds
3. Install tollgate-wrt: opkg install tollgate-wrt_<version>_mipsel_24kc.ipk
4. Configure /etc/tollgate/config.json
5. Configure 4G modem (EM060K-G) via GL.iNet web UI or uci
6. Start tollgate service: /etc/init.d/tollgate start
7. Test: connect to WiFi → captive portal → pay Cashu → internet
```

## Economics Model (Target)

```
GL-E750:
  Cost: 4G data plan (SilentLink, Lightning-funded)
  Revenue: Cashu payments from BitAxe + ESP32 + phone clients
  Target: revenue > data plan cost (positive margin)

BitAxe:
  Cost: Cashu payments to GL-E750 (or ESP32 reseller)
  Revenue: Bitcoin/ecash mined via Stratum V2
  Target: mined value > internet cost (positive margin)

Energy:
  Cost: 0 (solar excess, battery already full)
  Constraint: only mines when Anker Solix > threshold (e.g. 80%)
```

## Key Components

### 1. GL-E750 Mudi V2
- OpenWrt-based 4G LTE router with 7000mAh battery
- Built-in Quectel EM060K-G (Cat-4, 150/50 Mbps)
- Runs GL.iNet firmware (OpenWrt-based) — can be flashed to standard OpenWrt
- TollGate: install via opkg or build from tollgate-module-basic-go
- Architecture: mipsel_24kc (MediaTek MT7621)

### 2. tollgate-module-basic-go (Go on OpenWrt)
- Go daemon for OpenWrt routers
- Produces .ipk packages (CI builds for mipsel_24kc)
- Dependencies: nds (NoDogSplash for captive portal), Go runtime
- Config: /etc/tollgate/config.json
- Service: /etc/init.d/tollgate
- CLI: tollgate status, tollgate start/stop/restart

### 3. SilentLink eSIM → Physical SIM
Non-KYC eSIMs topped up via Lightning Network.
The user has a method to flash eSIM profiles to physical SIM cards.

### 4. esp32-tollgate (ESP32 C client/reseller)
- ESP-IDF firmware with tollgate_core component
- Can act as TollGate client (buy from GL-E750 via WiFi STA)
- Can act as TollGate server (resell on its own WiFi AP)
- ContextVM server for sensor data (MCP over Nostr)
- Local Nostr relay for offline-first operation

### 5. BitAxe Miner + TollGate Client
- Stratum V2 mining via esp-miner-tollgate firmware
- Connects as WiFi client to GL-E750
- Pays for internet via Cashu
- Mines Bitcoin/ecash via hashpool

## Power Budget (Estimated)

| Component | Power Draw | Source |
|-----------|-----------|--------|
| GL-E750 Mudi V2 | ~5-10W (WiFi + 4G + routing) | 7000mAh battery / Anker Solix |
| BitAxe (NerdAxe) | ~15-25W each | Anker Solix 220V |
| ESP32-S3 | ~0.5-1W | Anker Solix USB |
| Pi Zero 2 W | ~2-3W | Anker Solix USB |

200W solar panel at ~5 peak sun hours = ~1000Wh/day
Anker Solix capacity = 1000Wh

Mining budget = excess after: battery charging + GL-E750 + ESP32 + Pi

## Status

- [ ] CTG-GL1: GL-E750 OpenWrt / firmware confirmed
- [ ] CTG-GL2: TollGate Go installed and configured
- [ ] CTG-GL3: 4G modem working (SIM card + data)
- [ ] CTG-GL4: BitAxe connects and buys internet
- [ ] CTG-1: ESP32 TollGate client pays GL-E750 (interop test)
- [ ] CTG-ESP-RESELL: ESP32 resells internet
- [ ] CTG-6: ContextVM sensors exposed
- [ ] CTG-7: E2E margin test passed

## Kanban

Board: `car-tollgate`
