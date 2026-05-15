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

.PHONY: help setup detect-ports detect-chip detect-all
.PHONY: flash flash-a flash-b monitor monitor-a monitor-b
.PHONY: test smoke test-api test-portal test-network test-full
.PHONY: tokens test-payment
.PHONY: clean erase-nvs reset serial-log
.PHONY: bootstrap-config

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
	@echo "Test (Phase 1):"
	@echo "  test              Run all Phase 1 tests"
	@echo "  smoke             Quick 30s smoke test"
	@echo "  test-api          curl API endpoint tests"
	@echo "  test-portal       Playwright captive portal tests"
	@echo "  test-network      DNS/NAT connectivity tests"
	@echo "  test-full         All 14 Phase 1 tests"
	@echo ""
	@echo "Test (Phase 2):"
	@echo "  tokens            Mint test Cashu tokens (AMOUNT=21)"
	@echo "  test-payment      Payment flow tests"
	@echo ""
	@echo "Utilities:"
	@echo "  setup             One-time: install esptool, deps"
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

flash-a: PORT=$(PORT_A)
flash-a: flash

flash-b: PORT=$(PORT_B)
flash-b: flash

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
# Test Infrastructure
# ──────────────────────────────────────────────

test: test-api test-network
	@echo "=== All tests passed ==="

smoke:
	@echo "=== Running smoke test (30s) ==="
	$(NODE) tests/smoke.mjs $(PORT)

test-api:
	@echo "=== Running API tests ==="
	$(NODE) tests/api.mjs

test-portal:
	@echo "=== Running Playwright portal tests ==="
	npx playwright test tests/captive-portal.spec.mjs

test-network:
	@echo "=== Running network tests ==="
	$(NODE) tests/network.mjs

test-full: test-api test-portal test-network
	@echo "=== Full test suite passed ==="

# ──────────────────────────────────────────────
# Phase 2: Payment Testing
# ──────────────────────────────────────────────

tokens:
	@echo "=== Minting test tokens from $(TEST_MINT) ==="
	@AMOUNT=$${AMOUNT:-21}; \
	cd scripts/mint-token && go run main.go -mint https://$(TEST_MINT) -amount $$AMOUNT

test-payment:
	@echo "=== Running payment tests ==="
	$(NODE) tests/payment.mjs

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
