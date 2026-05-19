# Git Remotes — esp32-tollgate

## Nostr Identity

- **npub:** `npub12m5exm2uk3xa674cc5r0hlyvccs5xxn7qv83ezuteefv5972nquq4j4szl`
- **naddr coordinate:** `naddr1qq8x2umsxvez6ar0d3kxwct5v5pzq4hfjdk4edzdm4at33gxl07ge33pgvd8uqc0rj9chnjjegtu4xpcqvzqqqrhnyq3gamnwvaz7tmjv4kxz7fwdenkjapwv3jhv3t9utz`
- **ngit version:** 2.3.0

## Remotes

| Name | URL | Type | Purpose |
|------|-----|------|---------|
| `origin` | `nostr://npub12m5.../relay.ngit.dev/esp32-tollgate` | nostr (ngit) | Primary ngit relay |
| `orangesync` | `nostr://npub12m5.../ngit.orangesync.tech/esp32-tollgate` | nostr (ngit) | Self-hosted ngit relay |

## Infrastructure

| Service | URL | Role |
|---------|-----|------|
| GRASP (Git Server) | `https://git.orangesync.tech` | Git smart HTTP over Nostr (NIP-34) |
| ngit Relay (Nostr) | `wss://ngit.orangesync.tech` | Nostr relay for git state events |
| GitWorkshop | `https://workshop.orangesync.tech` | Web UI for browsing nostr git repos |
| relay.ngit.dev | `wss://relay.ngit.dev` | Public ngit relay (secondary) |

## GRASP Servers

Configured in NIP-34 repo announcement:

- `git.orangesync.tech` (self-hosted, primary)
- `relay.anzenkodo.workers.dev`
- `nostr.koning-degraaf.nl`
- `relay.damus.io`
- `nos.lol`

## Additional Relays

- `wss://ngit.orangesync.tech` (self-hosted, for state events)

## Branches

| Branch | HEAD | Status | Merged into master |
|--------|------|--------|--------------------|
| `master` | `62bce81` | Production | — |
| `feature/miner-integration` | `e75c350` | Active — tollgate_core C++ compat + NerdQAxePlus integration | No |
| `feature/tollgate-core-component` | `144b48f` | Active — original tollgate_core extraction + E2E fixes | No |
| `feature/display-fix` | `565d6a7` | Stale — Playwright E2E test plan for /setup | No |
| `feature/cvm-integration` | `2cd372c` | Merged to master | Yes (squashed) |
| `feature/local-relay` | `25eb0c5` | Merged to master | Yes (squashed) |
| `feature/mining-payment` | `ef9ae98` | Merged to master | Yes (squashed as `e366ceb`) |
| `backup/multi-mint-support-pre-rebase` | `ef2de0f` | Backup only | N/A |

## Local Worktrees

| Path | Branch | Purpose |
|------|--------|---------|
| `/home/c03rad0r/esp32-tollgate` | `master` | Main repo (clean, for other LLM sessions) |
| `/home/c03rad0r/esp32-miner-integration` | `feature/miner-integration` | tollgate_core + mining integration |
| `/home/c03rad0r/esp32-tollgate-arch` | `feature/tollgate-core-component` | Architecture branch (original component extraction) |

## Cross-Referenced Repositories

### esp-miner-nerdqaxeplus-tollgate (NerdQAxePlus Fork)

- **Identifier:** `esp-miner-nerdqaxeplus-tollgate`
- **Clone:** `/home/c03rad0r/esp-miner-nerdqaxeplus`
- **Branch:** `develop` (HEAD `83e09ab9`)
- **Upstream:** `https://github.com/shufps/ESP-Miner-NerdQAxePlus`
- **Dependency:** Uses `tollgate_core` component from `feature/miner-integration` branch (symlinked at `components/tollgate_core`)
- **Build:** `BOARD=NERDAXE TOLLGATE=1 idf.py build`
- **See:** `REMOTES.md` in that repo for full details

### tollgate_core Component

- **Source:** `components/tollgate_core/` on `feature/miner-integration` branch
- **Files:** 7 C source files, 2 public headers, CMakeLists.txt, idf_component.yml
- **Design doc:** `docs/TOLLGATE_CORE_DESIGN.md` on `feature/tollgate-core-component` branch

## Backup Bundles

| File | Contents | Size |
|------|----------|------|
| `/home/c03rad0r/mining-work-backup/feature-miner-integration-latest.bundle` | `feature/miner-integration` branch | ~950KB |
| `/home/c03rad0r/mining-work-backup/feature-mining-payment.bundle` | `feature/mining-payment` branch | ~755KB |
| `/home/c03rad0r/mining-work-backup/nerdqaxeplus-tollgate.bundle` | NerdQAxePlus `develop` branch | ~48MB |

### Restore from bundle

```bash
# Clone from bundle
git clone feature-miner-integration-latest.bundle -b feature/miner-integration esp32-tollgate-restored

# Or fetch into existing repo
git remote add bundle-src /home/c03rad0r/mining-work-backup/feature-miner-integration-latest.bundle
git fetch bundle-src
git checkout -b feature/miner-integration bundle-src/feature/miner-integration
```

## How to Push

```bash
# Push all branches to orangesync
git push orangesync --all

# Push specific branch
git push orangesync feature/miner-integration

# Push to both remotes
git push origin master && git push orangesync master

# Update repo metadata
ngit repo edit -d -g git.orangesync.tech --relay wss://ngit.orangesync.tech

# Sync (force GRASP servers to update)
ngit sync -d -v
```

## Nostr Push Troubleshooting

The "failed to list from https://..." messages during `git push` are **cosmetic noise** from third-party nostr relays that don't support git smart HTTP. The actual push succeeds — look for "Everything up-to-date" or "new state" from the primary relay in the output.

Key pattern:
```
failed to list from https://nos.lol/...   ← cosmetic, ignore
failed to list from https://relay.damus.io/...  ← cosmetic, ignore
To nostr://npub.../ngit.orangesync.tech/esp32-tollgate
 = [up to date]      master -> master     ← actual result
Everything up-to-date
```
