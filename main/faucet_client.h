#ifndef FAUCET_CLIENT_H
#define FAUCET_CLIENT_H

#include "esp_err.h"

esp_err_t faucet_client_start(void);
void faucet_client_stop(void);

#endif
