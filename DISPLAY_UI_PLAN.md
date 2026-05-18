# TollGate Display UI Design

## Display Hardware
- **Panel:** 3.5" IPS, 320x480 portrait (AXS15231B QSPI)
- **Font:** 8x8 bitmap, scalable (1x=8px, 2x=16px, 3x=24px)
- **Capabilities:** Text rendering, QR codes, filled rectangles
- **No touch input** — display is output-only signage

## Color Palette

| Color | RGB565 | Usage |
|-------|--------|-------|
| Black | `0x0000` | Background |
| White | `0xFFFF` | Primary text |
| Cyan | `0x07FF` | Titles, labels |
| Yellow | `0xFFE0` | Price, warnings |
| Green | `0x07E0` | Success, wallet OK |
| Orange | `0xFD20` | Accent (Bitcoin orange) |
| Red | `0xF800` | Errors, alerts |
| Dim gray | `0x8410` | Secondary info |

## Screen States

### 1. BOOT
Shown during startup until WiFi connects and services start.

```
┌──────────────────────────┐
│        TollGate          │  cyan, scale 2
│      connecting...       │  yellow, scale 1
│    WiFi: trying...       │  dim, scale 1
└──────────────────────────┘
```

### 2. READY — QR Cycling
Cycles every 5 seconds between WiFi QR and Portal QR.

**View A — WiFi QR:**
```
┌──────────────────────────┐
│         ┌──────┐         │
│         │  QR  │         │  WIFI:S:<ssid>;T:nopass;;
│         └──────┘         │
│   Scan to connect        │  cyan
│   SSID: TollGate-XXXX   │  white
│   21 sats/min            │  orange
│   Wallet: 420 sats       │  green/yellow/red
└──────────────────────────┘
```

**View B — Portal QR:**
```
┌──────────────────────────┐
│         ┌──────┐         │
│         │  QR  │         │  http://10.x.x.x/
│         └──────┘         │
│   Portal URL             │  cyan
│   Mint: testnut...       │  orange
│   21 sats/min            │  orange
│   Clients: 3             │  green
└──────────────────────────┘
```

### 3. PAYMENT_RECEIVED
Shows for 3 seconds after payment, then returns to READY.

```
┌──────────────────────────┐
│   ████████████████████   │  green bar
│      ACCESS GRANTED      │  white on green, scale 2
│   ████████████████████   │
│      Paid: 42 sats       │  white
│      Time: 2 min         │  white
│   Wallet: 462 sats       │  green
└──────────────────────────┘
```

### 4. ERROR
Shown when upstream WiFi is disconnected.

```
┌──────────────────────────┐
│   ████████████████████   │  red bar
│     NO UPSTREAM          │  white on red, scale 2
│   ████████████████████   │
│   Internet unavailable   │  white
│   Check WiFi config      │  yellow
│   AP still active        │  green
│   SSID: TollGate-XXXX   │  dim
└──────────────────────────┘
```

## State Synchronization

### Data sources and update triggers

| Display data | Source function | Update trigger |
|-------------|----------------|----------------|
| SSID | `config.ap_ssid` | Once at `start_services()` |
| Portal URL | `config.ap_ip_str` | Once at `start_services()` |
| Mint URL | `config.mint_url` | Once at `start_services()` |
| Price | `config.price_per_step` | Once at `start_services()` |
| **Wallet balance** | `nucula_wallet_balance()` | **Every 5s in main loop** |
| **Client count** | `session_active_count()` | **Every 5s in main loop + AP events** |
| **Last payment** | `display_notify_payment()` | **On each payment in API handler** |
| WiFi status | Event handler | On STA connect/disconnect |

### State transitions

```
app_main()
  └─ display_set_state(BOOT)
  └─ display_update(price, mint, ssid)   ← config data available after config_init

wifi_event_handler(STA_DISCONNECTED)
  └─ display_set_state(ERROR)
  └─ display_update(wifi_status="retrying...")

ip_event_handler(STA_GOT_IP)
  └─ start_services()
       └─ display_set_state(READY)
       └─ display_update(ssid, portal_url, mint, price)

tollgate_api (POST / payment)
  └─ session_create()
  └─ nucula_wallet_receive()
  └─ display_notify_payment(amount_sats, allotment)   ← NEW
  └─ display_set_state(PAYMENT_RECEIVED)

display_task (3s timeout)
  └─ auto-return PAYMENT_RECEIVED → READY

app_main() main loop (every 5s)
  └─ display_update(wallet_balance, client_count)     ← NEW periodic refresh
```

### New API: `display_notify_payment()`

```c
void display_notify_payment(int amount_sats, int64_t allotment_ms);
```

Stores the last payment amount and time purchased for the PAYMENT screen to display.

## Implementation Checklist

### Done
- [x] QSPI driver working with correct colors (DMA byte-swap + RAMWR)
- [x] BOOT screen with title and WiFi status
- [x] READY screen with QR cycling, price, mint, balance, clients
- [x] PAYMENT screen layout (green banner, amount, time, wallet)
- [x] ERROR screen layout (red banner, guidance, AP status)
- [x] WiFi disconnect → ERROR state transition
- [x] WiFi connect → READY state transition

### TODO — State Sync
- [ ] Periodic display data refresh in main loop (wallet balance, client count every 5s)
- [ ] `display_notify_payment()` API to pass payment amount and allotment
- [ ] Call `display_set_state(PAYMENT_RECEIVED)` from tollgate_api.c after payment
- [ ] Pass config data (price, mint) to display during boot phase
- [ ] Update client count on AP station connect/disconnect events

### TODO — Polish
- [ ] Run `make test-unit` to check for regressions
- [ ] Commit, push, prepare for merge
