# GSM Upstream Integration — Technical Reference

## Overview

Adding GSM/LTE mobile data upstream to esp32-tollgate using an ESP32-S3 + GSM shield
+ SilentLink non-KYC eSIM. This provides an alternative to WiFi STA for upstream
internet — critical for the car TollGate project where there's no WiFi upstream.

## SilentLink (silent.link)

- **Service:** Non-KYC eSIM provider
- **Top-up:** Bitcoin Lightning Network (via BTCPay)
- **Coverage:** Global roaming (uses multiple carrier networks)
- **eSIM → Physical SIM:** User has method to flash eSIM profiles onto physical SIMs
- **API:** Web-based ordering, QR code delivery, no API for programmatic access

## ESP-IDF GSM Support

### Component: `espressif/esp_modem` (IDF Component Manager)

The `esp_modem` component provides:
- AT command interface to GSM modems
- PPP (Point-to-Point Protocol) data mode
- Network registration monitoring
- Signal quality reporting
- DCE (Data Communication Equipment) abstraction

### Adding to esp32-tollgate

1. **Add to `main/idf_component.yml`:**
```yaml
dependencies:
  espressif/esp_modem: "^1.0.0"
```

2. **Enable PPP in `sdkconfig.defaults`:**
```
CONFIG_LWIP_PPP_SUPPORT=y
CONFIG_LWIP_PPP_NOTIFY_PHASE_SUPPORT=y
CONFIG_LWIP_PPP_PAP_SUPPORT=y
CONFIG_LWIP_PPP_CHAP_SUPPORT=y
```

Current sdkconfig shows: `CONFIG_LWIP_PPP_SUPPORT is not set` — MUST BE ENABLED.

3. **New source files:**
- `main/gsm_upstream.c` / `.h` — GSM modem init, PPP netif creation, upstream management
- Integration into `tollgate_main.c` — alternative upstream to WiFi STA

## GSM Shield Hardware (AWAITING USER CONFIRMATION)

### Common ESP32 GSM Shields

| Shield | Network | UART | Power | Notes |
|--------|---------|------|-------|-------|
| SIM800L | 2G only | 9600 baud | 3.7-4.2V, 2A peak | Being phased out in EU/US |
| SIM7600G-H | 4G LTE | 115200 baud | 3.4-4.4V, 2A peak | Most futureproof |
| A7670S | 4G LTE Cat-1 | 115200 baud | 3.4-4.4V, 2A peak | Newer, cheaper |
| LilyGO T-A7608E | 4G LTE (integrated) | USB/UART | Built-in | All-in-one ESP32+GSM |

### Typical ESP32-S3 Pin Mapping (UART connection)

```
ESP32-S3          GSM Shield
─────────         ──────────
GPIO 44 (TX)  ──> RX (shield receives)
GPIO 43 (RX)  <── TX (shield sends)
GPIO 4        ──> PWRKEY (power control)
GND           ──> GND
5V/USB        ──> VCC (or external 4V supply for high-current)
```

**CRITICAL:** GSM modems draw 2A peak during transmission. USB power is
insufficient — must use Anker Solix 220V or dedicated 4V/2A supply.

### AT Command Sequence (SIM7600/SIM800 — SIM initialization)

```
AT              → OK (modem responds)
AT+CPIN?        → +CPIN: READY (SIM unlocked)
AT+CSQ          → +CSQ: <rssi>,<ber> (signal quality)
AT+CREG?        → +CREG: 0,1 (registered, home network)
AT+CGDCONT=1,"IP","<APN>"  → OK (set APN — SilentLink uses "silent.link")
ATD*99***1#     → CONNECT (enter PPP/data mode)
```

SilentLink APN: `silent.link` (standard for their eSIM profiles)

## Integration Architecture

```
Current (WiFi STA upstream):
  Internet ← WiFi Router ← esp_wifi_sta ← services

New (GSM upstream, added alongside):
  Internet ← GSM/LTE ← esp_modem (PPP) ← services
                                    ↑
                              Automatic failover:
                              Try WiFi STA first, fallback to GSM
```

### Boot Sequence Change

```
[existing] → wifi_init_sta() + wifi_create_ap_netif()
[NEW]      → gsm_init() → gsm_connect() → ppp_netif created
            → If STA fails: switch default route to PPP netif
            → If STA succeeds: GSM as backup/failover
```

### Key Design Decisions (NEED USER INPUT)

1. **Dual upstream or GSM-only?** The car ESP32 could use GSM as primary
   (no WiFi upstream needed) or as failover alongside WiFi STA.

2. **Power management.** GSM TX draws 2A. Should we duty-cycle the modem
   when no clients are connected? Or keep it always on?

3. **Data metering.** SilentLink data plans have limits. The TollGate
   pricing already supports byte-based metering (`step_bytes` in config).
   Set price-per-byte to cover GSM data costs.

## Status

- [ ] User confirms GSM shield model
- [ ] User confirms SIM card format (standard/micro/nano)
- [ ] User confirms power supply approach (Anker Solix 220V vs dedicated)
- [ ] Enable PPP in sdkconfig
- [ ] Add esp_modem dependency
- [ ] Write gsm_upstream.c/h
- [ ] Write unit tests (AT command parsing, APN config)
- [ ] Integration test with live hardware
