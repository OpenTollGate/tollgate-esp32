---
Integration Assessment — [TRACK NAME: balloon-tollgate]

Date
2026-07-18

What Works Right Now

- **WiFi AP+STA with captive portal** (ESP32-S3): tollgate_main.c provides AP+STA mode, DNS hijack, captive portal HTTP server on :80. Stripped C3 build retains this. Host unit tests (86/86) pass for captive_portal, session, firewall, identity, geohash, nostr_event, cashu.
- **Cashu wallet via nucula_lib** (ESP32-S3): nucula_wallet.cpp bridges to nucula::Wallet C++ class. Uses secp256k1 for Schnorr/crypto. Wallet data persists to SPIFFS (/spiffs/wallet/). Proofs, keysets, counters stored as JSON files. No PSRAM allocations found in nucula wallet code — all heap usage is standard malloc/new.
- **Identity derivation from nsec** (ESP32-S3, host): identity.c derives npub, MAC, SSID, IP via HMAC-SHA512. Pure computation, no PSRAM dependency. Unit tested.
- **Nostr event signing** (ESP32-S3, host): nostr_event.c does NIP-01 serialization + BIP-340 Schnorr signing via secp256k1. Unit tested with known vectors.
- **tollgate_core component** (ESP32-S3, designed for C3): idf_component.yml declares both esp32s3 and esp32c3 as supported targets. Contains session management, firewall, Cashu token processing, DNS. 2799 lines of C across 24 files. No PSRAM allocations in tollgate_core source.
- **Stripped C3 build configuration** (ESP32-C3 config only): sdkconfig.defaults.esp32c3 and partitions_c3.csv exist. C3 config sets CONFIG_SPIRAM=n, 4MB flash, 16 lwIP sockets, mbedTLS dynamic buffer. Partition table: 3MB factory app, 960KB SPIFFS, no relay_store partition.
- **Unit test suite** (host-only): 86 tests pass covering portal, cashu, session, firewall, identity, geohash, nostr_event, relay types/validator, stratum, mining, display, keyboard, touch, market, beacon, cvm, mcp, nip04, lnurl, lightning, faucet, remote_miner, wifi_setup, negentropy, sub_manager, deletion, client_core, mint_health.

What Exists But Is Untested

- **ESP32-C3 actual compilation**: build/CMakeCache.txt shows IDF_TARGET=esp32c3 but no full build has been completed in this session. The sdkconfig file still contains S3 settings (CONFIG_IDF_TARGET="esp32s3", CONFIG_SPIRAM=y, 16MB flash) — it has not been regenerated from sdkconfig.defaults.esp32c3. A clean `idf.py set-target esp32c3` + build has not been verified.
- **Stripped binary size of 1.16MB**: claimed in commit 9b76965 message as ESP32-S3 target. C3 binary size may differ due to RISC-V vs Xtensa code generation. Not verified on C3.
- **tollgate_core on C3**: component declares esp32c3 support but has never been compiled or flashed for C3. Mining/stratum/market/beacon modules in tollgate_core are compiled but not called in stripped main — they add to binary size but are dead code at runtime.
- **mbedTLS on C3**: sdkconfig.defaults.esp32c3 sets CONFIG_MBEDTLS_HARDWARE_MPI=n. C3 has no hardware MPI accelerator (unlike S3). HTTPS performance to Cashu mint will be slower. Not benchmarked.
- **Cashu wallet on C3**: nucula_lib uses C++ (std::string, std::vector) which increases binary size and heap fragmentation on C3. No PSRAM fallback exists. Wallet operations (receive/send/swap) not tested on C3 hardware.

What Does NOT Exist Yet

- **Local Nostr relay on C3**: wisp_relay component is present in components/ but NOT in the stripped build's REQUIRES list. The C3 partition table has no relay_store partition. To enable: must add relay_store partition (minimum ~512KB to be useful), re-add wisp_relay to CMakeLists.txt, and fix PSRAM-dependent storage_engine.c allocation. Does not exist for C3.
- **Relay selector and sync manager on C3**: relay_selector.c and sync_manager.c exist in main/ but are excluded from the stripped C3 build. They use xTaskCreate with 16KB stacks and maintain WebSocket connections. Need to be re-added and tested on C3 if balloon requires Nostr relay connectivity.
- **C3-optimized partition table**: Current partitions_c3.csv has 3MB factory + 960KB SPIFFS. If local relay is needed, relay_store partition must be carved from the 3MB app space or SPIFFS, reducing available space further. No relay-aware C3 partition table exists.
- **C3 GPIO pin mapping for any display**: The axs15231b display uses pins 39,40,45,47,48 which don't exist on ESP32-C3 (max GPIO21). No alternative display driver or pin mapping exists. If a display is needed for balloon, a completely different display solution must be written.
- **Balloon-specific radio integration**: No LoRa, LR2021, or balloon-specific radio code exists. The firmware only knows WiFi AP/STA. Upstream communication for balloon telemetry/commands does not exist.
- **Power management for balloon flight**: No deep sleep, light sleep, or power optimization code exists. Current firmware runs WiFi continuously. Balloon power budget code does not exist.
- **C3 build verification**: No C3 binary has been produced, flashed, or tested on actual C3 hardware.

Blockers for ESP32-C3 Port

1. **Display (axs15231b) — HARD BLOCKER, cannot work on C3**: The QSPI TFT display driver uses GPIO pins 39, 40, 45, 47, 48 — none of which exist on ESP32-C3 (GPIO0-21 only). Additionally, the 300KB framebuffer is allocated via `heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM)` — C3 has no PSRAM. Even with different pins, 300KB framebuffer in 400KB total RAM is impossible. The display is already disabled in the stripped build (moved to disabled_components/). **This is acceptable for balloon if no display is needed.**

2. **wisp_relay storage_engine PSRAM allocation — BLOCKER for local relay on C3**: storage_engine.c line 120 allocates the event index via `heap_caps_calloc(5000, sizeof(storage_index_entry_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. Each entry is 52 bytes (packed: 32B event_id + 4B created_at + 4B expires_at + 4B file_index + 2B kind + 4B pubkey_prefix + 1B flags + 1B reserved). 5000 entries = 260KB. The fallback to `calloc(1000, ...)` = 52KB in internal RAM. On C3 with ~400KB RAM total (minus WiFi/OS overhead ~150KB), 52KB for relay index is tight but feasible at reduced capacity. The code already has a PSRAM fallback path. **Local relay is excluded from stripped C3 build; re-enabling requires partition table changes and testing the fallback path.**

3. **LittleFS relay_store partition — BLOCKER for local relay on C3**: S3 partition table allocates 4MB (0x400000) for relay_store. C3 has only 4MB total flash. The C3 partition table has NO relay_store partition. Even a minimal relay store (512KB) would require reducing the 3MB factory app partition to 2.5MB, leaving only 1.34MB headroom over the current 1.16MB binary. **Must decide: is local Nostr relay needed for balloon, or can it be dropped?**

4. **sdkconfig not regenerated for C3 — PROCESS BLOCKER**: The sdkconfig file still contains ESP32-S3 settings (CONFIG_IDF_TARGET="esp32s3", CONFIG_SPIRAM=y, 16MB flash, Xtensa arch). build/CMakeCache.txt says esp32c3 but the sdkconfig hasn't been regenerated. A clean `idf.py set-target esp32c3` + full reconfigure is needed before any C3 build can succeed.

5. **xTaskCreatePinnedToCore to core 1 — ALREADY RESOLVED**: display.c and cvm_server.c used `xTaskCreatePinnedToCore(..., 1)` to pin to core 1. C3 is single-core. Both modules are excluded from the stripped build. No remaining code pins to core 1. **Not a blocker for stripped build.**

6. **C++ std lib heap usage in nucula_lib — RISK, not a hard blocker**: nucula wallet uses std::string, std::vector extensively (104 std:: references in wallet.cpp alone). C3 has 400KB RAM with no PSRAM. C++ exceptions are disabled in ESP-IDF by default but heap fragmentation from std::vector reallocations could be problematic. Not verified. **Needs testing on actual C3 hardware.**

7. **secp256k1 precomputed tables in flash — RISK, not a hard blocker**: precomputed_ecmult.c is 16,456 lines of const data. ECMULT_GEN_PREC_BITS=4 and ECMULT_WINDOW_SIZE=8 are set. These tables go into flash (.rodata), not RAM. Estimated 200-400KB flash contribution. With 3MB app partition and 1.16MB binary, there is ~1.84MB headroom. **Likely fits but needs verification with actual C3 build.**

Estimated Effort

- **Regenerate sdkconfig for C3 and attempt clean build**: 2-4 hours. Run `idf.py set-target esp32c3`, fix any compilation errors (Xtensa-specific code, missing headers), verify binary fits in 3MB partition.
- **Test stripped captive portal + Cashu on C3 hardware**: 1-2 days. Flash to C3 board, verify WiFi AP starts, captive portal serves, Cashu receive/send works with real mint. Benchmark mbedTLS HTTPS performance.
- **Re-enable local Nostr relay for C3 (if needed)**: 3-5 days. Create new C3 partition table with relay_store (512KB-1MB), reduce app partition. Fix storage_engine PSRAM allocation (already has fallback to 1000 entries/52KB). Test wisp_relay WebSocket server on C3. Verify relay doesn't OOM under load.
- **Re-enable relay_selector and sync_manager for C3 (if needed)**: 2-3 days. Re-add to CMakeLists.txt, verify 16KB task stacks fit, test WebSocket relay connections. May need stack reduction.
- **Balloon radio integration (LoRa/LR2021)**: 1-2 weeks. New code entirely. Depends on radio module selection and protocol design from other tracks.
- **Power management for balloon flight**: 3-5 days. Deep sleep between telemetry intervals, WiFi duty cycling, PSRAM-less memory management during sleep.
- **C3 display alternative (if needed)**: 1-2 weeks. If any visual output is needed, must select a C3-compatible display (SPI/I2C, small framebuffer), write new driver, adapt display.c UI code.

Dependencies on Other Tracks

- **Radio/telemetry track**: Balloon needs upstream communication. Current firmware only has WiFi. If balloon uses LoRa/LR2021, this track depends on the radio track providing a driver and protocol specification.
- **Balloon mission profile track**: Need to know flight duration, telemetry interval, and whether local Nostr relay is required for balloon operations or can be dropped to save flash/RAM.
- **Hardware allocation track**: Need to know which ESP32-C3 board variant is selected (flash size, GPIO availability, PSRAM presence — most C3 modules have no PSRAM).

Shared Resources Needed

- **ESP32-C3 development board**: At least one C3 board with 4MB flash for testing. Need to verify actual flash chip size and partition layout. Other tracks may also need C3 boards.
- **ESP32-S3 board (Board A)**: Still needed as reference platform to verify stripped build doesn't regress on S3. Currently available (3 boards per AGENTS.md).
- **Cashu mint access**: Need HTTPS access to test mint for wallet verification on C3. Not hardware but a shared dependency.
- **LR2021 or LoRa module**: If balloon requires radio, need the actual radio module hardware. Likely shared with radio track.

Integration Checklist

1. `idf.py set-target esp32c3` succeeds and produces a binary ≤ 3MB
2. Binary flashes to ESP32-C3 board without errors
3. WiFi AP starts with derived SSID and IP on C3
4. Captive portal HTTP server responds on :80 on C3
5. Cashu wallet initializes on C3 (nucula_wallet_init succeeds)
6. Cashu receive (token → proofs) works on C3 with real mint
7. Cashu send (amount → token) works on C3 with real mint
8. DNS server hijacks/forwards correctly on C3
9. Session management and firewall work on C3
10. Free heap after all services started ≥ 80KB on C3 (to handle peak loads)
11. mbedTLS HTTPS to mint completes in < 10 seconds on C3
12. Decision made: local Nostr relay needed or not for balloon
13. If relay needed: relay_store partition carved from C3 flash, wisp_relay re-enabled, tested
14. If relay needed: relay_selector and sync_manager re-enabled and tested on C3
15. Balloon radio integration code written (if required by mission profile)
16. Power management code written (if required by mission profile)
17. All 86+ unit tests still pass after C3 changes
18. Integration test against live C3 board passes (captive portal + payment flow)

Key Risks

- **C3 RAM exhaustion**: 400KB total RAM, ~150KB consumed by WiFi/OS/IDF overhead, leaving ~250KB. Cashu wallet C++ std::string/vector allocations, mbedTLS dynamic buffers (4KB in + 4KB out = 8KB per TLS session), HTTP server buffers, and task stacks (16KB for services task) could exhaust RAM. The 16KB services task stack (`xTaskCreate(services_start_task, "svc_start", 16384, ...)`) alone is 16KB. May need stack reduction.
- **mbedTLS performance on C3**: C3 has no hardware MPI (CONFIG_MBEDTLS_HARDWARE_MPI=n). All bignum operations are software. HTTPS TLS handshake to Cashu mint could take 10-30 seconds. If the mint uses TLS 1.3 with large key exchanges, it may timeout. This is the single biggest runtime risk.
- **C++ exception/RTTI bloat**: nucula_lib uses C++ with std::string/vector. If RTTI or exceptions are accidentally enabled, binary size could exceed 3MB. Must verify -fno-exceptions -fno-rtti are set for C3 builds.
- **secp256k1 on RISC-V**: libsecp256k1 has optimized assembly for x86_64/ARM. On RISC-V (C3), it falls back to generic C. Schnorr signing performance may be 2-5x slower. Not a blocker but affects user experience for Cashu operations.
- **Partition table redesign cascade**: If local relay is needed, adding relay_store reduces app partition from 3MB to ~2MB. If binary grows beyond 2MB (e.g., adding radio code), flash is exhausted. May need to drop more features or use a larger flash C3 variant (some C3 modules have 8MB flash).
- **WiFi AP+STA concurrency on C3**: C3 is single-core. Running AP (captive portal) and STA (upstream to mint) simultaneously on a single core with WiFi + TLS + Cashu processing may cause timing issues or watchdog resets. S3 had two cores to distribute load.

Questions for the Coordinator

1. **Is the local Nostr relay required for balloon?** It's the largest flash/RAM consumer. Dropping it saves ~4MB flash and ~260KB RAM (or 52KB with fallback). This is the single biggest design decision for this track.
2. **Is any display needed on the balloon payload?** The current QSPI TFT cannot work on C3 (wrong GPIO pins, no PSRAM for 300KB framebuffer). If no display, we save significant complexity. If display is needed, what kind (small OLED via I2C? status LED only?).
3. **Does the balloon payload need WiFi AP at all, or just STA?** If the balloon doesn't serve a captive portal to nearby devices (just relays telemetry via radio), the captive portal, DNS server, and firewall can be stripped, saving ~50KB flash and significant RAM.
4. **What is the upstream communication method?** WiFi STA to ground station? LoRa? LR2021? This determines what networking code stays vs. gets stripped.
5. **Is the ESP32-C3 module confirmed to have exactly 4MB flash?** Some C3 modules come with 8MB flash. If 8MB is available, the relay_store partition can be added without sacrificing app space, dramatically easing the port.
6. **What is the target FreeRTOS tick rate and task priority scheme?** C3 single-core means all tasks compete for one core. Current config has CONFIG_FREERTOS_HZ=1000 which is fine, but task priorities may need rebalancing.
7. **Should tollgate_core's mining/stratum/market/beacon modules be stripped for C3?** They're compiled but unused in the stripped main. Removing them from tollgate_core's CMakeLists.txt for C3 builds would save flash. Estimate ~100-200KB savings.