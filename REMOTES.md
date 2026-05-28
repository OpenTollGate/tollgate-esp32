# Git Remotes — esp32-tollgate

## Nostr Identity

- **npub:** `npub12m5exm2uk3xa674cc5r0hlyvccs5xxn7qv83ezuteefv5972nquq4j4szl`
- **naddr coordinate:** `naddr1qq8x2umsxvez6ar0d3kxwct5v5pzq4hfjdk4edzdm4at33gxl07ge33pgvd8uqc0rj9chnjjegtu4xpcqvzqqqrhnyq3gamnwvaz7tmjv4kxz7fwdenkjapwv3jhv3t9utz`
- **ngit version:** 2.3.0

## Remotes

| Name | URL | Type | Purpose |
|------|-----|------|---------|
| `origin` | `nostr://npub12m5.../relay.ngit.dev/esp32-tollgate` | nostr (ngit) | Primary ngit relay |
| `orangesync` | `nostr://npub12m5.../ngit.orangesync.tech/esp32-tollgate` | nostr (ngit) | Self-hosted ngit relay (offline) |
| `github` | `https://github.com/OpenTollGate/tollgate-esp32` | HTTPS | Primary GitHub mirror |
| `ngit-dev` | `nostr://npub12m5.../relay.ngit.dev/esp32-tollgate` | nostr (ngit) | Public ngit relay (same as origin) |
| `gitnostr` | `nostr://npub12m5.../gitnostr.com/esp32-tollgate` | nostr (ngit) | gitnostr.com (auth issues) |

## Infrastructure

| Service | URL | Role | Status |
|---------|-----|------|--------|
| GitHub (primary) | `https://github.com/OpenTollGate/tollgate-esp32` | Main code host | Active |
| GitHub (NerdQAxePlus) | `https://github.com/c03rad0r/ESP-Miner-NerdQAxePlus` | Fork repo | Active |
| GRASP (Git Server) | `https://git.orangesync.tech` | Git smart HTTP over Nostr | Offline |
| ngit Relay (Nostr) | `wss://ngit.orangesync.tech` | Nostr relay for git state events | Offline |
| GitWorkshop | `https://workshop.orangesync.tech` | Web UI for browsing nostr git repos | Offline |
| relay.ngit.dev | `wss://relay.ngit.dev` | Public ngit relay (most reliable) | Active |
| gitnostr.com | `https://gitnostr.com` | Public ngit relay | Auth issues |
| IDF Component Registry | `components.espressif.com/c03rad0r/tollgate_core` | Published component v1.2.0 | Active |

## Branches (esp32-tollgate)

| Branch | HEAD | Status | Pushed to |
|--------|------|--------|-----------|
| `master` | `10c69b0` | Production | GitHub, relay.ngit.dev |
| `feature/tollgate-core-v2` | `8f18b7a` (v1.3.0) | Active — tollgate_core extraction + fixes | GitHub, relay.ngit.dev |
| `feature/miner-integration` | `e75c350` | Merged into feature/tollgate-core-v2 | — |
| `feature/tollgate-core-component` | `144b48f` | Superseded by feature/tollgate-core-v2 | — |
| `feature/display-fix` | `565d6a7` | Stale — Playwright E2E test plan | No |
| `feature/cvm-integration` | `2cd372c` | Merged to master | Yes |
| `feature/local-relay` | `25eb0c5` | Merged to master | Yes |
| `feature/mining-payment` | `ef9ae98` | Merged to master | Yes |

## Branches (NerdQAxePlus)

| Branch | HEAD | Status | Pushed to |
|--------|------|--------|-----------|
| `develop` | `a2fd1fa6` | Active — 9 TollGate commits rebased on upstream `4b8f3225` | GitHub |

## Local Worktrees

| Path | Branch | Purpose |
|------|--------|---------|
| `/home/c03rad0r/esp32-tollgate` | `feature/tollgate-core-v2` | Main working repo |
| `/home/c03rad0r/esp-miner-nerdqaxeplus` | `develop` | NerdQAxePlus fork |

## Board Inventory

| Board | Port | MAC | SSID | AP IP | Notes |
|-------|------|-----|------|-------|-------|
| A | `/dev/ttyACM1` | `94:a9:90:2e:37:7c` | `TollGate-B96D80` | `10.185.47.1` | USB flash failing, cable issue |
| B | `/dev/ttyACM0` | `fc:01:2c:c5:50:50` | `TollGate-C0E9CA` | `10.192.45.1` | Flashed with NerdQAxePlus TOLLGATE=1 |
| NerdAxe | `/dev/ttyACM2` | `80:b5:4e:c7:79:88` | TBD | TBD | Actual BM1366 ASIC, fan-damaged |

**Important:** Ports shift on every USB replug. Always verify with `esptool --port <port> chip-id`.

## Cross-Referenced Repositories

### ESP-Miner-NerdQAxePlus (Fork)

- **GitHub:** `https://github.com/c03rad0r/ESP-Miner-NerdQAxePlus`
- **Clone:** `/home/c03rad0r/esp-miner-nerdqaxeplus`
- **Branch:** `develop` (HEAD `a2fd1fa6`)
- **Upstream:** `https://github.com/shufps/ESP-Miner-NerdQAxePlus`
- **Dependency:** Uses `tollgate_core` component v1.3.0 (rsynced from esp32-tollgate)
- **Build:** `TOLLGATE=1 BOARD=NERDQAXEPLUS idf.py build` (or `BOARD=NERDAXE` for BM1366 variant)
- **Nostr remote:** `ngit-dev` added but push deferred (repo too large)

### tollgate_core Component

- **Source:** `components/tollgate_core/` on `feature/tollgate-core-v2`
- **Version:** v1.3.0
- **Registry:** `c03rad0r/tollgate_core` on components.espressif.com (v1.2.0 published, v1.3.0 pending)
- **Files:** 13 C source files, 2 public headers, CMakeLists.txt, idf_component.yml
- **Design doc:** `docs/TOLLGATE_CORE_DESIGN.md`

## Backup Bundles

| File | Contents | Size |
|------|----------|------|
| `/home/c03rad0r/mining-work-backup/feature-miner-integration-latest.bundle` | `feature/miner-integration` branch | ~950KB |
| `/home/c03rad0r/mining-work-backup/feature-mining-payment.bundle` | `feature/mining-payment` branch | ~755KB |
| `/home/c03rad0r/mining-work-backup/nerdqaxeplus-tollgate.bundle` | NerdQAxePlus `develop` branch | ~48MB |

## How to Push

```bash
# esp32-tollgate → GitHub (primary)
git push github master
git push github feature/tollgate-core-v2

# esp32-tollgate → nostr
git push ngit-dev --all

# NerdQAxePlus → GitHub
cd /home/c03rad0r/esp-miner-nerdqaxeplus
git push github develop

# IDF Component Registry
compote component pack --path components/tollgate_core
compote component upload --name tollgate_core --namespace c03rad0r
```

## Nostr Push Troubleshooting

The "failed to list from https://..." messages during `git push` are cosmetic noise from relays that don't support git smart HTTP. Look for "Everything up-to-date" or "new state" from the primary relay.

- **relay.ngit.dev** — most reliable, always works
- **gitnostr.com** — auth issues, often fails
- **ngit.orangesync.tech** — offline

## Current Status

- **orangesync.tech** — entirely offline (git, ngit, relay all DOWN)
- **GitHub** — primary remote, active
- **relay.ngit.dev** — secondary, working
- **IDF Component Registry** — v1.2.0 live, v1.3.0 pending upload
