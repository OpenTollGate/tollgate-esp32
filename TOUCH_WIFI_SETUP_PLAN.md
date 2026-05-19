# Touchscreen WiFi Setup Plan

## Overview
Add touchscreen WiFi configuration to the TollGate display so users can select a gateway network and enter a password directly on the device.

## Hardware

### Touch Controller
- **IC:** AXS15231B built-in touch (same chip as display, separate I2C interface)
- **Bus:** I2C, address `0x3B`
- **Pins:** SDA=GPIO4, SCL=GPIO8, RST=GPIO12, INT=GPIO11
- **Protocol:** Write 11 bytes `[0xb5,0xab,0xa5,0x5a,0x00,0x00,0x00,0x08,0x00,0x00,0x00]`, read 8 bytes
- **Coordinates:** X/Y from data bytes [2..5], 12-bit
- **RST sequence:** LOW 200ms -> HIGH 200ms

### No Pin Conflicts
| Pin | Display | Touch | Conflict? |
|-----|---------|-------|-----------|
| 4   | --      | SDA   | No |
| 8   | --      | SCL   | No |
| 11  | --      | INT   | No |
| 12  | --      | RST   | No |
| 1,21,39,40,45,47,48 | Display QSPI | -- | No |

## Trigger Points (all three)

| Trigger | How | When |
|---------|-----|------|
| A) Tap on ERROR screen | "Setup WiFi" button | Any time upstream is down |
| B) Auto-show on first boot | Check wifi_networks empty | Fresh device with no credentials |
| C) Setup button on READY/ERROR | Small gear icon in corner | Always accessible |

## UI Screens

### 1. Scanning
Title + "Scanning..." spinner

### 2. Network List
Top 8 by RSSI, sorted strongest first, scrollable. Lock icon for secured networks.

### 3. Password Entry
SSID name, masked password field with eye reveal toggle, QWERTY keyboard.

### 4. Connecting
"Connecting to SSID..." spinner

### 5. Result
Green "Connected!" with IP, or red "Failed" with retry options.

## New Files

| File | Purpose | Testable Logic |
|------|---------|----------------|
| `main/touch.h/c` | AXS15231B I2C touch driver | Coordinate parsing |
| `main/keyboard.h/c` | On-screen keyboard rendering + hit detection | Layout, key lookup |
| `main/wifi_setup.h/c` | WiFi scan/select/connect flow | State machine |
| `tests/unit/test_touch.c` | Touch coordinate decode | Pure math |
| `tests/unit/test_keyboard.c` | Key layout + hit detection | Pure logic |
| `tests/unit/test_wifi_setup.c` | Setup state machine | State transitions |

## Config Extension
Add `tollgate_config_add_wifi(const char *ssid, const char *password)` to `config.c` - rewrites `/spiffs/config.json`.

## Display Extension
Add `DISPLAY_WIFI_SETUP` to `display_state_t`.

## Implementation Checklist

### Phase 1: Touch Driver
- [x] Create `main/touch.h` with API
- [x] Create `main/touch.c` with ESP-IDF I2C v5 implementation
- [x] Create `tests/unit/test_touch.c` with coordinate parsing tests
- [x] Run `make test-unit`, verify all pass

### Phase 2: On-Screen Keyboard
- [x] Create `main/keyboard.h` with API
- [x] Create `main/keyboard.c` with QWERTY layout + hit detection
- [x] Create `tests/unit/test_keyboard.c` with layout + key lookup tests
- [x] Run `make test-unit`, verify all pass

### Phase 3: WiFi Setup Flow
- [x] Create `main/wifi_setup.h` with API
- [x] Create `main/wifi_setup.c` with scan/list/connect state machine
- [x] Add `tollgate_config_add_wifi()` to config.c + config.h
- [x] Create `tests/unit/test_wifi_setup.c` with state machine tests
- [x] Run `make test-unit`, verify all pass

### Phase 4: Integration
- [x] Add `DISPLAY_WIFI_SETUP` to display.h
- [x] Update display.c with WiFi setup rendering + touch input
- [x] Update `main/CMakeLists.txt` with new source files
- [ ] Build, flash to Board C, verify full flow
