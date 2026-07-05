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
| Raspberry Pi Zero 2 W | IDENTIFIED | RP3A0-AU (BCM2710A1 quad-core Cortex-A53, 512MB RAM), WiFi+BT. Week 40 2023 batch. For car-local Hermes instance. |
| BitAxe(s) | Confirmed | NerdAxe variants, ESP32-S3 + BM1397 ASIC |

## GSM Shields

| Component | Status | Details |
|-----------|--------|---------|
| GSM Shield #1 (with relays, SIM800C) | IDENTIFIED | SIM800C 2G modem, 2x SONGLE SRD-05VDC-SL-C relays (10A 250VAC). STM32 controller. UART pins PA9/PA10. Has IMEI. For relay control via SMS. 2G ONLY — being phased out. |
| GSM Shield #2 (with relay, wide voltage) | IDENTIFIED | Single-channel GSM relay, 6V-36V wide input, SIM holder, TMS/TCLK programming pins. Chip on underside. Also 2G. |

## GPS

| Component | Status | Details |
|-----------|--------|---------|
| GPS module(s) | NOT YET PHOTOGRAPHED | User mentioned GPS modules exist — photos pending |

## IMU / Sensors

| Component | Status | Details |
|-----------|--------|---------|
| IMU module(s) | NOT YET PHOTOGRAPHED | User mentioned IMU modules exist — photos pending |

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
