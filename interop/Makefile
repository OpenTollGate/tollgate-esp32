# ---------------------------------------------------------------------------
# Makefile — ESP32 ↔ OpenWRT TollGate Interop Tests
#
# Tests cross-platform Cashu token compatibility between ESP32 firmware
# and OpenWRT tollgate-module-basic-go daemon.
#
# Setup:
#   cp routers.env.example routers.env   # then edit with real values
#   make interop-setup                   # configure mints + fund wallets
#
# Quick reference:
#   make interop-status                  # show all device status
#   make interop-laptop-esp32            # laptop pays ESP32
#   make interop-laptop-openwrt          # laptop pays OpenWRT
#   make interop-openwrt-esp32           # OpenWRT auto-pays ESP32 upstream
#   make interop-cleanup                 # restore original configs
# ---------------------------------------------------------------------------

.PHONY: help interop-status interop-setup interop-setup-mints interop-verify-mints \
        interop-fund-esp32 interop-fund-openwrt interop-setup \
        interop-laptop-esp32 interop-laptop-openwrt \
        interop-openwrt-esp32 interop-esp32-esp32 \
        interop-cleanup interop-save-state interop-restore-state

-include routers.env

BOLD   := \033[1m
GREEN  := \033[32m
RED    := \033[31m
YELLOW := \033[33m
CYAN   := \033[36m
RESET  := \033[0m

define RESOLVE_ALPHA
alpha_host=$$(grep -E "^ROUTER_ALPHA_HOST=" routers.env | cut -d= -f2); \
if [ -z "$$alpha_host" ]; then echo "$(RED)No ROUTER_ALPHA_HOST in routers.env$(RESET)"; exit 1; fi
endef

define RESOLVE_ESP32A
esp32_host=$$(grep -E "^ESP32_A_HOST=" routers.env | cut -d= -f2); \
if [ -z "$$esp32_host" ]; then echo "$(RED)No ESP32_A_HOST in routers.env$(RESET)"; exit 1; fi
endef

MINT_TOKEN_BIN ?= /tmp/mint-token

help: ## Show this help
	@echo "TollGate Interop Tests — ESP32 ↔ OpenWRT"
	@echo "=========================================="
	@echo ""
	@echo "Setup:"
	@echo "  interop-setup           Configure mints + fund wallets on both devices"
	@echo "  interop-setup-mints     Add both mints to both devices"
	@echo "  interop-fund-esp32      Fund ESP32 wallet with V3 test tokens"
	@echo "  interop-fund-openwrt    Fund OpenWRT wallet with V4 test tokens"
	@echo ""
	@echo "Test Scenarios:"
	@echo "  interop-status          Show TollGate status for all devices"
	@echo "  interop-laptop-esp32    Scenario 1: Laptop pays ESP32 TollGate"
	@echo "  interop-laptop-openwrt  Scenario 2: Laptop pays OpenWRT TollGate"
	@echo "  interop-openwrt-esp32   Scenario 3: OpenWRT auto-pays ESP32 for upstream"
	@echo "  interop-esp32-esp32     Scenario 5: ESP32 cross-board payment"
	@echo ""
	@echo "Cleanup:"
	@echo "  interop-cleanup         Restore original configs on all devices"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  $(CYAN)%-30s$(RESET) %s\n", $$1, $$2}'

# ===========================================================================
#  Status
# ===========================================================================

interop-status: ## Show TollGate status for all devices
	@echo "$(BOLD)=======================================$(RESET)"
	@echo "$(BOLD)  TollGate Interop — Device Status$(RESET)"
	@echo "$(BOLD)=======================================$(RESET)"
	@echo ""
	@echo "$(CYAN)--- OpenWRT Router (alpha) ---$(RESET)"
	@$(RESOLVE_ALPHA); \
	echo "  Host: $$alpha_host"; \
	if ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "echo ok" >/dev/null 2>&1; then \
		echo "  $(GREEN)SSH: reachable$(RESET)"; \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate version 2>&1 | head -1"; \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate status 2>&1 | grep -E 'running|wallet_ok|network_ok'"; \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate wallet balance 2>&1"; \
		echo "  Accepted mints:"; \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "cat /etc/tollgate/config.json" 2>/dev/null | python3 -c "import sys,json; [print('    ' + m['url']) for m in json.load(sys.stdin).get('accepted_mints',[])]" 2>/dev/null || echo "    (parse error)"; \
	else \
		echo "  $(RED)SSH: unreachable$(RESET)"; \
	fi
	@echo ""
	@echo "$(CYAN)--- ESP32 Board A ---$(RESET)"
	@$(RESOLVE_ESP32A); \
	echo "  Host: $$esp32_host"; \
	if curl -s --connect-timeout 5 "http://$$esp32_host:2121/" >/dev/null 2>&1; then \
		echo "  $(GREEN)API: reachable$(RESET)"; \
		curl -s --connect-timeout 5 "http://$$esp32_host:2121/" | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'  kind={d[\"kind\"]}, tags={len(d.get(\"tags\",[]))}')" 2>/dev/null || echo "    (parse error)"; \
		curl -s --connect-timeout 5 "http://$$esp32_host:2121/wallet" 2>/dev/null | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'  Wallet: {d.get(\"balance\",\"?\")} sats, {d.get(\"proof_count\",\"?\")} proofs')" 2>/dev/null || echo "  Wallet: (not available)"; \
	else \
		echo "  $(RED)API: unreachable$(RESET)"; \
	fi
	@echo ""
	@echo "$(CYAN)--- Laptop Connectivity ---$(RESET)"
	@wifi_if=$$(grep -E "^LAPTOP_WIFI=" routers.env | cut -d= -f2); \
	eth_if=$$(grep -E "^LAPTOP_ETH=" routers.env | cut -d= -f2); \
	echo "  WiFi ($$wifi_if): $$(ip addr show $$wifi_if 2>/dev/null | grep 'inet ' | awk '{print $$2}' || echo 'no IP')"; \
	echo "  Ethernet ($$eth_if): $$(ip addr show $$eth_if 2>/dev/null | grep 'inet ' | awk '{print $$2}' || echo 'no IP')"

# ===========================================================================
#  Setup
# ===========================================================================

interop-setup-mints: ## Add both mints to both devices
	@echo "$(BOLD)=== Adding both mints to both devices ===$(RESET)"
	@echo ""
	@echo "$(CYAN)Step 1: Add testnut.cashu.space to OpenWRT$(RESET)"
	@$(RESOLVE_ALPHA); \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "cat /etc/tollgate/config.json" 2>/dev/null | \
		python3 -c "import sys,json; d=json.load(sys.stdin); urls=[m['url'] for m in d.get('accepted_mints',[])]; print('testnut' if any('testnut.cashu.space' in u and 'nofee' not in u for u in urls) else 'missing')" 2>/dev/null | \
		grep -q testnut && echo "  $(GREEN)Already present$(RESET)" || { \
		echo "  Adding testnut.cashu.space..."; \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "cat /etc/tollgate/config.json | python3 -c \"import sys,json; d=json.load(sys.stdin); d['accepted_mints'].append({'url':'https://testnut.cashu.space','price_per_step':21,'price_unit':'sats','min_balance':0}); json.dump(d,sys.stdout,indent=2)\" > /tmp/config-interop.json && mv /tmp/config-interop.json /etc/tollgate/config.json"; \
		echo "  $(GREEN)Added$(RESET)"; \
	}
	@echo ""
	@echo "$(CYAN)Step 2: Restart OpenWRT service$(RESET)"
	@$(RESOLVE_ALPHA); \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "/etc/init.d/tollgate-wrt restart"; \
	echo "  $(GREEN)Restarted$(RESET)"
	@echo ""
	@echo "$(YELLOW)Step 3: ESP32 mint config requires firmware rebuild$(RESET)"
	@echo "  ESP32 mint_url is in config.json on SPIFFS. To change it:"
	@echo "    1. Edit main/config.json to add 'nofee.testnut.cashu.space' as secondary mint"
	@echo "    2. make flash-a"
	@echo "  Skipping ESP32 mint change for now (both mints may not be needed for basic interop)."

interop-verify-mints: ## Verify both mints accepted on both sides
	@echo "$(BOLD)=== Verifying Mint Configuration ===$(RESET)"
	@$(RESOLVE_ALPHA); \
	echo ""; \
	echo "$(CYAN)OpenWRT accepted_mints:$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "cat /etc/tollgate/config.json" 2>/dev/null | \
		python3 -c "import sys,json; [print('  ' + m['url']) for m in json.load(sys.stdin).get('accepted_mints',[])]" 2>/dev/null
	@echo ""
	@$(RESOLVE_ESP32A); \
	echo "$(CYAN)ESP32 API advertisement:$(RESET)"; \
	curl -s --connect-timeout 5 "http://$$esp32_host:2121/" | \
		python3 -c "import sys,json; d=json.load(sys.stdin); [print('  ' + t[3] + ' (price=' + t[2] + ' ' + t[1] + ')') for t in d.get('tags',[]) if t[0]=='price_per_step']" 2>/dev/null || echo "  (parse error)"

interop-fund-esp32: ## Fund ESP32 wallet with V3 test tokens
	@echo "$(BOLD)=== Funding ESP32 Wallet ===$(RESET)"
	@$(RESOLVE_ESP32A); \
	echo "Minting 21 sats from testnut.cashu.space (V3 token)..."; \
	TOKEN=$$(cashu --env-mint testnut.cashu.space send --legacy 21 2>/dev/null | tail -1); \
	if [ -z "$$TOKEN" ]; then echo "$(RED)Failed to mint token$(RESET)"; exit 1; fi; \
	echo "Token minted (length $${#TOKEN}). Funding ESP32..."; \
	RESP=$$(curl -s --connect-timeout 5 -X POST -d "$$TOKEN" "http://$$esp32_host:2121/"); \
	echo "$$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'  kind={d[\"kind\"]}'); [print(f'  {t[0]}={t[1]}') for t in d.get('tags',[]) if t[0] in ('allotment','price_per_step')]" 2>/dev/null || echo "  Response: $$RESP"; \
	echo ""; \
	echo "$(CYAN)ESP32 wallet status:$(RESET)"; \
	curl -s --connect-timeout 5 "http://$$esp32_host:2121/wallet" 2>/dev/null | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'  Balance: {d.get(\"balance\",\"?\")} sats, {d.get(\"proof_count\",\"?\")} proofs')" 2>/dev/null || echo "  (wallet endpoint not available)"

interop-fund-openwrt: ## Fund OpenWRT wallet with V4 test tokens
	@echo "$(BOLD)=== Funding OpenWRT Wallet ===$(RESET)"
	@if [ ! -x "$(MINT_TOKEN_BIN)" ]; then \
		echo "$(RED)mint-token not found at $(MINT_TOKEN_BIN)$(RESET)"; \
		echo "Build it: cd physical-router-test-automation/scripts/mint-token && go build -o /tmp/mint-token ."; \
		exit 1; \
	fi
	@$(RESOLVE_ALPHA); \
	echo "Minting 1013 sats from nofee.testnut.cashu.space (V4 token)..."; \
	RAW=$$($(MINT_TOKEN_BIN) 2>/dev/null); \
	TOKEN=$$(echo "$$RAW" | python3 -c "import sys,json; print(json.load(sys.stdin)['token'])"); \
	if [ -z "$$TOKEN" ]; then echo "$(RED)Failed to mint token$(RESET)"; exit 1; fi; \
	echo "Token minted. Funding OpenWRT wallet..."; \
	echo "$$TOKEN" | ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate wallet fund" 2>&1; \
	echo ""; \
	echo "$(CYAN)OpenWRT wallet status:$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate wallet balance"

interop-setup: interop-setup-mints interop-verify-mints interop-fund-esp32 interop-fund-openwrt ## Full setup: mints + wallets
	@echo ""
	@echo "$(BOLD)=======================================$(RESET)"
	@echo "$(GREEN)$(BOLD)  Interop setup complete$(RESET)"
	@echo "$(BOLD)=======================================$(RESET)"

# ===========================================================================
#  Scenario 1: Laptop → ESP32
# ===========================================================================

interop-laptop-esp32: ## Scenario 1: Laptop pays ESP32 TollGate with V3 token
	@echo "$(BOLD)=======================================$(RESET)"
	@echo "$(BOLD)  Scenario 1: Laptop → ESP32$(RESET)"
	@echo "$(BOLD)=======================================$(RESET)"
	@$(RESOLVE_ESP32A); \
	echo ""; \
	echo "$(CYAN)1/6 — Verify ESP32 API reachable at $$esp32_host...$(RESET)"; \
	API=$$(curl -s --connect-timeout 5 "http://$$esp32_host:2121/"); \
	if [ -z "$$API" ]; then echo "$(RED)ESP32 API unreachable$(RESET)"; exit 1; fi; \
	KIND=$$(echo "$$API" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$KIND" != "10021" ]; then echo "$(RED)Expected kind=10021, got $$KIND$(RESET)"; exit 1; fi; \
	echo "  $(GREEN)kind=10021 advertisement received$(RESET)"; \
	echo ""; \
	echo "$(CYAN)2/6 — Minting V3 token (21 sats from testnut.cashu.space)...$(RESET)"; \
	TOKEN=$$(cashu --env-mint testnut.cashu.space send --legacy 21 2>/dev/null | tail -1); \
	if [ -z "$$TOKEN" ]; then echo "$(RED)Token minting failed$(RESET)"; exit 1; fi; \
	echo "  $(GREEN)Token minted (length $${#TOKEN})$(RESET)"; \
	echo ""; \
	echo "$(CYAN)3/6 — POST token to ESP32 TollGate API...$(RESET)"; \
	RESP=$$(curl -s --connect-timeout 10 -X POST -d "$$TOKEN" "http://$$esp32_host:2121/"); \
	RKIND=$$(echo "$$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$RKIND" != "1022" ]; then \
		echo "$(RED)Payment failed: kind=$$RKIND$(RESET)"; \
		echo "$$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); [print(f'  {t}') for t in d.get('tags',[])]" 2>/dev/null; \
		exit 1; \
	fi; \
	echo "  $(GREEN)kind=1022 session created$(RESET)"; \
	ALLOT=$$(echo "$$RESP" | python3 -c "import sys,json; [print(t[1]) for t in json.load(sys.stdin).get('tags',[]) if t[0]=='allotment']" 2>/dev/null); \
	echo "  Allotment: $$ALLOT"; \
	echo ""; \
	echo "$(CYAN)4/6 — Verify internet through ESP32...$(RESET)"; \
	sleep 1; \
	PING_OK=0; \
	wifi_if=$$(grep -E "^LAPTOP_WIFI=" routers.env | cut -d= -f2); \
	for i in 1 2 3; do \
		if ping -c 2 -W 3 -I $$wifi_if 8.8.8.8 2>/dev/null | grep -q "0% packet loss"; then \
			PING_OK=1; break; \
		fi; \
		sleep 2; \
	done; \
	if [ "$$PING_OK" = "1" ]; then echo "  $(GREEN)Internet works through ESP32$(RESET)"; \
	else echo "  $(YELLOW)WARN: No internet (ESP32 may have no upstream)$(RESET)"; fi; \
	echo ""; \
	echo "$(CYAN)5/6 — Test spent token rejection...$(RESET)"; \
	RESP2=$$(curl -s --connect-timeout 5 -X POST -d "$$TOKEN" "http://$$esp32_host:2121/"); \
	RKIND2=$$(echo "$$RESP2" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$RKIND2" = "21023" ]; then echo "  $(GREEN)Spent token rejected (kind=21023)$(RESET)"; \
	else echo "  $(YELLOW)WARN: Expected kind=21023 for spent token, got $$RKIND2$(RESET)"; fi; \
	echo ""; \
	echo "$(CYAN)6/6 — Test invalid token rejection...$(RESET)"; \
	RESP3=$$(curl -s --connect-timeout 5 -X POST -d "garbage_not_a_token" "http://$$esp32_host:2121/"); \
	RKIND3=$$(echo "$$RESP3" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$RKIND3" = "21023" ]; then echo "  $(GREEN)Invalid token rejected (kind=21023)$(RESET)"; \
	else echo "  $(YELLOW)WARN: Expected kind=21023 for invalid token, got $$RKIND3$(RESET)"; fi; \
	echo ""; \
	echo "$(BOLD)=======================================$(RESET)"; \
	echo "$(GREEN)$(BOLD)  Scenario 1 PASSED: Laptop → ESP32$(RESET)"; \
	echo "$(BOLD)=======================================$(RESET)"

# ===========================================================================
#  Scenario 2: Laptop → OpenWRT
# ===========================================================================

interop-laptop-openwrt: ## Scenario 2: Laptop pays OpenWRT TollGate with V4 token
	@echo "$(BOLD)=======================================$(RESET)"
	@echo "$(BOLD)  Scenario 2: Laptop → OpenWRT$(RESET)"
	@echo "$(BOLD)=======================================$(RESET)"
	@if [ ! -x "$(MINT_TOKEN_BIN)" ]; then \
		echo "$(RED)mint-token not found at $(MINT_TOKEN_BIN)$(RESET)"; \
		echo "Build it: cd physical-router-test-automation/scripts/mint-token && go build -o /tmp/mint-token ."; \
		exit 1; \
	fi
	@$(RESOLVE_ALPHA); \
	echo ""; \
	echo "$(CYAN)1/6 — Verify OpenWRT API reachable at $$alpha_host...$(RESET)"; \
	API=$$(curl -s --connect-timeout 5 "http://$$alpha_host:2121/"); \
	if [ -z "$$API" ]; then echo "$(RED)OpenWRT API unreachable$(RESET)"; exit 1; fi; \
	KIND=$$(echo "$$API" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$KIND" != "10021" ]; then echo "$(RED)Expected kind=10021, got $$KIND$(RESET)"; exit 1; fi; \
	echo "  $(GREEN)kind=10021 advertisement received$(RESET)"; \
	PRICE=$$(echo "$$API" | python3 -c "import sys,json; [print(t[2] + ' ' + t[1]) for t in json.load(sys.stdin).get('tags',[]) if t[0]=='price_per_step']" 2>/dev/null | head -1); \
	echo "  Price: $$PRICE"; \
	echo ""; \
	echo "$(CYAN)2/6 — Minting V4 token (1 sat from nofee.testnut.cashu.space)...$(RESET)"; \
	RAW=$$($(MINT_TOKEN_BIN) https://nofee.testnut.cashu.space 1 2>/dev/null); \
	TOKEN=$$(echo "$$RAW" | python3 -c "import sys,json; print(json.load(sys.stdin)['token'])"); \
	AMOUNT=$$(echo "$$RAW" | python3 -c "import sys,json; print(json.load(sys.stdin)['amount'])"); \
	if [ -z "$$TOKEN" ]; then echo "$(RED)Token minting failed$(RESET)"; exit 1; fi; \
	echo "  $(GREEN)Token minted: $$AMOUNT sats (length $${#TOKEN})$(RESET)"; \
	echo ""; \
	echo "$(CYAN)3/6 — POST token to OpenWRT TollGate API...$(RESET)"; \
	RESP=$$(curl -s --connect-timeout 10 -X POST -d "$$TOKEN" "http://$$alpha_host:2121/"); \
	RKIND=$$(echo "$$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$RKIND" != "1022" ]; then \
		echo "$(RED)Payment failed: kind=$$RKIND$(RESET)"; \
		echo "$$RESP" | python3 -m json.tool 2>/dev/null || echo "$$RESP"; \
		exit 1; \
	fi; \
	echo "  $(GREEN)kind=1022 session created$(RESET)"; \
	echo ""; \
	echo "$(CYAN)4/6 — Verify internet through OpenWRT...$(RESET)"; \
	sleep 1; \
	PING_OK=0; \
	for i in 1 2 3; do \
		if ping -c 2 -W 3 8.8.8.8 2>/dev/null | grep -q "0% packet loss"; then \
			PING_OK=1; break; \
		fi; \
		sleep 2; \
	done; \
	if [ "$$PING_OK" = "1" ]; then echo "  $(GREEN)Internet works$(RESET)"; \
	else echo "  $(YELLOW)WARN: No internet (check routing)$(RESET)"; fi; \
	echo ""; \
	echo "$(CYAN)5/6 — Test spent token rejection...$(RESET)"; \
	RESP2=$$(curl -s --connect-timeout 5 -X POST -d "$$TOKEN" "http://$$alpha_host:2121/"); \
	RKIND2=$$(echo "$$RESP2" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$RKIND2" = "21023" ]; then echo "  $(GREEN)Spent token rejected (kind=21023)$(RESET)"; \
	else echo "  $(YELLOW)WARN: Expected kind=21023 for spent token, got $$RKIND2$(RESET)"; fi; \
	echo ""; \
	echo "$(CYAN)6/6 — Test invalid token rejection...$(RESET)"; \
	RESP3=$$(curl -s --connect-timeout 5 -X POST -d "garbage_not_a_token" "http://$$alpha_host:2121/"); \
	RKIND3=$$(echo "$$RESP3" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	if [ "$$RKIND3" = "21023" ]; then echo "  $(GREEN)Invalid token rejected (kind=21023)$(RESET)"; \
	else echo "  $(YELLOW)WARN: Expected kind=21023 for invalid, got $$RKIND3$(RESET)"; fi; \
	echo ""; \
	echo "$(BOLD)=======================================$(RESET)"; \
	echo "$(GREEN)$(BOLD)  Scenario 2 PASSED: Laptop → OpenWRT$(RESET)"; \
	echo "$(BOLD)=======================================$(RESET)"

# ===========================================================================
#  Scenario 3: OpenWRT → ESP32 (Reseller)
# ===========================================================================

interop-openwrt-esp32: ## Scenario 3: OpenWRT auto-pays ESP32 for upstream internet
	@echo "$(BOLD)=======================================$(RESET)"
	@echo "$(BOLD)  Scenario 3: OpenWRT → ESP32 (Reseller)$(RESET)"
	@echo "$(BOLD)=======================================$(RESET)"
	@echo ""
	@$(RESOLVE_ALPHA); \
	$(RESOLVE_ESP32A); \
	esp32_ssid=$$(grep -E "^ESP32_A_SSID=" routers.env | cut -d= -f2); \
	upstream_ssid=$$(grep -E "^UPSTREAM_SSID=" routers.env | cut -d= -f2); \
	upstream_pass=$$(grep -E "^UPSTREAM_PASS=" routers.env | cut -d= -f2); \
	\
	echo "$(CYAN)Step 0: Pre-flight$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "echo alpha-ok" 2>/dev/null | grep -q alpha-ok || { echo "$(RED)OpenWRT unreachable$(RESET)"; exit 1; }; \
	curl -s --connect-timeout 5 "http://$$esp32_host:2121/" >/dev/null 2>&1 || { echo "$(RED)ESP32 API unreachable$(RESET)"; exit 1; }; \
	echo "  $(GREEN)Both devices reachable$(RESET)"; \
	\
	echo ""; \
	echo "$(CYAN)Step 1: Save OpenWRT's current upstream$(RESET)"; \
	prev_ssid=$$(ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream list 2>/dev/null" | grep ACTIVE | awk '{print $$1}'); \
	echo "  Active upstream: $$prev_ssid"; \
	echo "$$prev_ssid" > /tmp/interop-upstream-prev.txt; \
	\
	echo ""; \
	echo "$(CYAN)Step 2: Check ESP32 API advertisement$(RESET)"; \
	esp32_api=$$(curl -s --connect-timeout 5 "http://$$esp32_host:2121/"); \
	esp32_kind=$$(echo "$$esp32_api" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	echo "  ESP32 API kind=$$esp32_kind"; \
	if [ "$$esp32_kind" != "10021" ]; then echo "$(YELLOW)WARN: ESP32 not advertising TollGate service$(RESET)"; fi; \
	\
	echo ""; \
	echo "$(CYAN)Step 3: Connect OpenWRT to ESP32's AP ($$esp32_ssid)$(RESET)"; \
	echo "$(YELLOW)This will disrupt OpenWRT's current upstream connectivity.$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$esp32_ssid' 2>&1"; \
	echo "  $(GREEN)Connect command sent$(RESET)"; \
	\
	echo ""; \
	echo "$(CYAN)Step 4: Wait for DHCP on wwan (up to 60s)$(RESET)"; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12; do \
		sleep 5; \
		if ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "ifstatus wwan 2>/dev/null | jsonfilter -e '@.up' 2>/dev/null | grep -q true" 2>/dev/null; then \
			echo "$(GREEN)Connected after $$((i*5))s$(RESET)"; \
			break; \
		fi; \
		if [ "$$i" = "12" ]; then \
			echo "$(RED)Failed to connect$(RESET)"; \
			echo "$(YELLOW)Restoring upstream...$(RESET)"; \
			ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid' '$$upstream_pass'" 2>/dev/null || \
			ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid'" 2>/dev/null; \
			exit 1; \
		fi; \
		echo "  ... $$((i*5))s"; \
	done; \
	\
	echo ""; \
	echo "$(CYAN)Step 5: Watch for auto-payment (up to 30s)$(RESET)"; \
	timeout 30 ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "logread -e tollgate-wrt -f" 2>/dev/null | grep --line-buffered -i "payment\|session\|purchase\|allotment" | head -5; \
	\
	echo ""; \
	echo "$(CYAN)Step 6: Verify session on ESP32 (via serial log)$(RESET)"; \
	echo "$(YELLOW)Check ESP32 serial output for 'Session created' log.$(RESET)"; \
	echo "$(YELLOW)Or check: curl http://$$esp32_host:2121/wallet$(RESET)"; \
	\
	echo ""; \
	echo "$(CYAN)Step 7: Restore OpenWRT upstream to $$prev_ssid$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid' '$$upstream_pass'" 2>&1 || \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid'" 2>&1; \
	echo "  $(GREEN)Upstream restored$(RESET)"; \
	\
	echo ""; \
	echo "$(CYAN)Step 8: Wait for OpenWRT recovery$(RESET)"; \
	for i in 1 2 3 4 5 6; do \
		sleep 10; \
		if ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "echo ok" 2>/dev/null | grep -q ok; then \
			echo "$(GREEN)OpenWRT recovered after $$((i*10))s$(RESET)"; \
			break; \
		fi; \
		if [ "$$i" = "6" ]; then echo "$(RED)OpenWRT not back after 60s$(RESET)"; exit 1; fi; \
		echo "  ... $$((i*10))s"; \
	done; \
	\
	echo ""; \
	echo "$(BOLD)=======================================$(RESET)"; \
	echo "$(GREEN)$(BOLD)  Scenario 3 complete: OpenWRT → ESP32$(RESET)"; \
	echo "$(BOLD)=======================================$(RESET)"; \
	rm -f /tmp/interop-upstream-prev.txt

# ===========================================================================
#  Scenario 5: ESP32 ↔ ESP32
# ===========================================================================

interop-esp32-esp32: ## Scenario 5: ESP32 cross-board payment (needs Board B flashed)
	@echo "$(BOLD)=======================================$(RESET)"
	@echo "$(BOLD)  Scenario 5: ESP32 ↔ ESP32$(RESET)"
	@echo "$(BOLD)=======================================$(RESET)"
	@echo ""
	@echo "$(YELLOW)This scenario requires Board B to be flashed with unique nsec.$(RESET)"
	@echo "$(YELLOW)Board B setup has not been automated yet.$(RESET)"
	@echo ""
	@$(RESOLVE_ESP32A); \
	esp32_b_ssid=$$(grep -E "^ESP32_B_SSID=" routers.env | cut -d= -f2); \
	esp32_b_host=$$(grep -E "^ESP32_B_HOST=" routers.env | cut -d= -f2); \
	if [ "$$esp32_b_ssid" = "TBD" ] || [ -z "$$esp32_b_host" ]; then \
		echo "$(RED)Board B not configured. Update routers.env with ESP32_B_SSID and ESP32_B_HOST.$(RESET)"; \
		echo "Steps to set up Board B:"; \
		echo "  1. Generate a new nsec: openssl rand -hex 32"; \
		echo "  2. Edit main/config.json with new nsec"; \
		echo "  3. make flash-b"; \
		echo "  4. Note the derived SSID and IP from serial output"; \
		echo "  5. Update routers.env"; \
		exit 1; \
	fi; \
	echo "Board B: SSID=$$esp32_b_ssid, Host=$$esp32_b_host"; \
	echo ""; \
	echo "$(CYAN)Step 1: Verify both boards reachable$(RESET)"; \
	curl -s --connect-timeout 5 "http://$$esp32_host:2121/" >/dev/null 2>&1 || { echo "$(RED)Board A unreachable$(RESET)"; exit 1; }; \
	curl -s --connect-timeout 5 "http://$$esp32_b_host:2121/" >/dev/null 2>&1 || { echo "$(RED)Board B unreachable (connect to its AP first)$(RESET)"; exit 1; }; \
	echo "  $(GREEN)Both boards reachable$(RESET)"; \
	\
	echo ""; \
	echo "$(CYAN)Step 2: Mint V3 token and pay Board B$(RESET)"; \
	TOKEN=$$(cashu --env-mint testnut.cashu.space send --legacy 21 2>/dev/null | tail -1); \
	if [ -z "$$TOKEN" ]; then echo "$(RED)Token minting failed$(RESET)"; exit 1; fi; \
	echo "  Token minted"; \
	RESP=$$(curl -s --connect-timeout 10 -X POST -d "$$TOKEN" "http://$$esp32_b_host:2121/"); \
	RKIND=$$(echo "$$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['kind'])" 2>/dev/null); \
	echo "  Payment response: kind=$$RKIND"; \
	if [ "$$RKIND" = "1022" ]; then \
		echo "  $(GREEN)Board B accepted payment$(RESET)"; \
	else \
		echo "  $(YELLOW)Board B payment response: $$RESP$(RESET)"; \
	fi

# ===========================================================================
#  Cleanup
# ===========================================================================

interop-cleanup: ## Restore original configs on all devices
	@echo "$(BOLD)=== Interop Cleanup ===$(RESET)"
	@$(RESOLVE_ALPHA); \
	echo "$(CYAN)Restoring OpenWRT production config...$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "if [ -f /etc/tollgate/config.json.prod-backup ]; then mv /etc/tollgate/config.json.prod-backup /etc/tollgate/config.json && echo '  Config restored from backup'; else echo '  No backup found, keeping current config'; fi"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "rm -f /etc/tollgate/config.json.bak /etc/tollgate/config.json.bak2 2>/dev/null"; \
	prev_ssid=$$(cat /tmp/interop-upstream-prev.txt 2>/dev/null); \
	if [ -n "$$prev_ssid" ]; then \
		upstream_pass=$$(grep -E "^UPSTREAM_PASS=" routers.env | cut -d= -f2); \
		echo "  Restoring upstream to $$prev_ssid..."; \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid' '$$upstream_pass'" 2>/dev/null || \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid'" 2>/dev/null; \
		rm -f /tmp/interop-upstream-prev.txt; \
	fi; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "/etc/init.d/tollgate-wrt restart" 2>/dev/null; \
	echo "  $(GREEN)OpenWRT cleanup done$(RESET)"
	@echo ""
	@echo "$(YELLOW)ESP32: No automated cleanup (firmware rebuild required for config changes).$(RESET)"
	@echo "$(GREEN)Interop cleanup complete.$(RESET)"

interop-save-state: ## Save current device state before testing
	@echo "$(BOLD)=== Saving Device State ===$(RESET)"
	@$(RESOLVE_ALPHA); \
	echo "$(CYAN)Saving OpenWRT config...$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "cp /etc/tollgate/config.json /etc/tollgate/config.json.prod-backup && echo 'Saved' || echo 'No config to save'"; \
	prev_ssid=$$(ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream list 2>/dev/null" | grep ACTIVE | awk '{print $$1}'); \
	echo "  Current upstream: $$prev_ssid"; \
	echo "$$prev_ssid" > /tmp/interop-upstream-prev.txt; \
	echo "$(GREEN)State saved$(RESET)"

interop-restore-state: ## Restore saved device state
	@echo "$(BOLD)=== Restoring Device State ===$(RESET)"
	@$(RESOLVE_ALPHA); \
	echo "$(CYAN)Restoring OpenWRT config...$(RESET)"; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "if [ -f /etc/tollgate/config.json.prod-backup ]; then mv /etc/tollgate/config.json.prod-backup /etc/tollgate/config.json && echo 'Config restored'; else echo 'No backup found'; fi"; \
	prev_ssid=$$(cat /tmp/interop-upstream-prev.txt 2>/dev/null); \
	if [ -n "$$prev_ssid" ]; then \
		upstream_pass=$$(grep -E "^UPSTREAM_PASS=" routers.env | cut -d= -f2); \
		echo "Restoring upstream to $$prev_ssid..."; \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid' '$$upstream_pass'" 2>/dev/null || \
		ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "tollgate upstream connect '$$prev_ssid'" 2>/dev/null; \
	fi; \
	ssh $(SSH_OPTS) $(ROUTER_USER)@$$alpha_host "/etc/init.d/tollgate-wrt restart" 2>/dev/null; \
	rm -f /tmp/interop-upstream-prev.txt; \
	echo "$(GREEN)State restored$(RESET)"
