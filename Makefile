SHELL := /bin/bash
.DEFAULT_GOAL := help

-include .env
export

IDF_PATH ?= $(HOME)/esp/esp-idf
PROJECT_DIR := $(shell pwd)
BUILD_DIR := $(PROJECT_DIR)/build
PORT_A ?= /dev/ttyACM0
PORT_B ?= /dev/ttyACM1
PORT ?= $(PORT_A)
BAUD ?= 460800
TARGET ?= esp32s3

HARDWARE_LOCK_DIR := /home/c03rad0r/physical-router-test-automation/locks

RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
BOLD := \033[1m
RESET := \033[0m

define require_lock_a
	@if [ ! -f "$(HARDWARE_LOCK_DIR)/board-a.lock" ]; then \
		echo "$(RED)$(BOLD)Board A not locked — run 'make lock-a PHASE=\"description\"' first$(RESET)"; \
		echo "$(YELLOW)Another LLM session may be using Board A.$(RESET)"; \
		exit 1; \
	fi
endef

define require_lock_b
	@if [ ! -f "$(HARDWARE_LOCK_DIR)/board-b.lock" ]; then \
		echo "$(RED)$(BOLD)Board B not locked — run 'make lock-b PHASE=\"description\"' first$(RESET)"; \
		echo "$(YELLOW)Another LLM session may be using Board B.$(RESET)"; \
		exit 1; \
	fi
endef

define _acquire_lock
	@if [ -f "$(HARDWARE_LOCK_DIR)/$(1).lock" ]; then \
		echo "$(RED)$(BOLD)Cannot acquire lock — $(1) already locked:$(RESET)"; \
		echo ""; \
		cat $(HARDWARE_LOCK_DIR)/$(1).lock | sed 's/^/  /'; \
		echo ""; \
		echo "$(YELLOW)Use 'make force-unlock-$(1)' to override.$(RESET)"; \
		exit 1; \
	fi; \
	branch=$$(git branch --show-current 2>/dev/null || echo "unknown"); \
	worktree=$$(pwd); \
	echo "locked: true" > $(HARDWARE_LOCK_DIR)/$(1).lock; \
	echo "board: $(1)" >> $(HARDWARE_LOCK_DIR)/$(1).lock; \
	echo "branch: $$branch" >> $(HARDWARE_LOCK_DIR)/$(1).lock; \
	echo "worktree: $$worktree" >> $(HARDWARE_LOCK_DIR)/$(1).lock; \
	echo "session: $$USER@$$HOSTNAME" >> $(HARDWARE_LOCK_DIR)/$(1).lock; \
	echo "timestamp: $$(date -u '+%Y-%m-%dT%H:%M:%SZ')" >> $(HARDWARE_LOCK_DIR)/$(1).lock; \
	echo "phase: $(PHASE)" >> $(HARDWARE_LOCK_DIR)/$(1).lock; \
	echo "$(GREEN)$(BOLD)$(1) lock acquired$(RESET)"; \
	cat $(HARDWARE_LOCK_DIR)/$(1).lock
endef

NODE ?= node
NPM ?= npm
PYTHON ?= python3

TOLLGATE_IP ?= 10.192.45.1

.PHONY: help setup detect-ports detect-chip detect-all
.PHONY: flash flash-a flash-b monitor monitor-a monitor-b
.PHONY: test test-unit test-integration test-e2e test-all
.PHONY: test-smoke test-api test-network test-portal test-payment
.PHONY: test-reset-auth test-session-expiry test-dns-firewall test-cvm
.PHONY: tokens wallet-setup wallet-info wallet-balance mint-token send-token
.PHONY: clean erase-nvs reset serial-log bootstrap-config
.PHONY: cvm-pubkey cvm-test-tool cvm-announce
.PHONY: lock-a lock-b unlock-a unlock-b force-unlock-a force-unlock-b lock-status

help:
	@echo "TollGate ESP32 — Makefile"
	@echo ""
	@echo "Discovery:"
	@echo "  detect-ports      List connected ESP32 serial ports"
	@echo "  detect-chip       Identify chip type on PORT"
	@echo "  detect-all        Full device inventory"
	@echo ""
	@echo "Build & Flash:"
	@echo "  flash             Build + flash to PORT (default: PORT_A)"
	@echo "  flash-a           Flash to PORT_A"
	@echo "  flash-b           Flash to PORT_B"
	@echo "  monitor           Serial monitor on PORT"
	@echo ""
	@echo "Testing:"
	@echo "  test-unit         Host C unit tests (no hardware)"
	@echo "  test-integration  Node.js integration tests (live board)"
	@echo "  test-e2e          Playwright browser E2E tests"
	@echo "  test-all          Run all three test layers"
	@echo "  test-smoke        Quick 30s smoke test"
	@echo "  test-reset-auth   Reset auth + per-client NAT filter test"
	@echo "  test-dns-firewall DNS hijack + NAT filter test"
	@echo "  test-session-expiry Session lifecycle with 65s expiry wait"
	@echo "  test-cvm          ContextVM protocol integration test"
	@echo ""
	@echo "ContextVM:"
	@echo "  cvm-pubkey        Print board's ContextVM npub"
	@echo "  cvm-announce      Trigger re-publish of CEP-6 announcements"
	@echo "  cvm-test-tool     Send single MCP tools/call (METHOD=get_config)"
	@echo ""
	@echo "Wallet:"
	@echo "  wallet-setup      Initialize nutshell wallet for test mint"
	@echo "  wallet-info       Show mint info"
	@echo "  wallet-balance    Show wallet balance"
	@echo "  mint-token        Invoice + send test token (AMOUNT=21)"
	@echo "  send-token        Send cashuA token (AMOUNT=21)"
	@echo ""
	@echo "Utilities:"
	@echo "  clean             Clean build"
	@echo "  erase-nvs         Erase NVS partition on PORT"
	@echo "  reset             Hardware reset on PORT"
	@echo "  bootstrap-config  Write .env values to SPIFFS config.json"
	@echo "  serial-log        Capture serial output"

# ──────────────────────────────────────────────
# Discovery
# ──────────────────────────────────────────────

detect-ports:
	@echo "=== Serial Ports ==="
	@ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "No serial ports found"
	@echo ""
	@echo "=== USB Devices ==="
	@lsusb 2>/dev/null | grep -i -E "serial|uart|cp210|ch340|ftdi|esp" || true
	@echo ""
	@echo "=== By ID ==="
	@ls -la /dev/serial/by-id/ 2>/dev/null || true

detect-chip:
	@echo "=== Detecting chip on $(PORT) ==="
	@python3 -m esptool --port $(PORT) chip_id 2>&1 || \
		$(PYTHON) -c "import esptool; esptool.main(['--port','$(PORT)','chip_id'])" 2>&1 || \
		echo "esptool not found. Run: pip install esptool"
	@echo ""
	@python3 -m esptool --port $(PORT) flash_id 2>&1 || true

detect-all: detect-ports
	@for port in /dev/ttyACM* /dev/ttyUSB*; do \
		[ -e "$$port" ] || continue; \
		echo ""; \
		echo "=== Device at $$port ==="; \
		python3 -m esptool --port $$port chip_id 2>&1 | head -5 || true; \
		python3 -m esptool --port $$port flash_id 2>&1 | head -5 || true; \
	done

# ──────────────────────────────────────────────
# Setup
# ──────────────────────────────────────────────

setup:
	@echo "=== Installing esptool ==="
	pip install esptool 2>/dev/null || pip3 install esptool
	@echo ""
	@echo "=== Checking ESP-IDF ==="
	@test -d $(IDF_PATH) && echo "ESP-IDF found at $(IDF_PATH)" || \
		echo "ESP-IDF not found at $(IDF_PATH). Install from https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/"
	@echo ""
	@echo "=== Installing Node deps ==="
	$(NPM) install
	@echo ""
	@echo "=== Installing Python deps ==="
	pip install pyserial 2>/dev/null || pip3 install pyserial
	@echo ""
	@echo "Setup complete!"

# ──────────────────────────────────────────────
# Build & Flash
# ──────────────────────────────────────────────

flash: build
	@echo "=== Flashing to $(PORT) ==="
	. $(IDF_PATH)/export.sh && idf.py -p $(PORT) -b $(BAUD) flash

flash-a: build
	$(call require_lock_a)
	@echo "=== Flashing to $(PORT_A) (Board A) ==="
	. $(IDF_PATH)/export.sh && idf.py -p $(PORT_A) -b $(BAUD) flash

flash-b: build
	$(call require_lock_b)
	@echo "=== Flashing to $(PORT_B) (Board B) ==="
	. $(IDF_PATH)/export.sh && idf.py -p $(PORT_B) -b $(BAUD) flash

build:
	@echo "=== Building $(TARGET) ==="
	. $(IDF_PATH)/export.sh && \
		idf.py set-target $(TARGET) 2>/dev/null; \
		idf.py build

monitor:
	. $(IDF_PATH)/export.sh && idf.py -p $(PORT) monitor

monitor-a: PORT=$(PORT_A)
monitor-a: monitor

monitor-b: PORT=$(PORT_B)
monitor-b: monitor

# ──────────────────────────────────────────────
# Testing
# ──────────────────────────────────────────────

test-unit:
	@echo "=== Running host unit tests ==="
	$(MAKE) -C tests/unit test

test-integration: test-api test-network test-reset-auth test-dns-firewall test-cvm
	@echo "=== Integration tests passed ==="

test-e2e:
	@echo "=== Running Playwright E2E tests ==="
	cd tests/e2e && npx playwright test

test-all: test-unit test-integration test-e2e
	@echo "=== All test layers passed ==="

test: test-unit test-integration
	@echo "=== Tests passed ==="

test-smoke:
	@echo "=== Running smoke test (30s) ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/smoke.mjs

test-api:
	@echo "=== Running API tests ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/api.mjs

test-network:
	@echo "=== Running network tests ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/network.mjs

test-portal:
	@echo "=== Running Playwright portal tests ==="
	cd tests/e2e && npx playwright test captive-portal.spec.mjs

test-payment:
	@echo "=== Running payment tests ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/phase2.mjs

test-reset-auth:
	@echo "=== Running reset auth test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-reset-auth.mjs

test-session-expiry:
	@echo "=== Running session expiry test (65s wait, ~80s total) ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-session-expiry.mjs

test-dns-firewall:
	@echo "=== Running DNS + firewall test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-dns-firewall.mjs

test-cvm:
	@echo "=== Running CVM integration test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-cvm.mjs

# ──────────────────────────────────────────────
# Wallet
# ──────────────────────────────────────────────

wallet-setup:
	@echo "=== Setting up Nutshell wallet for $(TEST_MINT) ==="
	cashu --env-mint $(TEST_MINT) info 2>/dev/null || \
		cashu --env-mint $(TEST_MINT) restore

wallet-info:
	@echo "=== Mint info ==="
	cashu --env-mint $(TEST_MINT) info

wallet-balance:
	@echo "=== Wallet balance ==="
	cashu --env-mint $(TEST_MINT) balance

mint-token:
	@AMOUNT=$${AMOUNT:-21}; \
	echo "=== Minting test token ($$AMOUNT sats) ==="; \
	cashu --env-mint $(TEST_MINT) invoice $$AMOUNT && \
	echo "--- Token (cashuA legacy) ---" && \
	cashu --env-mint $(TEST_MINT) send --legacy $$AMOUNT

send-token:
	@AMOUNT=$${AMOUNT:-21}; \
	echo "=== Sending $$AMOUNT sats as cashuA token ===" && \
	cashu --env-mint $(TEST_MINT) send --legacy $$AMOUNT

tokens: send-token

# ──────────────────────────────────────────────
# ContextVM
# ──────────────────────────────────────────────

cvm-pubkey:
	@echo "=== Board ContextVM npub ==="
	@nak key public a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2 | xargs -I{} nak encode npub {}
	@echo ""
	@echo "Search for this npub on https://contextvm.org/servers"

cvm-announce:
	@echo "=== Triggering CEP-6 re-announcement ==="
	curl -s http://$(TOLLGATE_IP):2121/ | head -1 || echo "Board not reachable"

cvm-test-tool:
	@METHOD=$${METHOD:-get_config}; \
	PARAMS=$${PARAMS:-{}}; \
	echo "=== Calling $$METHOD via CVM ==="; \
	NPUB_HEX=$$(nak key public a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2); \
	CONTENT="$$(echo "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$$METHOD\",\"arguments\":$$PARAMS}}" | jq -c .)"; \
	EVENT_JSON="$$(nak event --kind 25910 --tag p=$$NPUB_HEX --content "$$CONTENT" wss://relay.damus.io 2>/dev/null)"; \
	echo "Published: $$EVENT_JSON"; \
	echo "Waiting for response..."; \
	sleep 3; \
	nak req -k 25910 -a $$NPUB_HEX -l 5 wss://relay.damus.io

# ──────────────────────────────────────────────
# Utilities
# ──────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR) sdkconfig sdkconfig.old
	. $(IDF_PATH)/export.sh && idf.py fullclean

erase-nvs:
	@echo "=== Erasing NVS on $(PORT) ==="
	. $(IDF_PATH)/export.sh && \
		partition_offset=$$(idf.py partition-table 2>/dev/null | grep nvs | awk '{print $$2}'); \
		python3 -m esptool --port $(PORT) erase_region $$partition_offset 0x6000

reset:
	@echo "=== Resetting device on $(PORT) ==="
	python3 -m esptool --port $(PORT) run 2>/dev/null || true

serial-log:
	@echo "=== Capturing serial output from $(PORT) ==="
	python3 -c "import serial; s=serial.Serial('$(PORT)',115200,timeout=1); \
		[print(s.readline().decode(errors='replace'),end='') for _ in iter(lambda: s.readline(), b'')]"

bootstrap-config:
	@echo "=== Bootstrapping config.json ==="
	@echo '{"wifi_networks":[{"ssid":"$(WIFI_SSID)","password":"$(WIFI_PASSWORD)"}],"ap_ssid":"$(AP_SSID)","ap_password":"$(AP_PASSWORD)","mint_url":"$(MINT_URL)","lnurl_url":"$(LNURL_URL)","price_per_step":$(PRICE_PER_STEP),"step_size_ms":$(STEP_SIZE)}' > main/config.json
	@echo "Config written to main/config.json"

# ──────────────────────────────────────────────
# Board Lock Management
# ──────────────────────────────────────────────

lock-a:
	@$(call _acquire_lock,board-a)

lock-b:
	@$(call _acquire_lock,board-b)

unlock-a:
	@if [ -f "$(HARDWARE_LOCK_DIR)/board-a.lock" ]; then \
		rm $(HARDWARE_LOCK_DIR)/board-a.lock; \
		echo "$(GREEN)Board A lock released.$(RESET)"; \
	else \
		echo "$(YELLOW)Board A not locked.$(RESET)"; \
	fi

unlock-b:
	@if [ -f "$(HARDWARE_LOCK_DIR)/board-b.lock" ]; then \
		rm $(HARDWARE_LOCK_DIR)/board-b.lock; \
		echo "$(GREEN)Board B lock released.$(RESET)"; \
	else \
		echo "$(YELLOW)Board B not locked.$(RESET)"; \
	fi

force-unlock-a:
	@echo "$(RED)$(BOLD)WARNING: Force-releasing Board A lock!$(RESET)"
	@cat $(HARDWARE_LOCK_DIR)/board-a.lock 2>/dev/null | sed 's/^/  /' || true
	@rm -f $(HARDWARE_LOCK_DIR)/board-a.lock
	@echo "$(GREEN)Board A lock force-released.$(RESET)"

force-unlock-b:
	@echo "$(RED)$(BOLD)WARNING: Force-releasing Board B lock!$(RESET)"
	@cat $(HARDWARE_LOCK_DIR)/board-b.lock 2>/dev/null | sed 's/^/  /' || true
	@rm -f $(HARDWARE_LOCK_DIR)/board-b.lock
	@echo "$(GREEN)Board B lock force-released.$(RESET)"

lock-status:
	@echo "$(BOLD)Board Lock Status$(RESET)"
	@if [ -f "$(HARDWARE_LOCK_DIR)/board-a.lock" ]; then \
		echo "Board a: $(YELLOW)LOCKED$(RESET)"; \
		cat $(HARDWARE_LOCK_DIR)/board-a.lock | sed 's/^/  /'; \
	else \
		echo "Board a: $(GREEN)available$(RESET)"; \
	fi
	@if [ -f "$(HARDWARE_LOCK_DIR)/board-b.lock" ]; then \
		echo "Board b: $(YELLOW)LOCKED$(RESET)"; \
		cat $(HARDWARE_LOCK_DIR)/board-b.lock | sed 's/^/  /'; \
	else \
		echo "Board b: $(GREEN)available$(RESET)"; \
	fi
