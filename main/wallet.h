#ifndef WALLET_H
#define WALLET_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define WALLET_MAX_PROOFS 50
#define WALLET_MAX_KEYSETS 5
#define WALLET_KEYSET_ID_LEN 68
#define WALLET_SECRET_LEN 65
#define WALLET_SIG_LEN 67

typedef struct {
    uint64_t amount;
    char id[WALLET_KEYSET_ID_LEN];
    char secret[WALLET_SECRET_LEN];
    char c[WALLET_SIG_LEN];
} wallet_proof_t;

typedef struct {
    char id[WALLET_KEYSET_ID_LEN];
    char public_key_33[67];
    uint64_t amount;
    int input_fee_ppk;
} wallet_keyset_t;

typedef struct {
    wallet_proof_t proofs[WALLET_MAX_PROOFS];
    int proof_count;
    wallet_keyset_t keysets[WALLET_MAX_KEYSETS];
    int keyset_count;
    uint64_t balance;
} wallet_t;

esp_err_t wallet_init(void);
wallet_t *wallet_get(void);
uint64_t wallet_balance(void);

esp_err_t wallet_add_proofs(const wallet_proof_t *proofs, int count);
esp_err_t wallet_remove_proof(int index);
void wallet_clear(void);

esp_err_t wallet_fetch_keysets(const char *mint_url);
esp_err_t wallet_swap_proofs(const char *mint_url, int start_index, int count);

esp_err_t wallet_create_token(char *out, size_t out_size, uint64_t amount,
                               const char *mint_url);
esp_err_t wallet_send(const char *mint_url, uint64_t amount,
                       char *token_out, size_t token_out_size);

void wallet_print_status(void);
#endif
