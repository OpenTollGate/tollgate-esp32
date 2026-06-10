# tollgate_core

ESP-IDF component providing paid WiFi hotspot logic with Cashu e-cash payments.

## Features

### Session & Payment
- **Session management** (`tollgate_core_session`) — time-based and bytes-based sessions with auto-expiry, extension, and revocation
- **Cashu token processing** (`tollgate_core_cashu`) — decode Cashu e-cash tokens, calculate allotments, multi-mint support, proof state checking
- **Per-client firewall** (`tollgate_core_firewall`) — whitelist-based NAT filtering with sandbox mode for unauthenticated clients

### Network
- **DNS server** (`tollgate_core_dns`) — per-client DNS hijack/forward with DoT rejection
- **Stratum proxy** (`tollgate_core_stratum_proxy`) — transparent TCP proxy for mining traffic with job management

### Mining
- **Mining payment** (`tollgate_core_mining`) — Stratum share tracking, hashprice calculation, difficulty conversion, share-to-allotment mapping
- **Stratum client** (`tollgate_core_stratum_client`) — Stratum V1 JSON protocol parsing (mining.notify, mining.set_difficulty), message construction (subscribe, authorize, submit)

### Discovery & Market
- **Beacon price** (`tollgate_core_beacon`) — WiFi vendor IE construction for price/mint/geohash advertising
- **Market scanner** (`tollgate_core_market`) — discover and compare nearby TollGate prices via vendor IEs
- **Mint health** (`tollgate_core_mint_health`) — mint health state tracking with recovery threshold, reachable/unreachable transitions

### Portal & Client
- **Portal rendering** (`tollgate_core_portal`) — HTML template rendering with multi-key substitution, usage calculation, captive URI detection
- **TollGate client** (`tollgate_core_client`) — upstream TollGate discovery JSON parsing, session/usage response parsing, renewal threshold logic, price comparison

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
    .spend_proofs = my_spend_proofs,
    .get_stratum_url = my_get_stratum_url,
    .get_stratum_port = my_get_stratum_port,
    .get_stratum_user = my_get_stratum_user,
    .get_stratum_pass = my_get_stratum_pass,
    .get_mining_port = my_get_mining_port,
    .get_hashprice_sats_per_ghs_day = my_get_hashprice,
    .on_share_accepted = my_on_share_accepted,
    .get_hashrate = my_get_hashrate,
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

### 5. Use sub-modules directly

```c
#include "tollgate_core_portal.h"
char *html = tollgate_core_portal_render(template, subs, nsubs);

#include "tollgate_core_stratum_client.h"
tollgate_core_stratum_build_subscribe(buf, size, req_id);

#include "tollgate_core_mint_health.h"
tollgate_core_mint_health_update(&state, idx, probe_ok, status, err, time);

#include "tollgate_core_client.h"
bool renew = tollgate_core_client_should_renew(remaining, allotment, threshold);
```

## API Reference

| Header | Description |
|--------|-------------|
| `tollgate_core.h` | Main API: init, process_payment, tick, status queries |
| `tollgate_platform.h` | Platform callback struct definition |
| `tollgate_core_session.h` | Session create/find/extend/revoke |
| `tollgate_core_cashu.h` | Token decode, checkstate, allotment |
| `tollgate_core_firewall.h` | Client grant/revoke, sandbox ports |
| `tollgate_core_dns.h` | DNS start/stop |
| `tollgate_core_mining.h` | Share tracking, hashprice, difficulty |
| `tollgate_core_stratum_proxy.h` | Proxy init/stop, job management |
| `tollgate_core_beacon.h` | IE construction, mint/npub hashing |
| `tollgate_core_market.h` | Market table, price comparison |
| `tollgate_core_portal.h` | Template rendering, usage calc, captive URIs |
| `tollgate_core_stratum_client.h` | Stratum V1 protocol parsing |
| `tollgate_core_mint_health.h` | Mint health state tracking |
| `tollgate_core_client.h` | Discovery/session parsing, renewal logic |

## Dependencies

- ESP-IDF >= 5.3
- mbedtls (SHA-256, HMAC)
- cJSON
- lwip
- esp_wifi (for beacon/market features)

## License

MIT — see [LICENSE](LICENSE).
