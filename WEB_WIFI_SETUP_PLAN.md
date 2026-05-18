# Web WiFi Setup Plan

## Overview

Move WiFi configuration from on-display touchscreen UI to a web-based setup page
served by the captive portal. The display becomes portrait-only, showing QR codes
and status info. No more landscape rotation, on-screen keyboard, or touch-driven
WiFi setup.

## Architecture

### Display (portrait 320x480 only)

| State | When | Content |
|-------|------|---------|
| BOOT | Startup | "TollGate" title + "starting..." |
| READY (unconfigured) | No STA network | AP WiFi QR + SSID + "http://AP_IP/setup" |
| READY (configured) | STA connected | QR cycling (WiFi↔Portal) + balance/clients/price |
| PAYMENT_RECEIVED | After payment | "ACCESS GRANTED" + amount + time (3s then→READY) |
| ERROR | No upstream | "NO UPSTREAM" + "http://AP_IP/setup" |

### Captive Portal (new endpoints)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/setup` | GET | WiFi setup HTML page (only when unconfigured) |
| `/wifi/scan` | GET | Trigger scan, return `[{ssid,rssi,secured}]` JSON |
| `/wifi/connect` | POST | Take `{ssid,password}`, save config, connect |
| `/wifi/status` | GET | Return `{connected,ip,ssid}` |

### Files Removed from Build (kept on disk)

- `main/touch.c` / `touch.h`
- `main/keyboard.c` / `keyboard.h`
- `main/wifi_setup.c` / `wifi_setup.h`

### Files Modified

- `main/display.c` — Strip WiFi setup state, rotation, offscreen; add setup URL text
- `main/display.h` — Remove `DISPLAY_WIFI_SETUP`, `display_enter_wifi_setup()`
- `main/tollgate_main.c` — Remove WiFi setup auto-enter, add display state for unconfigured
- `main/captive_portal.c` — Add WiFi scan/connect/status endpoints + `/setup` HTML
- `main/captive_portal.h` — Expose captive_portal_is_setup_available()
- `components/axs15231b/axs15231b.c` — Remove offscreen buffer
- `components/axs15231b/include/axs15231b.h` — Remove `axs15231b_set_offscreen()`
- `main/CMakeLists.txt` — Remove touch/keyboard/wifi_setup sources

## Checklist

### Phase 1: Strip display and driver
- [x] Remove offscreen buffer from `axs15231b.c` and `axs15231b.h`
- [x] Strip `display.c` — remove WiFi setup state, rotation, keyboard/touch imports
- [x] Update `display.h` — remove `DISPLAY_WIFI_SETUP`, `display_enter_wifi_setup()`
- [x] Add setup URL text to READY (unconfigured) and ERROR screens
- [x] Remove WiFi setup auto-enter from `tollgate_main.c`

### Phase 2: Add web WiFi setup
- [x] Add `/wifi/scan` endpoint to `captive_portal.c`
- [x] Add `/wifi/connect` endpoint to `captive_portal.c`
- [x] Add `/wifi/status` endpoint to `captive_portal.c`
- [x] Add `/setup` HTML page with scan list + connect form
- [x] Gate `/setup` behind `network_count == 0`

### Phase 3: Build configuration
- [x] Remove touch.c, keyboard.c, wifi_setup.c from `main/CMakeLists.txt`

### Phase 4: Testing
- [x] `make test-unit` passes
- [x] Build succeeds (`idf.py build`)
- [x] Flash to Board C, verify portrait display shows setup URL
- [ ] Test `/setup` page from phone browser
- [x] Write integration test `tests/integration/wifi_setup.mjs`
