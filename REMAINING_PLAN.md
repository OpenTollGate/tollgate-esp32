# TollGate Core — Remaining Execution Plan

**Branch:** `feature/tollgate-core-v2`
**Created:** 2026-05-23
**Goal:** Complete all remaining extraction, integration, CI/CD, and publishing steps.

---

## Checklist

### Phase A: Version bump + metadata
- [ ] A1. Bump `idf_component.yml` to v1.2.0, add MIT license, targets, tags
- [ ] A2. Create `LICENSE` file (MIT) in `components/tollgate_core/`
- [ ] A3. Update `CHANGELOG.md` with v1.2.0 entries
- [ ] A4. Update `README.md` to document all 13 modules
- [ ] A5. Run `make test-unit` + `idf.py build`
- [ ] A6. Commit + push

### Phase B: Re-sync component to NerdQAxePlus
- [ ] B1. Copy 4 new source pairs + updated files to NerdQAxePlus
- [ ] B2. Verify `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build`
- [ ] B3. Commit + push to NerdQAxePlus develop

### Phase C: Full wallet integration on NerdQAxePlus (Phase 9c)
- [ ] C1. Copy `nucula_lib/` component and `nucula_src/` submodule to NerdQAxePlus
- [ ] C2. Update NerdQAxePlus top-level CMakeLists to REQUIRES `nucula_lib`
- [ ] C3. Replace `spend_proofs` stub with `nucula_wallet_receive()`
- [ ] C4. Add `nucula_wallet_init(mint_url)` to `tollgate_nerdqaxe_init()`
- [ ] C5. Wire `get_hashrate` to actual ASIC stats
- [ ] C6. Wire `on_share_accepted` to real share tracking
- [ ] C7. Verify build
- [ ] C8. Commit + push

### Phase D: NerdQAxePlus UI integration (Phase 9d)
- [ ] D1. Add POST `/api/tollgate/pay` endpoint accepting Cashu tokens
- [ ] D2. Add minimal captive portal HTML page
- [ ] D3. Add GET `/api/tollgate/wallet` and POST `/api/tollgate/wallet/send` endpoints
- [ ] D4. Update portal screen: wallet balance, session count, payment QR
- [ ] D5. Add session status to mining screen
- [ ] D6. Verify build, flash, test payment flow
- [ ] D7. Commit + push

### Phase E: Ansible CI/CD workflow (Phase 10b)
- [ ] E1. Create ansible role `ci_runner` with tasks for Docker + ESP-IDF
- [ ] E2. Create Python nostr relay listener script
- [ ] E3. Create systemd service unit for listener
- [ ] E4. Create ansible playbook `20-ci-runner.yml`
- [ ] E5. Create ansible playbook for localhost hardware runner
- [ ] E6. Test ansible deployment

### Phase F: Publish to IDF Component Registry (Phase 10c)
- [ ] F1. Create `docs/PUBLISHING.md` with step-by-step upload instructions
- [ ] F2. Run `compote registry login` (manual — requires browser OAuth)
- [ ] F3. Run `compote component upload --dry-run` to validate
- [ ] F4. Run `compote component upload` to publish v1.2.0
- [ ] F5. Verify with clean test project
- [ ] F6. Commit + push updated metadata

### Phase G: Hardware smoke test
- [ ] G1. Flash Board A with updated firmware
- [ ] G2. Run `TOLLGATE_IP=10.185.47.1 BOARD=a make test-smoke`
- [ ] G3. Commit any fixes

---

## Execution Order

```
A → G → B → C → D → E → F
```

Phase A is quick metadata updates. G verifies on hardware. B syncs to NerdQAxePlus.
C adds the wallet. D adds the UI. E sets up CI/CD. F publishes.

---

## Architecture Notes

### CI/CD Design
- **Runner:** Docker container using `espressif/idf:latest` image
- **Trigger:** Python WebSocket client subscribed to `wss://ngit.orangesync.tech`
  for NIP-34 git state events (kind 30617)
- **Pipeline:** clone from GRASP → `idf.py build` → `make test-unit` → report
- **Hardware tests:** Run on localhost (dev machine) where boards are connected
- **Reporting:** Build status posted as nostr badge event (kind 1985)

### Wallet Integration Design
- `nucula_lib` provides C API: `nucula_wallet_init()`, `nucula_wallet_receive()`, `nucula_wallet_send()`, `nucula_wallet_balance()`
- `spend_proofs` callback in `tollgate_platform_t` maps directly to `nucula_wallet_receive()`
- Wallet proofs persisted to NVS automatically by nucula
- `libsecp256k1` already compiled in NerdQAxePlus build with Schnorr + Extrakeys modules

### Component Registry
- License: MIT (required for upload)
- Namespace: GitHub username (auto-created on first login)
- Version immutability: once uploaded, cannot overwrite — must bump version
- `compote` CLI installed at `~/.local/bin/compote` (v3.0.2)
