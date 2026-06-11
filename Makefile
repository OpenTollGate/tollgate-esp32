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

NODE ?= node
NPM ?= npm
PYTHON ?= python3

TOLLGATE_IP ?= 10.185.47.1
TOLLGATE_B_IP ?= 10.185.47.1

NSEC_A ?= 9af47906b45aca5e238390f3d03c8274e154198e81aa2095065627d1e61ca968
NSEC_B ?= a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2
MINT_URL ?= https://testnut.cashu.space
BOARD_A_IP = 10.185.47.1
BOARD_B_IP = 10.192.45.1
SPIFFS_OFFSET = 0x410000
SPIFFS_SIZE = 0xF0000
SPIFFSGEN = $(IDF_PATH)/components/spiffs/spiffsgen.py

BOARD ?= b

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

define _require_board_lock
	@if [ ! -f "$(HARDWARE_LOCK_DIR)/board-$(BOARD).lock" ]; then \
		echo "$(RED)$(BOLD)Board $(BOARD) not locked — run 'make lock-$(BOARD) PHASE=\"description\"' first$(RESET)"; \
		echo "$(YELLOW)Another LLM session may be using Board $(BOARD).$(RESET)"; \
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

.PHONY: help setup detect-ports detect-chip detect-all
.PHONY: flash flash-a flash-b monitor monitor-a monitor-b
.PHONY: test test-unit test-integration test-e2e test-all
.PHONY: test-smoke test-api test-network test-portal test-payment
.PHONY: test-reset-auth test-session-expiry test-dns-firewall test-cvm
.PHONY: test-local-relay test-relay-nip11 test-cvm-roundtrip test-cross-board test-cvm-mcp
.PHONY: test-market test-price-discovery test-mining-token
.PHONY: tokens wallet-setup wallet-info wallet-balance mint-token send-token
.PHONY: clean erase-nvs reset serial-log bootstrap-config
.PHONY: write-mining-config write-mining-config-vps
.PHONY: write-b2b-config-a write-b2b-config-b
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
	@echo "  test-local-relay  Local relay pub/sub WebSocket test"
	@echo "  test-relay-nip11  Local relay NIP-11 info document test"
	@echo "  test-cvm-roundtrip CVM MCP request/response via public relay"
	@echo "  test-cvm-mcp      CVM MCP relay integration test"
	@echo "  test-cross-board  Cross-board payment test"
	@echo "  test-mining-token Mining token flow test (needs mining config)"
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
	@echo "  write-mining-config-vps  Write mining config with VPS stratum_host"
	@echo "  write-b2b-config-a       Write B2B upstream config to Board A (TollGate-B96D80)"
	@echo "  write-b2b-config-b       Write B2B downstream config to Board B (client → Board A)"
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
	@echo "$(RED)Error: use 'make flash-a' or 'make flash-b' (per-board lock required)$(RESET)"
	@exit 1

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

monitor-a:
	$(call require_lock_a)
	. $(IDF_PATH)/export.sh && idf.py -p $(PORT_A) monitor

monitor-b:
	$(call require_lock_b)
	. $(IDF_PATH)/export.sh && idf.py -p $(PORT_B) monitor

# ──────────────────────────────────────────────
# Testing
# ──────────────────────────────────────────────

test-unit:
	@echo "=== Running host unit tests ==="
	$(MAKE) -C tests/unit test

test-integration: test-api test-network test-reset-auth test-dns-firewall test-cvm
	@echo "=== Integration tests passed ==="

test-e2e:
	$(call _require_board_lock)
	@echo "=== Running Playwright E2E tests ==="
	cd tests/e2e && npx playwright test

test-all: test-unit test-integration test-e2e
	@echo "=== All test layers passed ==="

test: test-unit test-integration
	@echo "=== Tests passed ==="

test-smoke:
	$(call _require_board_lock)
	@echo "=== Running smoke test (30s) ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/smoke.mjs

test-api:
	$(call _require_board_lock)
	@echo "=== Running API tests ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/api.mjs

test-network:
	$(call _require_board_lock)
	@echo "=== Running network tests ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/network.mjs

test-portal:
	$(call _require_board_lock)
	@echo "=== Running Playwright portal tests ==="
	cd tests/e2e && npx playwright test captive-portal.spec.mjs

test-payment:
	$(call _require_board_lock)
	@echo "=== Running payment tests ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/phase2.mjs

test-reset-auth:
	$(call _require_board_lock)
	@echo "=== Running reset auth test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-reset-auth.mjs

test-session-expiry:
	$(call _require_board_lock)
	@echo "=== Running session expiry test (65s wait, ~80s total) ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-session-expiry.mjs

test-dns-firewall:
	$(call _require_board_lock)
	@echo "=== Running DNS + firewall test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-dns-firewall.mjs

test-cvm:
	$(call _require_board_lock)
	@echo "=== Running CVM integration test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-cvm.mjs

test-cvm-mcp:
	$(call _require_board_lock)
	@echo "=== Running CVM MCP relay integration test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-cvm-mcp-relay.mjs

test-local-relay:
	$(call _require_board_lock)
	@echo "=== Running local relay pub/sub test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-local-relay.mjs

test-relay-nip11:
	$(call _require_board_lock)
	@echo "=== Running relay NIP-11 test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-relay-nip11.mjs

test-cvm-roundtrip:
	$(call _require_board_lock)
	@echo "=== Running CVM MCP roundtrip test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-cvm-roundtrip.mjs

test-cross-board:
	$(call _require_board_lock)
	@echo "=== Running cross-board payment test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-cross-board.mjs

test-market:
	$(call _require_board_lock)
	@echo "=== Running market endpoint test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-market.mjs

test-price-discovery:
	$(call _require_board_lock)
	@echo "=== Running two-board price discovery test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) TOLLGATE_B_IP=$(TOLLGATE_B_IP) $(NODE) tests/integration/test-price-discovery.mjs

test-mining-token:
	$(call _require_board_lock)
	@echo "=== Running mining token flow test ==="
	TOLLGATE_IP=$(TOLLGATE_IP) $(NODE) tests/integration/test-mining-token.mjs

# ──────────────────────────────────────────────
# SPIFFS Config
# ──────────────────────────────────────────────

define write_board_config
	$(call require_lock_$(1))
	@echo "=== Writing SPIFFS config to Board $(1) ($(PORT_$(1))) ==="
	@TMPDIR=$$(mktemp -d) && \
	echo '{"nsec":"$(NSEC_$(1))","wifi_networks":[{"ssid":"$(WIFI_SSID)","password":"$(WIFI_PASSWORD)"}],"ap_password":"","mint_url":"$(MINT_URL)","price_per_step":21,"step_size_ms":60000,"client_enabled":false,"nostr_geohash":"u281w0dfz","nostr_relays":["wss://relay.damus.io","wss://nos.lol"],"nostr_publish_interval_s":21600}' > "$$TMPDIR/config.json" && \
	echo "  Generating SPIFFS image..." && \
	python3 $(SPIFFSGEN) --page-size 256 --obj-name-len 32 --use-magic --use-magic-len $(SPIFFS_SIZE) "$$TMPDIR" "$$TMPDIR/spiffs.bin" && \
	echo "  Writing to flash..." && \
	python3 -m esptool --port $(PORT_$(1)) --baud $(BAUD) write_flash $(SPIFFS_OFFSET) "$$TMPDIR/spiffs.bin" && \
	rm -rf "$$TMPDIR" && \
	echo "Config written."
	@python3 -m esptool --port $(PORT_$(1)) run 2>/dev/null || true
endef

define write_board_config_ap_only
	$(call require_lock_$(1))
	@echo "=== Writing AP-only SPIFFS config to Board $(1) ($(PORT_$(1))) ==="
	@TMPDIR=$$(mktemp -d) && \
	echo '{"nsec":"$(NSEC_$(1))","wifi_networks":[],"ap_password":"","mint_url":"$(MINT_URL)","price_per_step":21,"step_size_ms":60000,"client_enabled":false,"nostr_geohash":"u281w0dfz","nostr_relays":["wss://relay.damus.io","wss://nos.lol"],"nostr_publish_interval_s":21600}' > "$$TMPDIR/config.json" && \
	echo "  Generating SPIFFS image..." && \
	python3 $(SPIFFSGEN) --page-size 256 --obj-name-len 32 --use-magic --use-magic-len $(SPIFFS_SIZE) "$$TMPDIR" "$$TMPDIR/spiffs.bin" && \
	echo "  Writing to flash..." && \
	python3 -m esptool --port $(PORT_$(1)) --baud $(BAUD) write_flash $(SPIFFS_OFFSET) "$$TMPDIR/spiffs.bin" && \
	rm -rf "$$TMPDIR" && \
	echo "AP-only config written."
	@python3 -m esptool --port $(PORT_$(1)) run 2>/dev/null || true
endef

write-config-a:
	$(call write_board_config,A)

write-config-b:
	$(call write_board_config,B)

write-config-ap-only-a:
	$(call write_board_config_ap_only,A)

write-config-ap-only-b:
	$(call write_board_config_ap_only,B)

write-mining-config:
	$(call _require_board_lock)
	@HOST_IP=$$(ip -4 addr show | grep -oP "inet \\K[\\d.]+" | while read ip; do \
		IFS='.' read -r a b c d <<< "$$ip"; \
		IFS='.' read -r ta tb tc td <<< "$(TOLLGATE_IP)"; \
		if [ "$$a" = "$$ta" ] && [ "$$b" = "$$tb" ] && [ "$$c" = "$$tc" ]; then echo "$$ip"; break; fi; \
	done); \
	if [ -z "$$HOST_IP" ]; then echo "ERROR: Host IP not found on AP network (TOLLGATE_IP=$(TOLLGATE_IP))"; exit 1; fi; \
	echo "=== Writing mining config (stratum_host=$$HOST_IP) to $(PORT) ==="; \
	TMPDIR=$$(mktemp -d) && \
	echo '{"nsec":"$(NSEC_A)","wifi_networks":[{"ssid":"$(WIFI_SSID)","password":"$(WIFI_PASSWORD)"}],"ap_password":"","mint_url":"$(MINT_URL)","accepted_mints":["$(MINT_URL)"],"price_per_step":21,"step_size_ms":60000,"display_enabled":false,"cvm_enabled":false,"sync_enabled":false,"wifistr_enabled":false,"local_relay_enabled":false,"mint_health_enabled":true,"mining":{"enabled":true,"payout_mode":"auto","stratum_host":"'"$$HOST_IP"'","stratum_port":34255,"stratum_user":"tollgate_test","stratum_pass":"x","mining_port":3334}}' > "$$TMPDIR/config.json" && \
	echo "  Generating SPIFFS image..." && \
	python3 $(SPIFFSGEN) --page-size 256 --obj-name-len 32 --use-magic --use-magic-len $(SPIFFS_SIZE) "$$TMPDIR" "$$TMPDIR/spiffs.bin" && \
	echo "  Writing to flash..." && \
	python3 -m esptool --port $(PORT) --baud $(BAUD) write_flash $(SPIFFS_OFFSET) "$$TMPDIR/spiffs.bin" && \
	rm -rf "$$TMPDIR" && \
	echo "Mining config written (stratum_host=$$HOST_IP)."

STRATUM_HOST ?=
STRATUM_PORT ?= 34255
UPPER_BOARD := $(shell echo $(BOARD) | tr 'a-z' 'A-Z')
NSEC_FOR_BOARD := $(NSEC_$(UPPER_BOARD))
PORT_FOR_BOARD := $(PORT_$(UPPER_BOARD))

write-mining-config-vps:
	$(call _require_board_lock)
	@if [ -z "$(STRATUM_HOST)" ]; then echo "ERROR: STRATUM_HOST is required (e.g., make write-mining-config-vps STRATUM_HOST=66.92.204.38)"; exit 1; fi
	@echo "=== Writing VPS mining config (board=$(BOARD), stratum=$(STRATUM_HOST):$(STRATUM_PORT)) to $(PORT_FOR_BOARD) ==="
	@TMPDIR=$$(mktemp -d) && \
	echo '{"nsec":"$(NSEC_FOR_BOARD)","wifi_networks":[{"ssid":"$(WIFI_SSID)","password":"$(WIFI_PASSWORD)"}],"ap_password":"","mint_url":"http://$(STRATUM_HOST):3338","accepted_mints":["http://$(STRATUM_HOST):3338","$(MINT_URL)"],"price_per_step":21,"step_size_ms":60000,"display_enabled":false,"cvm_enabled":false,"sync_enabled":false,"wifistr_enabled":false,"local_relay_enabled":false,"mint_health_enabled":true,"mining":{"enabled":true,"payout_mode":"auto","stratum_host":"$(STRATUM_HOST)","stratum_port":$(STRATUM_PORT),"stratum_user":"tollgate_test","stratum_pass":"x","mining_port":3334,"faucet_url":"http://$(STRATUM_HOST):8083/mint/tokens","faucet_poll_interval_s":120}}' > "$$TMPDIR/config.json" && \
	echo "  Generating SPIFFS image..." && \
	python3 $(SPIFFSGEN) --page-size 256 --obj-name-len 32 --use-magic --use-magic-len $(SPIFFS_SIZE) "$$TMPDIR" "$$TMPDIR/spiffs.bin" && \
	echo "  Writing to flash..." && \
	python3 -m esptool --port $(PORT_FOR_BOARD) --baud $(BAUD) write_flash $(SPIFFS_OFFSET) "$$TMPDIR/spiffs.bin" && \
	rm -rf "$$TMPDIR" && \
	echo "VPS mining config written (stratum_host=$(STRATUM_HOST):$(STRATUM_PORT))."

B2B_MINT_URL := http://66.92.204.38:3338
B2B_FAUCET_URL := http://66.92.204.38:8083/mint/tokens
B2B_STRATUM_HOST := 66.92.204.38

define write_b2b_config_a
	$(call require_lock_$(1))
	@echo "=== Writing B2B upstream config to Board $(1) ($(PORT_$(1))) ==="
	@TMPDIR=$$(mktemp -d) && \
	echo '{"nsec":"$(NSEC_$(1))","wifi_networks":[{"ssid":"$(WIFI_SSID)","password":"$(WIFI_PASSWORD)"}],"ap_password":"","mint_url":"$(B2B_MINT_URL)","accepted_mints":["$(B2B_MINT_URL)"],"price_per_step":21,"step_size_ms":60000,"client_enabled":false,"display_enabled":false,"cvm_enabled":false,"sync_enabled":false,"wifistr_enabled":false,"local_relay_enabled":false,"mint_health_enabled":true,"mining":{"enabled":true,"payout_mode":"auto","stratum_host":"$(B2B_STRATUM_HOST)","stratum_port":34255,"stratum_user":"tollgate_test","stratum_pass":"x","mining_port":3334,"faucet_url":"$(B2B_FAUCET_URL)","faucet_poll_interval_s":120}}' > "$$TMPDIR/config.json" && \
	echo "  Generating SPIFFS image..." && \
	python3 $(SPIFFSGEN) --page-size 256 --obj-name-len 32 --use-magic --use-magic-len $(SPIFFS_SIZE) "$$TMPDIR" "$$TMPDIR/spiffs.bin" && \
	echo "  Writing to flash..." && \
	python3 -m esptool --port $(PORT_$(1)) --baud $(BAUD) write_flash $(SPIFFS_OFFSET) "$$TMPDIR/spiffs.bin" && \
	rm -rf "$$TMPDIR" && \
	echo "B2B upstream config written to Board $(1)."
	@python3 -m esptool --port $(PORT_$(1)) run 2>/dev/null || true
endef

define write_b2b_config_b
	$(call require_lock_$(1))
	@echo "=== Writing B2B downstream config to Board $(1) ($(PORT_$(1))) ==="
	@TMPDIR=$$(mktemp -d) && \
	echo '{"nsec":"$(NSEC_$(1))","wifi_networks":[{"ssid":"TollGate-B96D80","password":""},{"ssid":"$(WIFI_SSID)","password":"$(WIFI_PASSWORD)"}],"ap_password":"","mint_url":"$(B2B_MINT_URL)","accepted_mints":["$(B2B_MINT_URL)"],"price_per_step":21,"step_size_ms":60000,"client_enabled":true,"client_steps_to_buy":1,"client_renewal_threshold_pct":20,"client_retry_interval_ms":30000,"display_enabled":false,"cvm_enabled":false,"sync_enabled":false,"wifistr_enabled":false,"local_relay_enabled":false,"mint_health_enabled":true,"mining":{"enabled":true,"payout_mode":"auto","stratum_host":"$(B2B_STRATUM_HOST)","stratum_port":34255,"stratum_user":"tollgate_test","stratum_pass":"x","mining_port":3334,"faucet_url":"$(B2B_FAUCET_URL)","faucet_poll_interval_s":120}}' > "$$TMPDIR/config.json" && \
	echo "  Generating SPIFFS image..." && \
	python3 $(SPIFFSGEN) --page-size 256 --obj-name-len 32 --use-magic --use-magic-len $(SPIFFS_SIZE) "$$TMPDIR" "$$TMPDIR/spiffs.bin" && \
	echo "  Writing to flash..." && \
	python3 -m esptool --port $(PORT_$(1)) --baud $(BAUD) write_flash $(SPIFFS_OFFSET) "$$TMPDIR/spiffs.bin" && \
	rm -rf "$$TMPDIR" && \
	echo "B2B downstream config written to Board $(1)."
	@python3 -m esptool --port $(PORT_$(1)) run 2>/dev/null || true
endef

write-b2b-config-a:
	$(call write_b2b_config_a,A)

write-b2b-config-b:
	$(call write_b2b_config_b,B)

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
	$(call _require_board_lock)
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
	$(call _require_board_lock)
	@echo "=== Erasing NVS on $(PORT) ==="
	. $(IDF_PATH)/export.sh && \
		partition_offset=$$(idf.py partition-table 2>/dev/null | grep nvs | awk '{print $$2}'); \
		python3 -m esptool --port $(PORT) erase_region $$partition_offset 0x6000

reset:
	$(call _require_board_lock)
	@echo "=== Resetting device on $(PORT) ==="
	python3 -m esptool --port $(PORT) run 2>/dev/null || true

serial-log:
	$(call _require_board_lock)
	@echo "=== Capturing serial output from $(PORT) ==="
	python3 -c "import serial; s=serial.Serial('$(PORT)',115200,timeout=1); \
		[print(s.readline().decode(errors='replace'),end='') for _ in iter(lambda: s.readline(), b'')]"

bootstrap-config:
	@echo "=== Bootstrapping config.json ==="
	@echo '{"wifi_networks":[{"ssid":"$(WIFI_SSID)","password":"$(WIFI_PASSWORD)"}],"ap_ssid":"$(AP_SSID)","ap_password":"$(AP_PASSWORD)","mint_url":"$(MINT_URL)","lnurl_url":"$(LNURL_URL)","price_per_step":$(PRICE_PER_STEP),"step_size_ms":$(STEP_SIZE)}' > main/config.json
	@echo "Config written to main/config.json"

# ──────────────────────────────────────────────
# Per-Board Hardware Locks
# ──────────────────────────────────────────────

lock-a: ## Acquire Board A lock (set PHASE="description")
	$(call _acquire_lock,board-a)

lock-b: ## Acquire Board B lock (set PHASE="description")
	$(call _acquire_lock,board-b)

unlock-a: ## Release Board A lock
	@if [ ! -f "$(HARDWARE_LOCK_DIR)/board-a.lock" ]; then \
		echo "$(YELLOW)Board A not locked.$(RESET)"; exit 0; \
	fi; \
	rm -f $(HARDWARE_LOCK_DIR)/board-a.lock; \
	echo "$(GREEN)Board A lock released.$(RESET)"

unlock-b: ## Release Board B lock
	@if [ ! -f "$(HARDWARE_LOCK_DIR)/board-b.lock" ]; then \
		echo "$(YELLOW)Board B not locked.$(RESET)"; exit 0; \
	fi; \
	rm -f $(HARDWARE_LOCK_DIR)/board-b.lock; \
	echo "$(GREEN)Board B lock released.$(RESET)"

force-unlock-a: ## Force-release Board A lock
	@if [ ! -f "$(HARDWARE_LOCK_DIR)/board-a.lock" ]; then \
		echo "$(YELLOW)Board A not locked.$(RESET)"; exit 0; \
	fi; \
	echo "$(RED)$(BOLD)WARNING: Force-releasing Board A!$(RESET)"; \
	cat $(HARDWARE_LOCK_DIR)/board-a.lock | sed 's/^/  /'; \
	rm -f $(HARDWARE_LOCK_DIR)/board-a.lock; \
	echo "$(GREEN)Board A force-released.$(RESET)"

force-unlock-b: ## Force-release Board B lock
	@if [ ! -f "$(HARDWARE_LOCK_DIR)/board-b.lock" ]; then \
		echo "$(YELLOW)Board B not locked.$(RESET)"; exit 0; \
	fi; \
	echo "$(RED)$(BOLD)WARNING: Force-releasing Board B!$(RESET)"; \
	cat $(HARDWARE_LOCK_DIR)/board-b.lock | sed 's/^/  /'; \
	rm -f $(HARDWARE_LOCK_DIR)/board-b.lock; \
	echo "$(GREEN)Board B force-released.$(RESET)"

lock-status: ## Show all board lock statuses
	@for board in a b; do \
		if [ -f "$(HARDWARE_LOCK_DIR)/board-$$board.lock" ]; then \
			echo "$(YELLOW)Board $$board: LOCKED$(RESET)"; \
			cat $(HARDWARE_LOCK_DIR)/board-$$board.lock | sed 's/^/  /'; \
		else \
			echo "Board $$board: $(GREEN)available$(RESET)"; \
		fi; \
	done
