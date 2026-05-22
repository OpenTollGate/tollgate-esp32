# ESP32 pytest Migration Plan

Migrate ESP32 board integration tests from Node.js `.mjs` + Makefile to pytest with proper fixtures for board provisioning (mutex locks, flashing, SPIFFS config, teardown).

## Architecture

### New Files

| File | Location | Purpose |
|------|----------|---------|
| `esp32_board.py` | `lib/` | `ESP32Board` class: lock, flash, config, reset, HTTP API, wait_for_boot |
| `conftest.py` | `tests/esp32/` | Board fixtures (`board_a`, `board_b`, `board_c`), marker registration, `.env` loading |
| `pytest-esp32.ini` | project root | Separate pytest config for ESP32 tests |

### Migrated Tests

| Priority | Source (.mjs) | Target (pytest) | Payment? | Boards |
|----------|--------------|-----------------|----------|--------|
| 1 | `tests/integration/api.mjs` | `tests/esp32/test_api.py` | No | a |
| 2 | `tests/integration/smoke.mjs` | `tests/esp32/test_smoke.py` | No | a |
| 3 | `tests/integration/test-cvm-roundtrip.mjs` | `tests/esp32/test_cvm.py` | No | a |
| 4 | `tests/integration/test-local-relay.mjs` | `tests/esp32/test_local_relay.py` | No | a |
| 5 | `tests/integration/test-relay-nip11.mjs` | `tests/esp32/test_relay_nip11.py` | No | a |
| 6 | `tests/integration/test-market.mjs` | `tests/esp32/test_market.py` | No | a |
| 7 | `tests/integration/test-cross-board.mjs` | `tests/esp32/test_cross_board.py` | No | a, c |
| 8 | `tests/integration/test-reset-auth.mjs` | `tests/esp32/test_reset_auth.py` | Yes | a |
| 9 | `tests/integration/test-session-expiry.mjs` | `tests/esp32/test_session_expiry.py` | Yes | a |
| 10 | `tests/integration/test-dns-firewall.mjs` | `tests/esp32/test_dns_firewall.py` | Yes | a |

All paths are relative to `/home/c03rad0r/physical-router-test-automation/`.

---

## `lib/esp32_board.py` — ESP32Board Helper

```python
class ESP32Board:
    """ESP32 board: lock, flash, SPIFFS config, reset, HTTP API access."""

    LOCK_DIR = "/home/c03rad0r/physical-router-test-automation/locks"
    SPIFFS_OFFSET = 0x410000
    SPIFFS_SIZE = 0xF0000
    IDF_PATH = os.environ.get("IDF_PATH", os.path.expanduser("~/esp/esp-idf"))
    SPIFFSGEN = f"{IDF_PATH}/components/spiffs/spiffsgen.py"

    def __init__(self, board_id, port, nsec, ssid, ip, worktree):
        self.board_id = board_id   # "a", "b", "c"
        self.port = port           # "/dev/ttyACM0"
        self.nsec = nsec
        self.ssid = ssid           # derived from nsec via HMAC-SHA512
        self.ip = ip               # derived AP IP
        self.worktree = worktree   # /home/c03rad0r/esp32-tollgate
        self._lock_held = False

    # --- Lock (board-{id}.lock, same as Makefile) ---
    def acquire_lock(self, phase="pytest"): ...
    def release_lock(self): ...

    # --- Firmware ---
    def build(self): ...       # idf.py build (in worktree)
    def flash(self, baud=460800): ...  # build + idf.py -p {port} flash

    # --- SPIFFS Config ---
    def write_config(self, wifi_ssid, wifi_password, mint_url, **extra): ...
    def write_config_ap_only(self, **extra): ...
    # 1. mktemp -d
    # 2. Write config.json with nsec, wifi_networks, mint_url, price, relays
    # 3. python3 spiffsgen.py --page-size 256 --obj-name-len 32 \
    #      --use-magic --use-magic-len 0xF0000 tmpdir/ tmpdir/spiffs.bin
    # 4. esptool --port {port} --baud {baud} write_flash 0x410000 spiffs.bin
    # 5. esptool --port {port} run  (reset)

    # --- Reset ---
    def reset(self): ...       # esptool --port {port} run

    # --- Wait ---
    def wait_for_boot(self, timeout=30): ...  # poll http://{ip}:2121/
    def wait_for_ap(self, timeout=30): ...    # poll ping {ip}

    # --- HTTP API (port 2121) ---
    def api_get(self, path="/") -> (int, str): ...
    def api_post(self, path, data="") -> (int, str): ...

    # --- Captive Portal (port 80) ---
    def http_get(self, path="/") -> (int, str): ...

    # --- WiFi client (Linux nmcli) ---
    def connect_wifi(self, interface="wlp59s0"): ...
    def disconnect_wifi(self, interface="wlp59s0"): ...

    # --- Context manager ---
    def __enter__(self): return self
    def __exit__(self, *exc): self.release_lock()
```

## `pytest-esp32.ini`

```ini
[pytest]
pythonpath = .
testpaths = tests/esp32
python_files = test_*.py
python_functions = test_*
markers =
    boards(*ids): specify which ESP32 boards to provision
    api: API-level test (HTTP only, no browser)
    smoke: quick connectivity check
    payment: requires mint/Cashu token
    cvm: ContextVM MCP test
    relay: Nostr relay test
    cross_board: requires multiple boards
addopts = -v --tb=short --timeout=120 --timeout-method=thread
```

## `tests/esp32/conftest.py` — Fixtures

```python
# Loads .env from /home/c03rad0r/esp32-tollgate/.env
# Board registry maps board_id -> env var names

BOARD_REGISTRY = {
    "a": dict(port_env="PORT_A", nsec_env="BOARD_A_NSEC",
              ssid_env="BOARD_A_SSID", ip_env="BOARD_A_IP"),
    "b": dict(port_env="PORT_B", nsec_env="BOARD_B_NSEC",
              ssid_env="BOARD_B_SSID", ip_env="BOARD_B_IP"),
    "c": dict(port_env="PORT_C", nsec_env="BOARD_C_NSEC",
              ssid_env="BOARD_C_SSID", ip_env="BOARD_C_IP"),
}

# CLI options
--esp32-no-flash    Skip firmware flash (assume already flashed)
--esp32-no-config   Skip SPIFFS config write
--esp32-worktree    Path to esp32-tollgate (default: /home/c03rad0r/esp32-tollgate)

# Fixtures (session-scoped, lazy provisioning)
@pytest.fixture(scope="session")
def board_a(request): ...
    # 1. acquire_lock
    # 2. flash (unless --esp32-no-flash)
    # 3. write_config (unless --esp32-no-config)
    # 4. wait_for_boot
    # 5. yield board
    # 6. release_lock

@pytest.fixture(scope="session")
def board_b(request): ...  # same pattern

@pytest.fixture(scope="session")
def board_c(request): ...  # same pattern

@pytest.fixture(scope="session")
def cashu_esp32(): ...  # CashuMint for test mint
```

## Marker-based Board Selection

Tests declare which boards they need via `@pytest.mark.boards("a", "c")`.
Only those board fixtures are provisioned. Tests requesting boards not
available in the session are skipped.

```python
# Single board
pytestmark = [pytest.mark.api, pytest.mark.boards("a")]

# Multiple boards
pytestmark = [pytest.mark.cross_board, pytest.mark.boards("a", "c")]
```

## Setup/Teardown Lifecycle

```
pytest -c pytest-esp32.ini
  |
  +-- pytest_sessionstart: (board fixtures are lazy)
  |
  +-- First test requests board_a:
  |     +-- acquire_lock("board-a") -> board-a.lock
  |     +-- flash() -> idf.py build + flash (~60s)
  |     +-- write_config() -> spiffsgen + esptool write_flash + reset (~5s)
  |     +-- wait_for_boot() -> poll :2121 until 200 (~10s)
  |     +-- yield board_a
  |
  +-- Tests run using board_a.api_get(), board_a.http_get(), etc.
  |
  +-- pytest_sessionfinish:
  |     +-- board_a.release_lock() -> remove board-a.lock
  |
  +-- Done
```

## CLI Usage

```bash
# Run all ESP32 tests (flash + configure by default)
pytest -c pytest-esp32.ini

# Skip flash (faster, assumes firmware already on board)
pytest -c pytest-esp32.ini --esp32-no-flash

# Skip config too (board already configured)
pytest -c pytest-esp32.ini --esp32-no-flash --esp32-no-config

# Run only smoke tests
pytest -c pytest-esp32.ini -m smoke

# Run only cross-board tests (needs boards a + c)
pytest -c pytest-esp32.ini -m cross_board

# Single test file
pytest -c pytest-esp32.ini tests/esp32/test_api.py

# Verbose with no timeout (debugging)
pytest -c pytest-esp32.ini tests/esp32/test_api.py -v --timeout=0
```

## Key Design Decisions

1. **Flash by default** — guarantees known-good firmware state per session
2. **Merged into existing `esp32/` test suite** — removed duplicate `tests/esp32/`, added new tests to `esp32/tests/`
3. **Same lock files as Makefile** — `board-{a,b,c}.lock` in `physical-router-test-automation/locks/`, mutually exclusive with `make flash-a` etc.
4. **Board C support** — `esp32/boards.env` and `esp32/boards.py` support all three boards
5. **Session-scoped `board_connected`** — connects WiFi once per session
6. **`--board` flag** — `pytest --board=a` selects board via marker filtering
7. **Test ordering matters** — smoke tests first (they're self-contained), then API (some reset auth), then relay
8. **Existing fixtures** — `board_connected`, `http`, `wifi` from `esp32/conftest.py`; added `relay`, `dns` helpers

## Board Identity Reference

| Board | Port | SSID | AP IP | nsec (first 8) |
|-------|------|------|-------|-----------------|
| A | `/dev/ttyACM2` | `TollGate-B96D80` | `10.185.47.1` | `9af47906...` |
| B | `/dev/ttyACM1` | `TollGate-C0E9CA` | `10.192.45.1` | `a1b2c3d4...` |
| C | `/dev/ttyACM0` | `TollGate-4A2510` | `10.74.63.1` | `71bf3f4d...` |

Ports change on every USB replug. Verify with `esptool.py --port <port> chip_id`.

---

## Checklist

### Infrastructure

- [x] Create `lib/esp32_board.py` — ESP32Board class (standalone, for programmatic use)
- [x] Merge into existing `esp32/` test suite (conftest.py, pytest.ini, boards.py)
- [x] Update `esp32/boards.env` with correct port assignments
- [x] Add `relay` and `dns` helpers to `esp32/conftest.py`
- [x] Add nsec env vars to `esp32/boards.env` (for CVM tests)
- [x] Add Makefile targets for new test files

### Test Files in `esp32/tests/`

**Existing (pre-migration):**
- [x] `test_smoke.py` — 6 tests, SSID visible, portal, grant, internet, reset
- [x] `test_api.py` — 9 tests, debug, discovery, mints, wallet, whoami, usage, captcha URIs
- [x] `test_captive_portal.py` — 7 tests, branding, price, input, button, placeholders, mint URL
- [x] `test_multi_mint.py` — 4 tests, pricing, mint list, health, wallet
- [x] `test_wallet_funding.py` — 2 tests, fund from upstream, spend (spend skipped)

**New (migrated from .mjs):**
- [x] `test_local_relay.py` — 5 tests, WS connect, REQ/EOSE, publish, close, multi-conn
- [x] `test_relay_nip11.py` — 4 tests, JSON, fields, NIP support, name
- [x] `test_market.py` — 3 tests, JSON, entry structure, empty state
- [x] `test_cvm.py` — 4 tests, API reachable, 11316/11317 announcements, MCP roundtrip
- [x] `test_reset_auth.py` — 6 tests, reset, payment, session lifecycle
- [x] `test_session_expiry.py` — 1 test, 65s wait for expiry + renewal
- [x] `test_dns_firewall.py` — 10 tests, DNS hijack, NAT filter, before/after auth/revoke

### Verification

- [x] `pytest --collect-only` — 60 tests collected from 12 files
- [x] **37 passed, 1 skipped** — `pytest test_smoke.py test_api.py test_captive_portal.py test_multi_mint.py test_market.py test_relay_nip11.py test_local_relay.py --board=a`
- [x] Lock files created/cleaned: `board-a.lock` lifecycle verified
- [x] Relay tests pass: WS connect, EOSE, publish, close, NIP-11
- [x] Market tests pass: JSON, entry structure
- [ ] Payment-dependent tests (reset_auth, session_expiry, dns_firewall AfterAuth/AfterRevoke) — need mint available
- [ ] Cross-board tests — need Board C provisioned and reachable
- [ ] CVM roundtrip — needs nsec env var set and board connected to internet

### Known Issues

1. **Test ordering required** — `test_smoke.py` must run first (it's self-contained). `test_api.py` calls `reset_authentication` in `test_usage_endpoint_no_session` which breaks portal access for subsequent tests. Solution: run smoke first.
2. **Relay max connections** — ESP32 relay supports ~2 simultaneous WebSocket connections. `test_multiple_connections` tests with 2 connections.
3. **Board crashes after destructive tests** — `reset_authentication` in DNS/firewall tests can crash the HTTP server after multiple rapid resets. Board needs physical reset between test groups.
4. **Payment-dependent tests blocked** — Mint intermittently returns `sqlite3.OperationalError: unable to open database file`. Tests work when mint is healthy.
