#ifndef STUBS_NUCULA_WALLET_H
#define STUBS_NUCULA_WALLET_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

esp_err_t nucula_wallet_init(const char *mint_url);
esp_err_t nucula_wallet_receive(const char *token_str);
esp_err_t nucula_wallet_send(uint64_t amount_sat, char *token_out, size_t token_out_size);
uint64_t nucula_wallet_balance(void);
int nucula_wallet_proof_count(void);
char *nucula_wallet_proofs_json(void);
esp_err_t nucula_wallet_swap_all(void);
esp_err_t nucula_wallet_melt(const char *bolt11_invoice, uint64_t max_fee_sats);
void nucula_wallet_print_status(void);

#endif
