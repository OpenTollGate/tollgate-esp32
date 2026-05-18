#include "test_framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    uint64_t created_at;
    uint8_t id[32];
} negentropy_item_t;

typedef struct negentropy_adapter {
    void *storage;
    negentropy_item_t *items;
    size_t count;
    size_t capacity;
} negentropy_adapter_t;

static negentropy_adapter_t *negentropy_adapter_from_storage(void *storage_engine)
{
    if (!storage_engine) return NULL;
    negentropy_adapter_t *adapter = calloc(1, sizeof(negentropy_adapter_t));
    if (!adapter) return NULL;
    adapter->storage = storage_engine;
    return adapter;
}

static void negentropy_adapter_destroy(negentropy_adapter_t *adapter)
{
    if (!adapter) return;
    if (adapter->items) free(adapter->items);
    free(adapter);
}

static int adapter_insert(negentropy_adapter_t *adapter, uint64_t created_at, const uint8_t *id)
{
    if (!adapter || !id) return -1;
    if (adapter->count >= adapter->capacity) {
        size_t new_cap = adapter->capacity == 0 ? 64 : adapter->capacity * 2;
        negentropy_item_t *new_items = realloc(adapter->items, new_cap * sizeof(negentropy_item_t));
        if (!new_items) return -2;
        adapter->items = new_items;
        adapter->capacity = new_cap;
    }
    negentropy_item_t *item = &adapter->items[adapter->count];
    item->created_at = created_at;
    memcpy(item->id, id, 32);
    adapter->count++;
    return 0;
}

static int test_adapter_create(void) {
    negentropy_adapter_t *a = negentropy_adapter_from_storage((void*)0x1);
    ASSERT(a != NULL, "adapter created from storage");
    ASSERT(a->storage == (void*)0x1, "storage pointer set");
    ASSERT(a->count == 0, "initial count is 0");
    ASSERT(a->items == NULL, "initial items is NULL");
    negentropy_adapter_destroy(a);
    return 0;
}

static int test_adapter_null_storage(void) {
    negentropy_adapter_t *a = negentropy_adapter_from_storage(NULL);
    ASSERT(a == NULL, "NULL storage returns NULL adapter");
    return 0;
}

static int test_adapter_insert(void) {
    negentropy_adapter_t *a = negentropy_adapter_from_storage((void*)0x1);
    uint8_t id[32];
    memset(id, 0xAA, 32);
    int rc = adapter_insert(a, 1700000000, id);
    ASSERT(rc == 0, "insert succeeds");
    ASSERT(a->count == 1, "count is 1 after insert");
    ASSERT(a->items[0].created_at == 1700000000, "created_at stored");
    ASSERT(memcmp(a->items[0].id, id, 32) == 0, "id stored correctly");
    negentropy_adapter_destroy(a);
    return 0;
}

static int test_adapter_insert_multiple(void) {
    negentropy_adapter_t *a = negentropy_adapter_from_storage((void*)0x1);
    uint8_t id1[32]; memset(id1, 0x11, 32);
    uint8_t id2[32]; memset(id2, 0x22, 32);
    uint8_t id3[32]; memset(id3, 0x33, 32);

    adapter_insert(a, 100, id1);
    adapter_insert(a, 200, id2);
    adapter_insert(a, 300, id3);

    ASSERT(a->count == 3, "count is 3 after 3 inserts");
    ASSERT(a->items[0].created_at == 100, "item 0 created_at");
    ASSERT(a->items[1].created_at == 200, "item 1 created_at");
    ASSERT(a->items[2].created_at == 300, "item 2 created_at");
    ASSERT(memcmp(a->items[0].id, id1, 32) == 0, "item 0 id");
    ASSERT(memcmp(a->items[1].id, id2, 32) == 0, "item 1 id");
    ASSERT(memcmp(a->items[2].id, id3, 32) == 0, "item 2 id");

    negentropy_adapter_destroy(a);
    return 0;
}

static int test_adapter_grow(void) {
    negentropy_adapter_t *a = negentropy_adapter_from_storage((void*)0x1);
    uint8_t id[32];
    for (int i = 0; i < 100; i++) {
        memset(id, i, 32);
        int rc = adapter_insert(a, i, id);
        ASSERT(rc == 0, "insert succeeds");
    }
    ASSERT(a->count == 100, "count is 100");
    ASSERT(a->capacity >= 100, "capacity >= 100");
    negentropy_adapter_destroy(a);
    return 0;
}

static int test_adapter_destroy_null(void) {
    negentropy_adapter_destroy(NULL);
    ASSERT(1, "destroy NULL does not crash");
    return 0;
}

int main(void) {
    int failed = 0;
    failed += test_adapter_create();
    failed += test_adapter_null_storage();
    failed += test_adapter_insert();
    failed += test_adapter_insert_multiple();
    failed += test_adapter_grow();
    failed += test_adapter_destroy_null();

    if (failed == 0) {
        printf("\n=== ALL NEGENTROPY ADAPTER TESTS PASSED ===\n");
    }
    return failed;
}
