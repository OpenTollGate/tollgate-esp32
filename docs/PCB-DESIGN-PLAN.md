# Car TollGate — Carrier PCB Design Plan

## Toolchain (Proven from Balloon Project)

| Tool | Version | Role |
|------|---------|------|
| SKiDL | 2.2.3 | Python → KiCad netlist generation |
| KiCad | CLI + pcbnew | Schematic capture, PCB layout, gerber export |
| JLCPCB | Fabrication | 2-layer PCB, minimum 0.4mm trace/clearance |

### Workflow (from balloon tracker)

1. Write Python schematic script (SKiDL) — defines parts, nets, connections
2. Generate netlist → import into KiCad
3. Layout PCB in KiCad (or pcbnew CLI for automated placement)
4. Export Gerbers + drill files
5. Upload to JLCPCB with BOM + pick-and-place file

Reference implementation: `esp32-balloon-integration/tracker/hardware/hub_board/hub_schematic.py`

## Board Overview

```
Car TollGate Carrier Board (~80x60mm, 2-layer FR4 1.6mm)

┌────────────────────────────────────────────────┐
│                   JLCPCB ORDERABLE              │
│                                                 │
│  ┌──────────┐  ┌─────────┐  ┌───────────────┐ │
│  │ ESP32-S3 │  │  GSM     │  │  GPS Module   │ │
│  │ Module   │  │  Shield  │  │  (NEO-6M or   │ │
│  │ (castellated)│ (SIM7600)│  │   similar)    │ │
│  └────┬─────┘  └────┬────┘  └───────┬───────┘ │
│       │              │               │          │
│       │ UART2        │ UART1         │ UART2    │
│       └──────────────┴───────────────┘          │
│                                                 │
│  ┌──────────┐  ┌─────────┐  ┌───────────────┐ │
│  │  IMU     │  │ Power   │  │  Status LEDs  │ │
│  │ ICM-42688│  │ 5V USB-C│  │  (x4: power,  │ │
│  │ or MPU6050│ │ or 12V  │  │   gsm, gps,   │ │
│  │ (I2C)    │  │ barrel  │  │   mining)     │ │
│  └──────────┘  └─────────┘  └───────────────┘ │
│                                                 │
│  ┌─────────────────────────────────────────┐   │
│  │ Pin Headers (2.54mm) for debug/expand   │   │
│  └─────────────────────────────────────────┘   │
│                                                 │
└────────────────────────────────────────────────┘
```

## Design Principles

1. **Modular population** — each subsystem (GSM, GPS, IMU) has its own section
   with 0-ohm jumpers or solder bridges to enable/disable. If a component isn't
   ready, leave it unpopulated.

2. **JLCPCB-friendly specs:**
   - 2-layer, 1.6mm FR4
   - Minimum trace: 0.25mm (6mil) — JLCPCB standard
   - Minimum drill: 0.3mm
   - HASL surface finish (cheapest) or ENIG (for BGA/QFN)
   - Silkscreen both sides

3. **Power distribution:**
   - Input: USB-C 5V (from Anker Solix) AND/OR barrel jack (12V from car)
   - GSM power: separate 4V/2A LDO from main 3.3V (GSM draws 2A peak)
   - ESP32: 3.3V from existing onboard regulator
   - IMU/GPS: 3.3V shared with ESP32

4. **Signal integrity:**
   - GSM UART: separate from GPS UART (UART1 vs UART2)
   - GPS: away from GSM antenna (interference)
   - IMU: close to ESP32, short I2C traces
   - Decoupling: 100nF + 10µF near every IC

## Pin Assignments (ESP32-S3)

| GPIO | Function | Destination | Notes |
|------|----------|-------------|-------|
| GPIO 43 | UART0 TX | USB-C (debug) | Default console |
| GPIO 44 | UART0 RX | USB-C (debug) | Default console |
| GPIO 17 | UART1 TX | GSM shield RX | GSM AT commands |
| GPIO 18 | UART1 RX | GSM shield TX | GSM AT commands |
| GPIO 4 | GSM PWRKEY | GSM shield PWR | Power control |
| GPIO 15 | UART2 TX | GPS module RX | NMEA |
| GPIO 16 | UART2 RX | GPS module TX | NMEA |
| GPIO 8 | I2C SDA | IMU SDA | Shared I2C bus |
| GPIO 9 | I2C SCL | IMU SCL | Shared I2C bus |
| GPIO 2 | Status LED 1 | Power LED | Active high |
| GPIO 3 | Status LED 2 | GSM LED | Active high |
| GPIO 5 | Status LED 3 | GPS lock LED | Active high |
| GPIO 6 | Status LED 4 | Mining LED | Active high |
| GPIO 19 | SPI MISO | (reserved for display) | |
| GPIO 20 | SPI MOSI | (reserved for display) | |
| GPIO 21 | SPI SCK | (reserved for display) | |

## SKiDL Schematic Structure

```python
# car_tollgate_schematic.py
from skidl import *

# Subcircuits for each subsystem:
# - esp32_s3_module()    → ESP32-S3-WROOM-1 (castellated module)
# - gsm_shield_conn()    → GSM shield connector (UART + power)
# - gps_module()         → GPS module footprint (UART)
# - imu_sensor()         → ICM-42688-P or MPU6050 (I2C)
# - power_input()        → USB-C + barrel jack + LDOs
# - status_leds()        → 4x LED with current-limit resistors
# - debug_header()       → 2.54mm pin header for expansion
# - decoupling_caps()    → 100nF near every IC

# Output: car_tollgate.net → import into KiCad for layout
```

## BOM (Preliminary — Updated After Hardware Identification)

| Component | Part | Qty | Source | Notes |
|-----------|------|-----|--------|-------|
| MCU | ESP32-S3-WROOM-1 | 1 | LCSC | Castellated module |
| GSM | (awaiting shield model) | 1 | — | Need user's shield model |
| GPS | (awaiting module photos) | 1 | — | Need user's GPS model |
| IMU | (awaiting module photos) | 1 | — | Need user's IMU model |
| USB-C | USB4175 | 1 | LCSC | Power input + debug |
| LDO 3.3V | AMS1117-3.3 | 1 | LCSC | For ESP32 + sensors |
| LDO 4V | (TBD based on GSM) | 1 | LCSC | For GSM shield (2A peak) |
| LEDs | 0805 SMD | 4 | LCSC | Status indicators |
| Resistors | 0402/0603 | ~10 | LCSC | LED current limit + pulls |
| Caps | 0402/0603 100nF | ~8 | LCSC | Decoupling |
| Caps | 0805 10µF | 3 | LCSC | Bulk decoupling |
| Pin header | 2.54mm 1x20 | 2 | LCSC | Debug expansion |

## JLCPCB Ordering Parameters

| Parameter | Value |
|-----------|-------|
| Base material | FR4-Standard TG 135-140 |
| Layers | 2 |
| Dimension | ~80 x 60 mm (TBD) |
| Thickness | 1.6mm |
| Surface finish | HASL (lead-free) |
| Copper weight | 1oz |
| Min track/spacing | 6/6mil (0.15/0.15mm) |
| Min hole size | 0.3mm |
| Silkscreen | Both sides |
| Quantity | 5 (minimum JLCPCB order) |
| Cost estimate | ~$2-5 for 5 boards |

## Status

- [ ] Identify all hardware from photos (BLOCKING)
- [ ] Write SKiDL schematic script
- [ ] Generate KiCad netlist
- [ ] PCB layout in KiCad
- [ ] Export Gerbers + drill files
- [ ] Generate BOM + pick-and-place
- [ ] Order from JLCPCB
