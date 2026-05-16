#ifndef LNURL_PAY_H
#define LNURL_PAY_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#define LNURL_MAX_BOLT11_LEN 2048
#define LNURL_MAX_ADDR_LEN   128

esp_err_t lnurl_get_invoice(const char *lightning_address, uint64_t amount_sats,
                            char *bolt11_out, size_t bolt11_out_size);

#endif
