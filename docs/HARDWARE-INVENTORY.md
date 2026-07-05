# Car TollGate — Complete Hardware Inventory

> All items identified via vision analysis (PPQ gemini-3-flash-preview) + Google AI cross-reference.
> Updated 2026-07-06.

## Compute

| Ref | Component | Details | Qty | Car Role |
|-----|-----------|---------|-----|----------|
| COMP-1 | Raspberry Pi Zero 2 W | RP3A0-AU (BCM2710A1 quad-core A53, 512MB RAM), WiFi+BT 4.2/BLE, CSI camera, mini-HDMI, micro-USB PWR+OTG. Week 40 2023. | 1 | Car-local Hermes instance, BLE hub, sensor aggregation |
| COMP-2 | ESP32-S3 DevKit Board A | MAC 94:a9:90:2e:37:7c, 16MB flash, 8MB PSRAM | 1 | GSM TollGate gateway |
| COMP-3 | ESP32-S3 DevKit Board B | MAC fc:01:2c:c5:50:50 | 1 | Secondary / mining |
| COMP-4 | ESP32-S3 DevKit Board C | MAC 20:6e:f1:98:d7:08 | 1 | Display board |
| COMP-5 | BitAxe(s) | NerdAxe variants, ESP32-S3 + BM1397 ASIC | 1+ | Bitcoin mining |

## GSM Shields (ALL 2G — see concern below)

| Ref | Component | Chip | Details | Qty |
|-----|-----------|------|---------|-----|
| GSM-1 | SIM800L EVB | SIM800L | Breakout board "EUB", IMEI 86672840134831, U.FL antenna pigtail, NET/RING LEDs, FCC ID UVD-SIM800L. Pins: VCC/5V, GND, SIF_RXD, SIF_TXD, RST. SIM card holder. | 1 |
| GSM-2 | SIM800 module (ROHS) | SIM800 | Blue PCB, ROHS marked, pins: RST, GND, SIF_RXD, SIF_TXD, VCC, 5V. SIM card holder. Antenna wire soldered. | 1 |
| GSM-3 | SIM800C Dual-Relay | SIM800C | 2x SONGLE SRD-05VDC-SL-C relays (10A 250VAC). STM32 controller. UART PA9/PA10. Has IMEI. Net LED. | 1 |
| GSM-4 | GSM Relay (wide voltage) | SIM800C variant | Single relay, 6V-36V wide input, TMS/TCLK programming pins, chip on underside. | 1 |

**CRITICAL: All 4 GSM shields are 2G-only (SIM800 series). 2G networks are being
phased out globally. Germany's 2G shutdown is scheduled/active. Must verify
network availability in the car's operating area before relying on these.**
**SilentLink eSIM compatibility must also be verified for 2G-only devices.**

## GPS

| Ref | Component | Chip | Details | Qty |
|-----|-----------|------|---------|-----|
| GPS-1 | GY-GPS6MV2 | u-blox NEO-6M-0-001 | Blue PCB, UART interface (VCC/RX/TX/GND), active ceramic patch antenna (IPEX/U.FL), battery backup for cold-start retention. V122 PCB revision. | 1 |

**NEO-6M is well-supported by ESP-IDF (NMEA parsing, u-blox UBX protocol).
Standard UART at 9600 baud. Airborne mode available (dynamic model 6).
Geohash encoding already in esp32-tollgate firmware (geohash.c).**

## IMU / Sensor Modules

| Ref | Component | Chip(s) | Interface | Details | Qty |
|-----|-----------|---------|-----------|---------|-----|
| IMU-1 | MPU-9250/6500 board | MPU-9250 (or MPU-6500) | I2C (SDA/SCL), SPI | 9-DoF: 3-axis accel + 3-axis gyro + 3-axis magnetometer. BEST for vibration FFT analysis. Pins: VCC, GND, SCL, SDA, ADO, ECL, EDA, FSYNC, NCS, INT. I2C addr: 0x68 (ADO=LOW) or 0x69 (ADO=HIGH). | 1 |
| IMU-2 | ADXL345 board | ADXL345 | I2C (SDA/SCL), SPI | 3-axis accelerometer, ±16g range. Pins: VCC, GND, SCL, SDA, SDO, CS, INT1, INT2. I2C addr: 0x53 (SDO=LOW) or 0x1D (SDO=HIGH). Good for tap/double-tap detection. | 1 |
| IMU-3 | GY-291 board | ADXL345 variant | I2C | Board variant of ADXL345. Same chip, different PCB layout. | 1 |
| IMU-4 | HW-246 GY-271 | HMC5883L or QMC5883L | I2C (SDA/SCL) | 3-axis magnetometer/compass. Pins: VCC, GND, SCL, SDA. I2C addr: 0x1E (HMC5883L) or 0x0D (QMC5883L). | 1 |

**RECOMMENDATION for vibration analysis (CTG-9): Use the MPU-9250.
It has the best specs for FFT wheel imbalance detection:
- Accelerometer: ±2g/±4g/±8g/±16g selectable ranges
- Gyroscope: ±250/±500/±1000/±2000 °/sec
- High sample rates (up to 32kHz for accel, 8kHz for gyro)
- Built-in DMP (Digital Motion Processor) for orientation tracking
The ADXL345 is a good secondary/backup.**

## Other Modules

| Ref | Component | Details | Qty | Potential Use |
|-----|-----------|---------|-----|---------------|
| MISC-1 | Keyes 140C07 MOS Module (IRF520) | MOSFET switching module. Orange PCB. SIG/VCC/GND input, U+/U- output (or VIN/GND), screw terminals. 1201 resistors. | 2 | Switching loads from ESP32 (lights, motors, relay control) |
| MISC-2 | IR proximity sensor module | Black PCB, IR LEDs + photodetector. 3 pins (likely VCC/GND/OUT). | 1 | Obstacle detection, proximity sensing |
| MISC-3 | Unidentified PCB ("QC") | Small black PCB, 3 pins, "QC" marking. Possibly Hall effect sensor (KY-035 analog) or other 3-pin module. | 1 | TBD |

## Connectivity

| Component | Status | Details |
|-----------|--------|---------|
| SilentLink eSIM | Confirmed | Non-KYC, Lightning top-up, convertible to physical SIM. **Must verify 2G compatibility.** |

---

## Summary

| Category | Items | Status |
|----------|-------|--------|
| Compute | 5 (Pi Zero 2 W, 3x ESP32-S3, BitAxe) | All identified |
| GSM | 4 (all SIM800 2G) | All identified — **2G concern** |
| GPS | 1 (NEO-6M) | Identified |
| IMU/Sensors | 4 (MPU-9250, 2x ADXL345, GY-271) | All identified |
| Other | 3 (2x MOSFET, IR sensor, unknown) | Mostly identified |
| **Total** | **17 distinct items** | |

## Open Questions

1. **2G NETWORK AVAILABILITY**: Are 2G networks still active in the car's
   operating area? If not, we need 4G shields (SIM7600/A7670).
2. **SilentLink + 2G**: Does SilentLink eSIM work on 2G-only devices?
3. **GPS antenna placement**: Roof near solar panel for best sky view?
4. **IMU placement**: Where on the car for best vibration data?
   (Near each wheel hub? On the chassis frame?)
5. **How many IMU modules needed?** One per wheel (4 total) or one central?
6. **MISC-3 identification**: Is the "QC" board a Hall sensor or something else?
