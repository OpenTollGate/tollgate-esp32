#ifndef WALLET_PERSIST_H
#define WALLET_PERSIST_H

#include "esp_err.h"

esp_err_t wallet_persist_save(void);
esp_err_t wallet_persist_load(void);

#endif
