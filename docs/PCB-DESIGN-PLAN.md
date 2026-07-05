# Car TollGate — PCB Design Plan

> Custom PCB integrating GSM shield, ESP32-S3, GPS, IMU on a single JLCPCB-orderable board.
> Uses the same toolchain proven in the balloon tracker project.

## Toolchain (Same as Balloon Project)

| Tool | Purpose | Version |
|------|---------|---------|
| **SKiDL** (Python) | Schematic generation via code — generates KiCad netlist | `pip install skidl` |
| **KiCad 9.0** | PCB layout + custom footprints | Native app |
| **Custom footprints** | JSON-defined component footprints | `footprints/*.json` |
| **JLCPCB** | Fabrication — accepts KiCad Gerber export | jlcpcb.com |
| **LCSC** | Component sourcing (JLCPCB's component store) | lcsc.com |

### Why SKiDL + KiCad

The balloon project proved this approach works:
1. SKiDL generates the netlist programmatically (Python) — version controlled, diffable
2. KiCad imports the netlist for PCB layout
3. Custom footprints defined as `.kicad_mod` files in `custom.pretty/`
4. Gerber export → JLCPCB ordering in minutes
5. BOM with LCSC part numbers for JLCPCB assembly service

## Board Requirements

### Car TollGate Hub Board V1

| Parameter | Value |
|-----------|-------|
| Board name | Car TollGate Hub V1 |
| Size | ~80 x 60 mm (larger than balloon — no weight constraint) |
| Layers | 2-layer (top + bottom copper) |
| Thickness | 1.6mm FR4 (standard JLCPCB) |
| Min trace width | 0.25mm (JLCPCB standard) |
| Min clearance | 0.25mm |
| Surface finish | HASL or ENIG (HASL cheaper, ENIG better for fine pitch) |
| Copper weight | 1oz (standard) — GSM power traces may need 2oz |

### Dual-Footprint Design (Same as Balloon)

Following the balloon project's proven pattern:
- **Dev config:** ESP32-S3 dev board via pin headers (USB-C, easy debug)
- **Flight/Car config:** Bare ESP32-S3 module soldered directly (compact, reliable)

### Components

| Ref | Component | Package | Qty | Purpose | LCSC Part (TBD) |
|-----|-----------|---------|-----|---------|-----------------|
| U1a | ESP32-S3 DevKit (header) | 2x10 pin 2.54mm | 1 | Dev mode MCU |
| U1b | ESP32-S3-WROOM-1 (bare) | M.2 module | 1 | Car mode MCU |
| U2 | GSM Shield connector | 2x8 pin 2.54mm or M.2 | 1 | GSM modem (SIM7600/A7670) |
| U3 | GPS module connector | 1x6 pin 2.54mm or U.FL | 1 | GPS (NEO-M9N, ATGM336H, etc.) |
| U4 | IMU module connector | 1x8 pin 2.54mm | 1 | IMU (ICM-42688-P, MPU6050) |
| U5 | USB-UART bridge | CP2102N | 1 | Flash programming (bare MCU mode) |
| U6 | Power path controller | LM2596 or MP1584 | 1 | 5V→4V buck for GSM (2A peak) |
| U7 | LDO 3.3V | AMS1117-3.3 or ME6211 | 1 | 3.3V for ESP32 + sensors |
| D1 | Schottky diode | SS34 (SMA) | 1 | Solar input reverse protection |
| D2 | Power LED | 0805 LED | 1 | Status indicator |
| D3 | LED green | 0805 | 1 | TollGate active |
| D4 | LED red | 0805 | 1 | Error/GSM fail |
| C1-C8 | 100nF decoupling | 0402 or 0603 | 8 | One per IC |
| C9 | 10uF bulk | 0805 | 1 | ESP32 power |
| C10 | 100uF electrolytic | SMD | 1 | GSM TX burst |
| C11 | 4.7uF | 0805 | 1 | GPS VCC |
| C12 | 2.2uF | 0805 | 1 | IMU VCC |
| R1,R2 | 10k pullup | 0402 | 2 | I2C bus (GPS+IMU) |
| R3 | 470 ohm | 0402 | 1 | Power LED resistor |
| R4 | 470 ohm | 0402 | 1 | Status LED resistor |
| J1 | USB-C receptacle | USB4105 | 1 | Programming + power |
| J2 | SIM card holder | C707 | 1 | SIM7600 nano-SIM |
| J3 | SMA/U.FL antenna | U.FL | 1 | GSM antenna |
| J4 | SMA/U.FL antenna | U.FL | 1 | GPS antenna |
| J5 | I2C expansion | 1x4 JST-PH | 1 | Future sensors |
| J6 | Power input | XT30 or barrel jack | 1 | Anker Solix 5V/220V |
| SB1 | Solder bridge (dev/car) | 2 pads | 1 | MCU mode select |
| F1 | Polyfuse 3A | 1206 | 1 | GSM overcurrent protection |

### Pin Mapping (ESP32-S3)

| ESP32 GPIO | Function | Destination | Notes |
|------------|----------|-------------|-------|
| GPIO43 | UART0 TX | CP2102N RX | Console/debug |
| GPIO44 | UART0 RX | CP2102N TX | Console/debug |
| GPIO4 | UART1 TX | GSM RX | GSM modem AT commands |
| GPIO5 | UART1 RX | GSM TX | GSM modem responses |
| GPIO6 | GSM PWRKEY | GSM PWR pin | Power on/off modem |
| GPIO7 | GSM STATUS | GSM status pin | Modem online indicator |
| GPIO8 | GPS UART TX | GPS RXD | NMEA commands |
| GPIO9 | GPS UART RX | GPS TXD | NMEA data |
| GPIO1 | I2C SDA | IMU + expansion | Shared I2C bus |
| GPIO2 | I2C SCL | IMU + expansion | Shared I2C bus |
| GPIO10 | IMU INT1 | IMU interrupt | Data-ready / tap detection |
| GPIO11 | IMU INT2 | IMU interrupt | Free-fall / motion |
| GPIO0 | BOOT | Button + pullup | Programming mode |
| GPIO46 | LED TollGate | Green LED | TollGate active |
| GPIO47 | LED Error | Red LED | Error state |

**IMPORTANT:** ESP32-S3 strapping pins (GPIO0, GPIO45, GPIO46) must not have
external pullups/pulldowns that interfere with boot mode selection.

## Power Architecture

```
Anker Solix 5V USB (or 220V→5V adapter)
    │
    ├── F1 (3A polyfuse) → J6 power input
    │
    ├── U6 (buck: 5V → 4.0V, 2A) → GSM shield VCC
    │   └── C10 (100uF) for GSM TX burst current
    │
    └── U7 (LDO: 5V → 3.3V, 800mA) → ESP32 + sensors
        ├── GPS VCC
        ├── IMU VCC
        └── Expansion VCC

Solar input (optional, through D1):
    Solar → D1 (reverse protection) → J6
    Used when Anker Solix USB is unavailable
```

**GSM power is CRITICAL:** The modem draws 2A peak during transmission.
A dedicated buck converter (U6) is REQUIRED — USB/LDO cannot supply this.
C10 (100uF) handles burst current; the buck handles sustained draw.

## Board Layout Zones

```
┌─────────────────────────────────────────────┐
│  [USB-C]  [Power LED]                       │
│              │                               │
│  ┌──────────┴──────────┐                    │
│  │    ESP32-S3          │   ┌──────────┐    │
│  │   (dev header or     │   │  GPS     │    │
│  │    bare module)      │   │ connector│    │
│  │                      │   │  + U.FL  │    │
│  │   I2C: SDA/SCL       │   └──────────┘    │
│  └──────────┬──────────┘                    │
│             │                  ┌──────────┐ │
│  ┌──────────┴──────────┐      │   IMU    │ │
│  │   GSM Shield         │     │ connector│ │
│  │   connector          │     │  + INT   │ │
│  │   (2x8 header)       │     └──────────┘ │
│  │                      │                  │
│  │   SIM holder (J2)   │   ┌──────────┐   │
│  │   U.FL antenna (J3) │   │ Buck 4V  │   │
│  └─────────────────────┘   │ (GSM pwr)│   │
│                            └──────────┘   │
│  [I2C expansion J5]  [Power input J6]     │
│  [Status LEDs: green/red]                  │
└─────────────────────────────────────────────┘
```

## Solder Bridges (Same Pattern as Balloon)

| Bridge | Function | Default |
|--------|----------|---------|
| SB1 | MCU mode: dev header vs bare module | Dev (open) |
| SB2 | GPS UART vs I2C (some GPS modules use I2C) | UART (open) |
| SB3 | Power source: Anker USB vs solar | USB (closed) |

## JLCPCB Ordering

### Gerber Export

```bash
# From KiCad PCB editor:
# File → Plot → Format: Gerber → Layers: F.Cu, B.Cu, F.Mask, B.Mask, F.Silk, B.Silk, Edge.Cuts
# File → Generate Drill File → PTH + NPTH → Excellon format
```

### JLCPCB Settings

| Setting | Value |
|---------|-------|
| Base material | FR4 |
| Layers | 2 |
| Dimensions | ~80 x 60 mm |
| PCB thickness | 1.6mm |
| PCB color | Any (green cheapest, blue looks good) |
| Surface finish | HASL (lead-free) — cheapest, fine for 0.25mm pitch |
| Copper weight | 1oz (if GSM traces need more: 2oz +$) |
| Min track/spacing | 6/6mil (standard, no extra cost) |
| Quantity | 5 (JLCPCB minimum) |
| Estimated cost | ~$5-10 for 5 boards + shipping |

### Assembly Service (Optional)

JLCPCB offers SMT assembly for ~$7 setup + $0.50-2.00 per component.
If using assembly: all parts must have LCSC part numbers in the BOM.
Recommend: order bare boards first, hand-solder dev version, then order
assembled boards for production.

## File Structure (Mirroring Balloon Project)

```
esp32-tollgate/hardware/
├── car_hub_board/
│   ├── car_hub_schematic.py     ← SKiDL schematic generator
│   ├── car_hub_board.kicad_pcb  ← KiCad PCB layout
│   ├── car_hub_board.kicad_sch  ← KiCad schematic
│   ├── car_hub_board.kicad_pro  ← KiCad project
│   ├── custom.pretty/           ← Custom footprints
│   │   ├── GSM_Shield_Header.kicad_mod
│   │   ├── ESP32_S3_DevKit_Header.kicad_mod
│   │   └── SIM_Card_Holder.kicad_mod
│   ├── plan.md                  ← This file
│   └── implementation-plan.md   ← Step-by-step build guide
├── footprints/
│   ├── esp32-s3-devkit.json     ← Pin/footprint data
│   ├── gsm-shield.json          ← GSM connector pinout
│   ├── gps-module.json          ← GPS connector pinout
│   └── imu-module.json          ← IMU connector pinout
├── assembly/
│   └── generate_gerbers.py      ← Gerber export script
└── bom/
    └── BOM.md                   ← Bill of materials with LCSC parts
```

## Build Phases

### Phase 1: Breadboard/Protoboard (NOW)
Wire ESP32-S3 dev board + GSM shield breakout + GPS + IMU on breadboard.
Validate firmware works with all peripherals before designing PCB.

### Phase 2: PCB V1 (After hardware confirmed)
- Write SKiDL schematic (`car_hub_schematic.py`)
- Layout in KiCad
- Order from JLCPCB (5 boards, ~$10)
- Hand-solder dev version
- Test in car

### Phase 3: PCB V2 (Production)
- Fix any V1 issues
- Add JLCPCB assembly service
- Deploy in car permanently

## Status

- [ ] Hardware model numbers confirmed (BLOCKED on photos)
- [ ] SKiDL schematic written
- [ ] KiCad PCB laid out
- [ ] JLCPCB order placed
- [ ] Board received + assembled
- [ ] Tested in car
