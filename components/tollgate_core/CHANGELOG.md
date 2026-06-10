# Changelog

## 1.2.0 (2026-05-23)

- Added `tollgate_core_portal` — HTML template rendering, usage calculation, captive URI detection
- Added `tollgate_core_stratum_client` — Stratum V1 JSON protocol parsing, message building (subscribe, authorize, submit, mining.notify, mining.set_difficulty)
- Added `tollgate_core_mint_health` — mint health state tracking with recovery threshold (3 consecutive successes), reachable/unreachable transitions
- Added `tollgate_core_client` — upstream TollGate discovery JSON parsing (kind 10021), session response parsing (kind 1022), usage string parsing, renewal threshold check, price-per-minute calculation
- Added MIT license for IDF Component Registry compatibility
- 13 compiled source files, 14 headers, full platform abstraction via `tollgate_platform_t`
- 25 host unit tests covering all modules

## 1.1.0 (2026-05-23)

- Added `tollgate_core_beacon` — WiFi vendor IE construction (hash_mint, hash_npub, build_ie)
- Added `tollgate_core_market` — market table logic (parse_ie, find_cheapest, update_ssid)
- Platform-agnostic `tollgate_beacon_config_t` input struct for IE construction
- `tollgate_vendor_ie_t` abstraction decouples market parsing from ESP WiFi types

## 1.0.0 (2026-05-22)

- Initial release
- Session management (time + bytes based)
- Cashu token decode and allotment calculation
- Per-client firewall with sandbox mode
- DNS server with per-client hijack/forward
- Mining payment tracking (nbits→difficulty, hashprice, share→allotment)
- Stratum proxy with job management
- Platform abstraction via `tollgate_platform_t` callbacks
