#ifndef NEGENTROPY_ADAPTER_H
#define NEGENTROPY_ADAPTER_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t created_at;
    uint8_t id[32];
} negentropy_item_t;

typedef struct negentropy_adapter negentropy_adapter_t;

negentropy_adapter_t *negentropy_adapter_from_storage(void *storage_engine);

esp_err_t negentropy_adapter_get_items(negentropy_adapter_t *adapter,
                                       negentropy_item_t **items,
                                       size_t *count);

esp_err_t negentropy_adapter_insert_item(negentropy_adapter_t *adapter,
                                          uint64_t created_at,
                                          const uint8_t *id);

void negentropy_adapter_destroy(negentropy_adapter_t *adapter);

#endif
