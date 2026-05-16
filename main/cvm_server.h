#ifndef CVM_SERVER_H
#define CVM_SERVER_H

#include "esp_err.h"

esp_err_t cvm_server_init(void);
void cvm_server_start(void);
void cvm_server_stop(void);

#endif
