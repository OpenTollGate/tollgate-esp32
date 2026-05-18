#include "negentropy_adapter.h"
#include "storage_engine.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "negentropy_adapter";

struct negentropy_adapter {
    void *storage;
    negentropy_item_t *items;
    size_t count;
    size_t capacity;
};

negentropy_adapter_t *negentropy_adapter_from_storage(void *storage_engine)
{
    if (!storage_engine) return NULL;

    negentropy_adapter_t *adapter = calloc(1, sizeof(negentropy_adapter_t));
    if (!adapter) return NULL;

    adapter->storage = storage_engine;
    adapter->items = NULL;
    adapter->count = 0;
    adapter->capacity = 0;

    return adapter;
}

esp_err_t negentropy_adapter_get_items(negentropy_adapter_t *adapter,
                                       negentropy_item_t **items,
                                       size_t *count)
{
    if (!adapter || !items || !count) return ESP_ERR_INVALID_ARG;

    if (adapter->items) {
        free(adapter->items);
        adapter->items = NULL;
    }
    adapter->count = 0;
    adapter->capacity = 0;

    *items = adapter->items;
    *count = adapter->count;

    ESP_LOGI(TAG, "Adapter has %zu items", adapter->count);
    return ESP_OK;
}

esp_err_t negentropy_adapter_insert_item(negentropy_adapter_t *adapter,
                                          uint64_t created_at,
                                          const uint8_t *id)
{
    if (!adapter || !id) return ESP_ERR_INVALID_ARG;

    if (adapter->count >= adapter->capacity) {
        size_t new_cap = adapter->capacity == 0 ? 64 : adapter->capacity * 2;
        negentropy_item_t *new_items = realloc(adapter->items, new_cap * sizeof(negentropy_item_t));
        if (!new_items) return ESP_ERR_NO_MEM;
        adapter->items = new_items;
        adapter->capacity = new_cap;
    }

    negentropy_item_t *item = &adapter->items[adapter->count];
    item->created_at = created_at;
    memcpy(item->id, id, 32);
    adapter->count++;

    return ESP_OK;
}

void negentropy_adapter_destroy(negentropy_adapter_t *adapter)
{
    if (!adapter) return;
    if (adapter->items) free(adapter->items);
    free(adapter);
}
