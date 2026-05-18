#ifndef NUCULA_WALLET_H
#define NUCULA_WALLET_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nucula_wallet_init(const char *mint_url);
esp_err_t nucula_wallet_init_multi(const char mint_urls[][256], int count);

esp_err_t nucula_wallet_receive(const char *token_str);

esp_err_t nucula_wallet_send(uint64_t amount_sat, char *token_out, size_t token_out_size);

uint64_t nucula_wallet_balance(void);

int nucula_wallet_proof_count(void);

char *nucula_wallet_proofs_json(void);

esp_err_t nucula_wallet_swap_all(void);

esp_err_t nucula_wallet_melt(const char *bolt11_invoice, uint64_t max_fee_sats);

void nucula_wallet_print_status(void);

#ifdef __cplusplus
}
#endif

#endif
