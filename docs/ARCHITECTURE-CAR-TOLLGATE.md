# Car Solar TollGate — Architecture Document

> Self-sustaining mobile node: solar energy → Bitcoin mining → ecash → internet access → sensor exposure

## Overview

An old car equipped with a 200W solar panel and Anker Solix C1000 (1000Wh) power bank
becomes a self-sustaining TollGate node. Excess solar energy (when battery is full)
powers BitAxe Bitcoin miners. The miners earn ecash via Stratum V2 / hashpool. A
GSM-connected ESP32 provides mobile data upstream and sells it via TollGate (Cashu
payments). Both sides achieve positive margin: the GSM gateway profits on data resale,
and the BitAxes mine more ecash than they spend on internet.

The end result: the car has "free" internet paid for by solar excess, and ContextVM
exposes battery state, OBD data, and sensors over the internet via MCP (Model Context
Protocol over Nostr).

## System Architecture

```
                    ┌─────────────────────────────────┐
                    │         SOLAR (200W)             │
                    │    ┌─────────────────────┐       │
                    │    │   Solar Panel       │───────┼──→ Anker Solix C1000 (1000Wh)
                    │    │   (roof-mounted)    │       │    ├── 220V AC output
                    │    └─────────────────────┘       │    ├── Bluetooth (BLE monitoring)
                    └─────────────────────────────────┘    └── ContextVM wrapper (existing)
                                          │
                                    Excess energy
                                    (battery full)
                                          │
                                          ▼
                    ┌─────────────────────────────────────┐
                    │        BITAXE MINER(s)               │
                    │  ESP32-S3 + BM1397 ASIC             │
                    │  ┌───────────────────────────────┐   │
                    │  │ esp-miner-tollgate firmware   │   │
                    │  │ ├── Stratum V2 mining         │   │
                    │  │ ├── TollGate client (Cashu)   │   │
                    │  │ ├── Pays for internet         │   │
                    │  │ └── Mines → earns ecash       │   │
                    │  └───────────────────────────────┘   │
                    └───────────────┬─────────────────────┘
                                    │
                            WiFi AP (TollGate-XXXX)
                            Pays Cashu for data
                                    │
                                      ▼
                    ┌─────────────────────────────────────┐
                    │     GSM UPSTREAM ESP32               │
                    │  ESP32-S3 + GSM shield               │
                    │  ┌───────────────────────────────┐   │
                    │  │ esp32-tollgate firmware       │   │
                    │  │ ├── WiFi AP (TollGate gateway) │   │
                    │  │ ├── Cashu payment processing  │   │
                    │  │ ├── Captive portal            │   │
                    │  │ ├── PPP/GSM upstream          │   │
                    │  │ ├── ContextVM (MCP/Nostr)     │   │
                    │  │ ├── Local Nostr relay         │   │
                    │  │ └── Sells mobile data         │   │
                    │  └───────────────────────────────┘   │
                    │        │                             │
                    │   SIM card (SilentLink eSIM)         │
                    │   Non-KYC, Lightning top-up          │
                    └────────┼────────────────────────────┘
                             │
                        Mobile data (3G/4G/LTE)
                             │
                             ▼
                    ┌─────────────────────────────────────┐
                    │          INTERNET                    │
                    │  ├── Mining pool (SV2/SV1)          │
                    │  ├── Cashu mint (hashpool)          │
                    │  ├── Nostr relays                   │
                    │  └── ContextVM endpoints (public)   │
                    └─────────────────────────────────────┘
```

## Repositories

| Repo | Path | Role |
|------|------|------|
| esp32-tollgate | /home/c03rad0r/esp32-tollgate | ESP32-S3 TollGate firmware (standalone, mature) |
| esp-miner-tollgate | /home/c03rad0r/esp-miner-tollgate | BitAxe miner fork with TollGate integration plan |
| contextvm-anker-solix | /home/c03rad0r/contextvm-anker-solix | Anker Solix battery monitoring via BLE + MCP |
| axepool | /home/c03rad0r/axepool | SV1/SV2 mining proxy with ehash mint integration |
| tollgate-module-basic-go | /home/c03rad0r/tollgate-module-basic-go | Go backend for OpenWrt routers (reference impl) |

## Device Roles (Phase 1)

| Device | Firmware | Upstream | Downstream | Role |
|--------|----------|----------|------------|------|
| GSM ESP32 | esp32-tollgate | GSM/LTE (SilentLink SIM) | WiFi AP (TollGate) | Sells mobile data |
| BitAxe(s) | esp-miner-tollgate | WiFi STA → GSM ESP32's AP | — | Mines Bitcoin, pays for internet |

### Future: Interchangeable roles

Eventually any ESP32 should be able to act as miner, gateway, or both simultaneously.
The tollgate_core component extraction enables this — the same code runs in different
modes depending on available upstream (WiFi STA, GSM, or both).

## Economics Model (Target)

```
GSM ESP32:
  Cost: mobile data plan (SilentLink, Lightning-funded)
  Revenue: Cashu payments from BitAxes for internet access
  Target: revenue > cost (positive margin)

BitAxe:
  Cost: Cashu payments to GSM ESP32 for internet
  Revenue: Bitcoin/ecash mined via Stratum V2
  Target: mined value > internet cost (positive margin)

Energy:
  Cost: 0 (solar excess, battery already full)
  Constraint: only mines when Anker Solix > threshold (e.g. 80%)
```

## Key Components

### 1. SilentLink eSIM → Physical SIM

SilentLink (silent.link) provides non-KYC eSIMs topped up via Lightning Network.
The user has a method to flash eSIM profiles onto physical SIM cards for use in
GSM shields.

**Status:** Need to determine which GSM shield model and its pin mapping.

### 2. GSM Shield → ESP32 PPP

ESP-IDF supports PPP (Point-to-Point Protocol) over serial for GSM connectivity.
The GSM shield connects via UART, provides AT command interface, and enters
data/PPP mode for IP connectivity.

**Integration point:** esp32-tollgate's `tollgate_main.c` — add GSM init alongside
existing WiFi STA init. PPP becomes an alternative netif for upstream.

### 3. tollgate_core Component Extraction

The esp32-tollgate firmware's core modules (cashu, dns, firewall, session) are being
extracted into a reusable `tollgate_core` ESP-IDF component. This enables:
- esp-miner-tollgate to consume it as a dependency (BitAxe becomes TollGate client)
- Any ESP32 project to add TollGate payment functionality
- Shared codebase, single source of truth

**Blocked by:** 4 feature branches must merge to esp32-tollgate master first.

### 4. AxePool Mining Proxy

Rust-based SV1 proxy that sits between miners and upstream pools, intercepting
shares to mint ehash tokens via hashpool. This is how miners earn ecash.

**Status:** Under investigation — need to determine if standalone (VPS) or embedded.

### 5. ContextVM (MCP over Nostr)

The esp32-tollgate firmware already has a ContextVM server exposing 10 MCP tools
over Nostr kind 25910 events. When the GSM upstream provides internet, these tools
become publicly accessible — enabling remote monitoring of the car's battery, mining
status, TollGate sessions, and future sensors.

The contextvm-anker-solix daemon provides BLE monitoring of the Anker Solix power bank
(98.7% test coverage, OBD reader, telemetry logging, Nostr event publishing).

## Network Topology (Car)

```
[SilentLink SIM] → GSM shield → ESP32-S3 (GSM gateway)
                                   │
                              WiFi AP (TollGate-XXXX, open)
                                   │
                    ┌──────────────┼──────────────┐
                    │              │              │
               BitAxe #1      BitAxe #2     (future sensors)
               (miner)        (miner)
```

## Power Budget (Estimated)

| Component | Power Draw | Source |
|-----------|-----------|--------|
| Anker Solix C1000 | — | Solar (200W) |
| GSM ESP32 + shield | ~2-5W (idle), ~10W (TX) | Anker Solix USB/220V |
| BitAxe (NerdAxe) | ~15-25W each | Anker Solix 220V |
| ESP32-S3 (standalone) | ~0.5W | Anker Solix USB |

200W solar panel at ~5 peak sun hours = ~1000Wh/day
Anker Solix capacity = 1000Wh

Mining budget = excess after: battery charging + ESP32 + GSM shield

## Status

- [ ] CTG-1: GSM upstream validated (BLOCKED on hardware info)
- [ ] CTG-2: 4 branches merged to esp32-tollgate master
- [ ] CTG-3: tollgate_core extracted
- [ ] CTG-4: BitAxe TollGate client integrated
- [ ] CTG-5: AxePool wired to hashpool
- [ ] CTG-6: ContextVM sensors exposed publicly
- [ ] CTG-7: E2E margin test passed

## Kanban

Board: `car-tollgate`
