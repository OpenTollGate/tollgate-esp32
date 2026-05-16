#ifndef CASHU_H
#define CASHU_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define CASHU_MAX_PROOFS      10
#define CASHU_MAX_SECRET_LEN  128
#define CASHU_MAX_ID_LEN      68
#define CASHU_MAX_C_LEN       128

typedef struct {
    uint64_t amount;
    char id[CASHU_MAX_ID_LEN];
    char secret[CASHU_MAX_SECRET_LEN];
    char c[CASHU_MAX_C_LEN];
} cashu_proof_t;

typedef struct {
    cashu_proof_t proofs[CASHU_MAX_PROOFS];
    int proof_count;
    char mint_url[256];
    uint64_t total_amount;
} cashu_token_t;

typedef struct {
    char y_hex[65];
    bool spent;
} cashu_proof_state_t;

esp_err_t cashu_decode_token(const char *token_str, cashu_token_t *out);

esp_err_t cashu_check_proof_states(const char *mint_url, const cashu_token_t *token,
                                   cashu_proof_state_t *states, int *state_count);

uint64_t cashu_calculate_allotment_ms(uint64_t token_amount, uint64_t price_per_step,
                                       uint64_t step_size_ms);

bool cashu_is_mint_accepted(const char *mint_url);

#endif
