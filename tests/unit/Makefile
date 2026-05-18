REPO_ROOT := ../..
SECP256K1_SRC := $(REPO_ROOT)/nucula_src/components/secp256k1/libsecp256k1
SECP256K1_INC := $(SECP256K1_SRC)/include
SECP256K1_PRIV_INC := $(SECP256K1_SRC)/src
SECP256K1_CFG := $(REPO_ROOT)/nucula_src/components/secp256k1
CJSON_SRC := $(REPO_ROOT)/../esp/esp-idf/components/json/cJSON

CC := gcc
CFLAGS := -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
          -std=gnu17 -g -O0 \
          -DTEST_HOST \
          -DENABLE_MODULE_SCHNORRSIG=1 -DENABLE_MODULE_EXTRAKEYS=1 \
          -DENABLE_MODULE_ECDH=1 \
          -DECMULT_WINDOW_SIZE=8 -DECMULT_GEN_PREC_BITS=4 \
          -include stubs/esp_err.h \
          -I stubs \
          -I $(SECP256K1_INC) \
          -I $(SECP256K1_CFG) \
          -I /usr/include/cjson

LDFLAGS := -lmbedcrypto -lcjson -lm

SECP256K1_OBJ := secp256k1.o precomputed_ecmult.o precomputed_ecmult_gen.o

TESTS := test_geohash test_identity test_nostr_event test_cashu test_session test_tollgate_client test_lnurl_pay test_lightning_payout test_mcp_handler test_nip04 test_cvm_server test_touch test_keyboard test_wifi_setup

.PHONY: all test clean $(TESTS)

all: test

test: $(TESTS)
	@echo ""
	@echo "=== Running all unit tests ==="
	@failed=0; \
	for t in $(TESTS); do \
		echo ""; \
		echo "--- $$t ---"; \
		./$$t || failed=$$((failed + 1)); \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "=== ALL UNIT TESTS PASSED ==="; \
	else \
		echo "=== $$failed test(s) FAILED ==="; \
		exit 1; \
	fi

$(SECP256K1_OBJ): %.o: $(SECP256K1_SRC)/src/%.c
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) -c $< -o $@

test_geohash: test_geohash.c $(REPO_ROOT)/main/geohash.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_identity: test_identity.c $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) $< $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_nostr_event: test_nostr_event.c $(REPO_ROOT)/main/nostr_event.c $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) $< $(REPO_ROOT)/main/nostr_event.c $(REPO_ROOT)/main/identity.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_cashu: test_cashu.c $(REPO_ROOT)/main/cashu.c
	$(CC) $(CFLAGS) $< $(REPO_ROOT)/main/cashu.c -o $@ $(LDFLAGS)

test_session: test_session.c $(REPO_ROOT)/main/session.c $(REPO_ROOT)/main/cashu.c
	$(CC) $(CFLAGS) $< $(REPO_ROOT)/main/session.c $(REPO_ROOT)/main/cashu.c -o $@ $(LDFLAGS)

test_tollgate_client: test_tollgate_client.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_lnurl_pay: test_lnurl_pay.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_lightning_payout: test_lightning_payout.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_mcp_handler: test_mcp_handler.c $(REPO_ROOT)/main/mcp_handler.c
	$(CC) $(CFLAGS) -I $(REPO_ROOT)/main $< $(REPO_ROOT)/main/mcp_handler.c -o $@ $(LDFLAGS)

test_nip04: test_nip04.c $(REPO_ROOT)/main/nip04.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) $< $(REPO_ROOT)/main/nip04.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_cvm_server: test_cvm_server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_touch: test_touch.c $(REPO_ROOT)/main/touch.c
	$(CC) $(CFLAGS) $< $(REPO_ROOT)/main/touch.c -o $@ $(LDFLAGS)

test_keyboard: test_keyboard.c $(REPO_ROOT)/main/keyboard.c
	$(CC) $(CFLAGS) $< $(REPO_ROOT)/main/keyboard.c -o $@ $(LDFLAGS)

test_wifi_setup: test_wifi_setup.c $(REPO_ROOT)/main/wifi_setup.c
	$(CC) $(CFLAGS) $< $(REPO_ROOT)/main/wifi_setup.c -o $@ $(LDFLAGS)

clean:
	rm -f $(TESTS) $(SECP256K1_OBJ)
