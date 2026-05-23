# Changelog

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
