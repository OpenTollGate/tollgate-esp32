# Car TollGate — Hardware Inventory

> Updated as hardware is identified from photos and user input.
> Recommendations marked with **[RECOMMENDED]** or **[NOT RECOMMENDED]** with reasoning.

## Power

| Component | Status | Details |
|-----------|--------|---------|
| 200W Solar Panel | Confirmed | Roof-mounted, charges Anker Solix |
| Anker Solix C1000 | Confirmed | 1000Wh, BLE monitoring (contextvm-anker-solix daemon), 220V AC output |

## Compute

| Component | Status | Details |
|-----------|--------|---------|
| ESP32-S3 Board A | Confirmed | MAC 94:a9:90:2e:37:7c, primary test target |
| ESP32-S3 Board B | Confirmed | MAC fc:01:2c:c5:50:50, secondary |
| ESP32-S3 Board C | Confirmed | MAC 20:6e:f1:98:d7:08, display board |
| Raspberry Pi Zero/Nano | AWAITING PHOTO | Car-local Hermes instance, BLE hub |
| BitAxe(s) | Confirmed | NerdAxe variants, ESP32-S3 + BM1397 ASIC |

## GSM Shields

| Component | Status | Details |
|-----------|--------|---------|
| GSM Shield #1 (with relays) | AWAITING PHOTO | May be useful if relays control power switching |
| GSM Shield #2 (without relays) | AWAITING PHOTO | Simpler, likely for data-only upstream |

## GPS

| Component | Status | Details |
|-----------|--------|---------|
| GPS module(s) | AWAITING PHOTO | For car location via ContextVM/MCP |

## IMU / Sensors

| Component | Status | Details |
|-----------|--------|---------|
| IMU module(s) | AWAITING PHOTO | Vibration recording for FFT wheel imbalance analysis |

## SIM / Connectivity

| Component | Status | Details |
|-----------|--------|---------|
| SilentLink eSIM | Confirmed | Non-KYC, Lightning top-up, convertible to physical SIM |

---

## Pending Questions

1. Which GSM shield connects to which ESP32 board?
2. What SIM format do the GSM shields take (standard/micro/nano)?
3. Do the GSM shields use UART or USB to communicate with ESP32?
4. What's the Raspberry Pi model exactly (Zero W, Zero 2 W, Pi Nano)?
5. How many IMU modules and where will they be placed on the car?
6. Where will the GPS module be mounted (roof near solar panel for best signal)?
