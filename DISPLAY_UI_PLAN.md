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
| Dark bg | `0x2104` | Card backgrounds |

## Screen States

### 1. BOOT
Shown during startup until WiFi connects and services start.

```
┌──────────────────────────┐  0
│                          │
│        TollGate          │  y=180, cyan, scale 2
│      connecting...       │  y=205, yellow, scale 1
│                          │
│    WiFi: trying...       │  y=260, dim, scale 1
│                          │
└──────────────────────────┘  479
```

WiFi status line shows: "trying...", "connected!", "failed (retry)"

### 2. READY — QR Cycling (primary screen)
Cycles every 5 seconds between WiFi QR and Portal QR.

**View A — WiFi QR:**
```
┌──────────────────────────┐  0
│         ┌──────┐         │
│         │  QR  │         │  QR: WIFI:S:<ssid>;T:nopass;;
│         │      │         │  Centered in top 2/3 of screen
│         └──────┘         │
│                          │  ~y=320
│   Scan to connect        │  cyan, scale 1
│   SSID: TollGate-XXXX   │  white, scale 1
│   21 sats/min            │  orange, scale 1
│   Wallet: 420 sats       │  green, scale 1
└──────────────────────────┘  479
```

**View B — Portal QR:**
```
┌──────────────────────────┐  0
│         ┌──────┐         │
│         │  QR  │         │  QR: http://10.x.x.x/
│         │      │         │
│         └──────┘         │
│                          │  ~y=320
│   Portal URL             │  cyan, scale 1
│   testnut.cashu.space    │  orange, scale 1 (mint domain)
│   21 sats/min            │  yellow, scale 1
│   Clients: 3             │  green, scale 1
└──────────────────────────┘  479
```

### 3. PAYMENT_RECEIVED
Shows for 3 seconds after payment, then returns to READY.

```
┌──────────────────────────┐  0
│                          │
│   ████████████████████   │  green filled bar, y=190..230
│      ACCESS GRANTED      │  white on green, scale 2
│   ████████████████████   │
│                          │
│      Paid: 21 sats       │  white, scale 1
│      Time: 1 min         │  white, scale 1
│                          │
│   Wallet: 441 sats       │  green, scale 1
└──────────────────────────┘  479
```

### 4. ERROR
Shown when upstream WiFi is disconnected.

```
┌──────────────────────────┐  0
│   ████████████████████   │  red filled bar, y=190..230
│     NO UPSTREAM          │  white on red, scale 2
│   ████████████████████   │
│                          │
│   Internet unavailable   │  white, scale 1
│   Check WiFi config      │  yellow, scale 1
│                          │
│   AP still active        │  green, scale 1
│   SSID: TollGate-XXXX   │  dim, scale 1
└──────────────────────────┘  479
```

## Data Flow

### display_update() receives:
```c
void display_update(const char *ap_ssid, int active_clients,
                    uint64_t wallet_balance, const char *portal_url);
```

### Enhanced to also receive:
```c
void display_update(const char *ap_ssid, int active_clients,
                    uint64_t wallet_balance, const char *portal_url,
                    const char *mint_url, int price_per_step,
                    const char *wifi_status);
```

### display_set_state() triggers:
- `DISPLAY_BOOT` → at startup
- `DISPLAY_READY` → when services start (WiFi connected)
- `DISPLAY_PAYMENT_RECEIVED` → on successful payment (auto-returns to READY)
- `DISPLAY_ERROR` → when upstream WiFi disconnects

## Implementation Notes

- Render every 2 seconds (reduces SPI bus load vs 1 second)
- QR codes: auto-size based on string length, centered in top portion
- Mint URL: show only domain part (truncate at first `/`)
- Wallet balance: color-coded (green > 100, yellow > 0, red = 0)
- Client count: "Clients: N" or empty string if 0
