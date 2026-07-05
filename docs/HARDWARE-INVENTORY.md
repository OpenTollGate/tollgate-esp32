# Car TollGate — Complete Hardware Inventory

> Updated 2026-07-06 — all 13 photos analyzed via GLM-4.6V vision model.
> Cross-referenced with user's Google Search AI identifications.

## Compute

### 1. Raspberry Pi Zero 2 W
| Field | Value |
|-------|-------|
| Photos | img_09be2f3f5558.jpg, img_60c32bddf179.jpg |
| Chip | RP3A0-AU (Broadcom SiP — quad-core + RAM) |
| Model | Raspberry Pi Zero 2 W |
| Features | GPIO header, micro-USB, micro-SD |
| Role | Car-local Hermes instance (CTG-10) — WiFi/BLE for device control |
| WiFi/BLE | YES — Zero 2 W has built-in 2.4GHz WiFi + Bluetooth 4.2 |

## GSM / Cellular

### 2. SIM800L EVB (GSM Shield WITHOUT relays)
| Field | Value |
|-------|-------|
| Photos | img_1525feac45d0.jpg, img_25c5858a7169.jpg (front), img_8893ecf68086.jpg (back) |
| Chip | SIM800L (quad-band GSM/GPRS, 2G ONLY) |
| Board | SIM800L EVB (evaluation board variant) |
| IMEI | 86672840134831 |
| FCC ID | UVD-SIM800L |
| Pins | VCC, GND, RST, TX, RX |
| SIM | Micro-SIM card slot |
| Antenna | SMA connector with pigtail to external whip antenna |
| LEDs | NET (network status), RING (incoming call) |
| Network | 2G GSM/GPRS ONLY — being phased out in EU |
| Role | Data-only upstream (CTG-1) — but 2G is a concern |

### 3. SIM800C Dual-Relay GSM Shield
| Field | Value |
|-------|-------|
| Photos | img_dd9d5a22475e.jpg (front), img_f68bfbfa3d01.jpg (context) |
| Chip | SIM800C (2G GSM/GPRS — successor to SIM800L, more power efficient) |
| Relays | 2x SONGLE SRD-05VDC-SL-C (10A 250VAC, 5V DC coil) |
| Pins | PA8, PA10, NET, PWR, GND, VCC |
| SIM | SIM card slot |
| Antenna | Green wire antenna |
| Network | 2G GSM/GPRS ONLY |
| Role | SMS-controlled relay switching + backup GSM |

### 4. SIM Card Interface Module (micro-SIM breakout)
| Field | Value |
|-------|-------|
| Photo | img_8893ecf68086.jpg |
| Pins | VCC, 3V, RST, GND, SIM_RXD, SIM_TXD |
| Role | SIM card breakout for direct microcontroller connection |

## GPS

### 5. GY-GPS6MV2 (u-blox NEO-6M GPS)
| Field | Value |
|-------|-------|
| Photo | img_5e249c8d02f3.jpg |
| Chip | u-blox NEO-6M-0-001 |
| Board | GY-GPS6MV2 (revision V122) |
| Pins | VCC, RX, TX, GND |
| Antenna | Active ceramic patch antenna via IPEX/U.FL cable |
| Interface | UART (9600 baud default, NMEA sentences) |
| Role | Car location via ContextVM/MCP (CTG-8) |

## IMU / Sensors

### 6. MPU-9250/6500 (9-DoF IMU) — RECOMMENDED FOR VIBRATION ANALYSIS
| Field | Value |
|-------|-------|
| Photos | img_2dc5a67d07db.jpg, img_c7b0e63a1258.jpg |
| Chip | MPU-9250 (or MPU-6500 variant) |
| Capabilities | 3-axis accelerometer + 3-axis gyroscope + 3-axis magnetometer |
| Pins | VCC, GND, SCL, SDA, FSYNC, NCS, INT, ADO, ECL, EDA |
| Interface | I2C (addr 0x68 with ADO=GND, 0x69 with ADO=VCC) and SPI |
| Role | Vibration recording for FFT wheel imbalance analysis (CTG-9) |
| Rating | BEST option for vibration analysis — high ODR, good noise performance |

### 7. ADXL345 (3-axis accelerometer)
| Field | Value |
|-------|-------|
| Photos | img_2dc5a67d07db.jpg, img_c7b0e63a1258.jpg |
| Chip | ADXL345 (Analog Devices) |
| Pins | VCC, GND, SCL, SDA, SDO, INT1, INT2, CS |
| Interface | I2C (addr 0x53 with SDO=GND, 0x1D with SDO=VCC) and SPI |
| Range | ±2g/±4g/±8g/±16g, 13-bit resolution |
| Role | Secondary accelerometer / vibration backup |

### 8. GY-291 (ADXL345 variant — same chip, different board)
| Field | Value |
|-------|-------|
| Photos | img_2dc5a67d07db.jpg, img_c7b0e63a1258.jpg |
| Chip | ADXL345 (same chip as above, different breakout board label) |
| Note | Google AI says this is a BMP180 barometric pressure sensor. Vision model says same as ADXL345. Need user confirmation. |

### 9. GY-271 / HW-246 (3-axis magnetometer/compass)
| Field | Value |
|-------|-------|
| Photos | img_2dc5a67d07db.jpg, img_c7b0e63a1258.jpg |
| Chip | HMC5883L or QMC5883L (3-axis magnetometer) |
| Board | HW-246 GY-271 |
| Pins | VCC, GND, SCL, SDA, DRDY |
| Interface | I2C |
| Role | Compass heading — could complement GPS for navigation |

## Power / Motor Control

### 10. IRF520 MOSFET Driver Module (x2, front and back)
| Field | Value |
|-------|-------|
| Photos | img_f35940444b82.jpg (front), img_161192262bff.jpg (back) |
| Brand | Keyes |
| Model | 140C07 "MOS Module" |
| Chip | IRF520 MOSFET |
| Pins | SIG, VCC, GND (logic side); U+, U-, UIN, GND (power side) |
| Function | Switch high-current DC loads (motors, valves, LEDs) via PWM |
| Role | Solar panel power routing, mining rig power control, fan control |

### 11. KY-035 Analog Hall Effect Sensor (49E)
| Field | Value |
|-------|-------|
| Photo | img_13fda6b806a0.jpg (Google AI identified, vision sees "Keyes sensor") |
| Chip | 49E Linear Hall sensor |
| Brand | Keyes (KY-035) |
| Function | Detect magnetic field proximity |
| Role | Could detect wheel rotation speed (magnet on wheel + Hall sensor) |

## Summary — Hardware Count

| Category | Items | Count |
|----------|-------|-------|
| Compute | RPi Zero 2 W | 1 |
| GSM | SIM800L EVB, SIM800C relay, SIM breakout | 3 |
| GPS | GY-GPS6MV2 (NEO-6M) | 1 |
| IMU | MPU-9250, ADXL345, GY-291 | 3 |
| Compass | GY-271 (HMC5883L) | 1 |
| Power | IRF520 MOSFET x2 | 2 |
| Sensor | KY-035 Hall effect | 1 |
| **Total** | | **12 modules** |

## Recommendations

### For CTG-1 (GSM upstream):
USE: SIM800L EVB (has external SMA antenna — better signal)
CONCERN: Both GSM modules are 2G only. Germany 2G shutdown expected 2025-2027.
FUTURE: Need a 4G LTE module (SIM7600/A7670S) for long-term viability.

### For CTG-9 (Vibration/FFT analysis):
USE: MPU-9250 (best sensor in the inventory — 9-DoF, high data rate)
BACKUP: ADXL345 (dedicated accelerometer, simpler to interface)
APPROACH: Mount MPU-9250 near wheel hub, sample at 200Hz+, FFT on Z-axis

### For CTG-8 (GPS):
USE: GY-GPS6MV2 (NEO-6M) — proven module, easy UART interface
MOUNT: Near solar panel on roof for best sky visibility

### For power control (mining on/off by solar):
USE: IRF520 MOSFET modules — switch BitAxe power based on battery level
