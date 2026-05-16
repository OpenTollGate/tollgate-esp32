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
          -DECMULT_WINDOW_SIZE=8 -DECMULT_GEN_PREC_BITS=4 \
          -include stubs/esp_err.h \
          -I stubs \
          -I $(SECP256K1_INC) \
          -I $(SECP256K1_CFG) \
          -I /usr/include/cjson

LDFLAGS := -lmbedcrypto -lcjson

SECP256K1_OBJ := secp256k1.o precomputed_ecmult.o precomputed_ecmult_gen.o

TESTS := test_geohash test_identity test_nostr_event test_cashu test_session

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

test_nostr_event: test_nostr_event.c $(REPO_ROOT)/main/nostr_event.c $(SECP256K1_OBJ)
	$(CC) $(CFLAGS) -I $(SECP256K1_PRIV_INC) $< $(REPO_ROOT)/main/nostr_event.c $(SECP256K1_OBJ) -o $@ $(LDFLAGS)

test_cashu: test_cashu.c $(REPO_ROOT)/main/cashu.c
	$(CC) $(CFLAGS) $< $(REPO_ROOT)/main/cashu.c -o $@ $(LDFLAGS)

test_session: test_session.c $(REPO_ROOT)/main/session.c
	$(CC) $(CFLAGS) $< $(REPO_ROOT)/main/session.c -o $@ $(LDFLAGS)

clean:
	rm -f $(TESTS) $(SECP256K1_OBJ)
