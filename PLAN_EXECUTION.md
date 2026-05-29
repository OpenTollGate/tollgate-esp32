# Execution Plan — TollGate + NerdQAxePlus Integration

**Created:** 2026-05-28  
**Updated:** 2026-05-29  
**Status:** In Progress — Mining-for-Internet implementation (see PLAN_MINING_INTERNET.md)

---

## Locked-In Decisions

| Decision | Choice |
|----------|--------|
| esp32-tollgate GitHub repo | `OpenTollGate/tollgate-esp32` |
| esp32-tollgate default branch | `master` |
| NerdQAxePlus GitHub repo | `c03rad0r/ESP-Miner-NerdQAxePlus` |
| NerdQAxePlus branch | `develop` |
| Target boards | Board A + Board B + NerdAxe |
| Board variant | `NERDQAXEPLUS` for dev boards, `NERDAXE` for BM1366 hardware |
| Nostr remotes | `ngit-dev` (relay.ngit.dev) + `gitnostr` (gitnostr.com) |
| Stratum username fallback | Option A: `#ifdef TOLLGATE` uses `CONFIG_STRATUM_USER` |
| Component version | v1.3.0 |
| ASIC-less testing | Safe — TollGate WiFi/payment/portal work without ASIC |
| Upstream WiFi | `EnterSSID-2.4GHz` / `c03rad0r123!` |

## Board Inventory

| Board | Port | MAC | Hardware | Status |
|-------|------|-----|----------|--------|
| A | `/dev/ttyACM1` | `94:a9:90:2e:37:7c` | ESP32-S3 dev | USB flash failing, cable issue |
| B | `/dev/ttyACM0` | `fc:01:2c:c5:50:50` | ESP32-S3 dev | Flashed, needs NVS config + smoke test |
| NerdAxe | `/dev/ttyACM2` | `80:b5:4e:c7:79:88` | BM1366 ASIC (fan-damaged) | Needs flash + smoke test |

**Important:** Ports shift on USB replug. Always verify with `esptool --port <port> chip-id`.

## Current State

| Item | Value |
|------|-------|
| esp32-tollgate `master` | HEAD `10c69b0` (pushed to GitHub + relay.ngit.dev) |
| esp32-tollgate `feature/tollgate-core-v2` | HEAD `8f18b7a` v1.3.0 (pushed to GitHub + relay.ngit.dev) |
| NerdQAxePlus `develop` | HEAD `a2fd1fa6` (9 TollGate commits rebased on upstream, pushed to GitHub) |
| IDF Component Registry | v1.2.0 live, v1.3.0 pending upload |
| Unit tests | 29 tests, all pass |
| orangesync.tech | Entirely offline |

---

## Step 1: Stratum Username Fallback ✅

- [x] 1a-c. Implemented, committed, verified

## Step 2: Add Nostr Remotes (esp32-tollgate) ✅

- [x] 2a-f. `ngit-dev` and `gitnostr` added, pushed to relay.ngit.dev

## Step 3: Add Nostr Remotes (NerdQAxePlus) ✅

- [x] 3a-d. Remotes added, push to nostr deferred (repo too large)

## Step 4: Push esp32-tollgate to GitHub ✅

- [x] 4a-d. Both branches pushed, default set to `master`

## Step 5: Rebase + Push NerdQAxePlus ✅

- [x] 5a-g. 9 TollGate commits rebased onto upstream `4b8f3225`, pushed to GitHub

## Step 6: Bump tollgate_core to v1.3.0 ✅

- [x] 6a-f. Bumped, committed, pushed, rsynced to NerdQAxePlus

## Step 7: Re-publish to IDF Component Registry (PENDING)

- [ ] 7a. `compote component pack --path components/tollgate_core`
- [ ] 7b. `compote component upload --name tollgate_core --namespace c03rad0r`
- [ ] 7c. Verify v1.3.0 on components.espressif.com

## Step 8: Update Docs ✅

- [x] 8a. PLAN_BITAXE_FIXES.md updated with checklist
- [x] 8b. REMOTES.md updated with current state
- [x] 8c. PLAN_EXECUTION.md updated

## Step 9: Build NerdQAxePlus ✅

- [x] 9a. `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` passes
- [x] 9b. `TOLLGATE=1 BOARD=NERDAXE idf.py build` — needs verification for NerdAxe

## Step 10: Flash Boards

- [ ] 10a. **NerdAxe** (`/dev/ttyACM2`, MAC `80:b5:4e:c7:79:88`): Flash with `BOARD=NERDAXE`
- [ ] 10b. **Board B** (`/dev/ttyACM0`, MAC `fc:01:2c:c5:50:50`): Already flashed, needs NVS config
- [ ] 10c. **Board A** (`/dev/ttyACM1`, MAC `94:a9:90:2e:37:7c`): Blocked by USB issue

## Step 11: Configure NVS on NerdAxe

- [ ] 11a. Connect to NerdAxe AP, note IP
- [ ] 11b. `curl -X PATCH http://<IP>/api/system -d '{"ssid":"EnterSSID-2.4GHz","wifiPass":"c03rad0r123!"}'`
- [ ] 11c. `curl -X PATCH http://<IP>/api/system -d '{"stratumUser":"bc1q7n70rumyv6lvu8avpml0c3uggvssfu52egum3q.nerdqaxe"}'`
- [ ] 11d. `curl -X POST http://<IP>/api/system/restart`
- [ ] 11e. Wait for reboot, verify upstream WiFi connection

## Step 12: Smoke Test NerdAxe

- [ ] 12a. AP visible
- [ ] 12b. Connect to AP
- [ ] 12c. Captive portal auto-redirects
- [ ] 12d. Portal page renders (price, mint, form)
- [ ] 12e. Generate token: `cashu -h https://testnut-nutshell.mints.orangesync.tech send --legacy 21`
- [ ] 12f. Submit token → payment accepted
- [ ] 12g. Internet passthrough: `curl -I https://google.com`
- [ ] 12h. `GET /api/tollgate/status`
- [ ] 12i. `GET /api/tollgate/wallet`
- [ ] 12j. `GET /api/tollgate/config`
- [ ] 12k. `POST /api/tollgate/grant`
- [ ] 12l. Disconnect + reconnect → internet blocked
- [ ] 12m. `GET /api/system/info` — check ASIC detection
- [ ] 12n. Serial monitor: check ASIC init output, hashrate

## Step 13: Smoke Test Board B (if time permits)

- [ ] 13a-13n. Repeat Step 12 on Board B

## Step 14: Final Commit + Push

- [ ] 14a. Commit doc updates
- [ ] 14b. Push esp32-tollgate to GitHub + nostr
- [ ] 14c. Push NerdQAxePlus to GitHub

---

## Risks

| Risk | Mitigation |
|------|------------|
| NerdAxe ASIC dead from fan block | TollGate still works ASIC-less; diagnose via serial |
| Test mint down | Skip payment test, test everything else |
| Board doesn't create AP | Check serial monitor |
| `BOARD=NERDAXE` build fails | Use `NERDQAXEPLUS` variant, works ASIC-less |
| `compote` CLI syntax changed | Check `compote --help` before upload |

---

## Next Phase: Mining-for-Internet

See **PLAN_MINING_INTERNET.md** for the full mining + ecash integration plan:
- Phase 1: SV1 MVP (stratum proxy, hashpool translator, ecash poller)
- Phase 2: SV2 direct connection (Noise NX, binary codec, direct to pool)
