# Car TollGate — Hardware Inventory

> Updated 2026-07-06 — identified from photos via GLM-4.6V vision model.

## Identified Hardware

### Photo 1: Raspberry Pi (RP3A0-AU)

| Field | Value |
|-------|-------|
| **Type** | Single-board computer (Raspberry Pi) |
| **Chip** | RP3A0-AU (Broadcom BCM2837B0 — Pi 3 series) |
| **Serial** | 2399158 |
| **Features** | GPIO header, 2x micro-USB, 1x USB-A, micro-SD, DSI display |
| **Role** | Car-local Hermes instance (CTG-10) |
| **Question for user** | Is this a Pi Zero or Pi 3A+/3B+? Need to confirm WiFi/BLE capability |

### Photo 2: GSM Shield WITHOUT relays (chip marking unclear)

| Field | Value |
|-------|-------|
| **Type** | GSM/GPRS module/shield |
| **Board color** | Blue PCB |
| **Key features** | SIM card slot, wire antenna, interface pins |
| **Pin labels** | GND, B0, TMS, TCLK, 3V3 |
| **Chip marking** | A677A or similar (unclear — could be A7670S?) |
| **Role** | Data-only upstream for car TollGate (CTG-1) |
| **Question for user** | What's the exact chip model? Is it SIM800, A7670, SIM7600? |

### Photo 3: GSM Relay Shield (SIM800C)

| Field | Value |
|-------|-------|
| **Type** | GSM Relay Shield |
| **Board color** | Blue PCB |
| **GSM chip** | **SIM800C** (2G only!) |
| **Relays** | 2x SONGLE SRD-05VDC-SL-C (10A 250VAC) |
| **Pin labels** | PA8, PA10, NET, PWR, GND, VCC |
| **Antenna** | Green wire antenna |
| **Certification** | CE 0678 |
| **Role** | SMS-controlled relay switching + backup GSM |
| **WARNING** | SIM800C is 2G only. 2G is being phased out in many countries. |

## Still Needed From User

1. **Pi model confirmation** — Pi Zero W? Pi Zero 2 W? Pi 3A+?
2. **Photo 2 chip model** — the exact text on the main black chip
3. **How many of each board** do you have?
4. **GPS modules** — no GPS visible in these photos. Do you have separate GPS modules?
5. **IMU modules** — no IMU visible in these photos. Do you have separate IMU modules?

## Concerns

- **SIM800C is 2G only** — this is being phased out across Europe. Germany shutdown timeline: 2025-2027. For the car TollGate, the GSM shield WITHOUT relays (Photo 2) should be the primary upstream IF it supports 3G/4G.
- **Photo 2 chip (A677A?)** — if this is an A7670S, it supports 4G LTE Cat-1 which is ideal. Need confirmation.
