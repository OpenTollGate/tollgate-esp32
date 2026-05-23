# tollgate_core

ESP-IDF component providing paid WiFi hotspot logic with Cashu e-cash payments.

## Features

- **Session management** — time-based and bytes-based sessions with auto-expiry
- **Cashu token processing** — decode Cashu e-cash tokens, calculate allotments, multi-mint support
- **Per-client firewall** — whitelist-based NAT filtering with sandbox mode
- **DNS server** — per-client DNS hijack/forward with DoT rejection
- **Mining payment** — Stratum share tracking, hashprice calculation, difficulty conversion
- **Stratum proxy** — transparent TCP proxy for mining traffic with job management
- **Beacon price** — WiFi vendor IE construction for price/mint/geohash advertising
- **Market scanner** — discover and compare nearby TollGate prices

## Integration

### 1. Implement platform callbacks

```c
#include "tollgate_platform.h"

static tollgate_platform_t platform = {
    .get_price_sats = my_get_price,
    .get_step_ms = my_get_step_ms,
    .get_mint_url = my_get_mint_url,
    .get_metric = my_get_metric,
    .get_time_ms = my_get_time_ms,
    // ... other callbacks
};
```

### 2. Initialize

```c
#include "tollgate_core.h"

esp_ip4_addr_t ap_ip;
IP4_ADDR(&ap_ip, 192, 168, 4, 1);
tollgate_core_init(&platform, ap_ip);

esp_ip4_addr_t dns;
IP4_ADDR(&dns, 8, 8, 8, 8);
tollgate_core_dns_start(dns);
```

### 3. Process payments

```c
tollgate_core_process_payment(client_ip, cashu_token_string);
```

### 4. Tick

Call `tollgate_core_tick()` periodically (e.g., every second) to expire sessions.

## API Reference

See `include/tollgate_core.h` for the public API and `include/tollgate_platform.h` for platform callbacks.

## Dependencies

- ESP-IDF >= 5.3
- mbedtls (SHA-256, HMAC)
- cJSON
- lwip
- esp_wifi (for beacon/market features)

## License

Proprietary — see repository for details.
