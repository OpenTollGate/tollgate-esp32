#ifndef TOLLGATE_CORE_CASHU_H
#define TOLLGATE_CORE_CASHU_H

#include <stdint.h>
#include <stdbool.h>

#define TG_CASHU_MAX_PROOFS      10
#define TG_CASHU_MAX_SECRET_LEN  128
#define TG_CASHU_MAX_ID_LEN      68
#define TG_CASHU_MAX_C_LEN       128

typedef struct {
    uint64_t amount;
    char id[TG_CASHU_MAX_ID_LEN];
    char secret[TG_CASHU_MAX_SECRET_LEN];
    char c[TG_CASHU_MAX_C_LEN];
} tg_cashu_proof_t;

typedef struct {
    tg_cashu_proof_t proofs[TG_CASHU_MAX_PROOFS];
    int proof_count;
    char mint_url[256];
    uint64_t total_amount;
} tg_cashu_token_t;

typedef struct {
    char y_hex[65];
    bool spent;
} tg_cashu_proof_state_t;

int tg_cashu_decode_token(const char *token_str, tg_cashu_token_t *out);
int tg_cashu_check_proof_states(const char *mint_url, const tg_cashu_token_t *token,
                                 tg_cashu_proof_state_t *states, int *state_count);
uint64_t tg_cashu_calculate_allotment_ms(uint64_t token_amount, uint64_t price_per_step,
                                          uint64_t step_size_ms);
uint64_t tg_cashu_calculate_allotment(uint64_t token_amount, uint64_t price_per_step,
                                       uint64_t step_size);
bool tg_cashu_is_mint_accepted(const char *mint_url, const char *accepted_mint_url);

#endif
