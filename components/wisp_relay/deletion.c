#include "deletion.h"
#include "relay_types.h"
#include "cJSON.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "deletion";

static int extract_event_id_field(const char *event_json, size_t len,
                                  uint8_t id_out[32])
{
    cJSON *obj = cJSON_ParseWithLength(event_json, len);
    if (!obj) return -1;
    cJSON *id_item = cJSON_GetObjectItem(obj, "id");
    if (!id_item || !cJSON_IsString(id_item) || strlen(id_item->valuestring) != 64) {
        cJSON_Delete(obj);
        return -1;
    }
    int ret = relay_hex_to_bytes(id_item->valuestring, 64, id_out, 32);
    cJSON_Delete(obj);
    return ret;
}

static char *extract_pubkey_hex(const char *event_json, size_t len)
{
    cJSON *obj = cJSON_ParseWithLength(event_json, len);
    if (!obj) return NULL;
    cJSON *pk = cJSON_GetObjectItem(obj, "pubkey");
    char *result = NULL;
    if (pk && cJSON_IsString(pk)) result = strdup(pk->valuestring);
    cJSON_Delete(obj);
    return result;
}

static int delete_by_e_tags(storage_engine_t *storage, const char *event_json,
                            size_t len, const char *deleter_pubkey)
{
    cJSON *obj = cJSON_ParseWithLength(event_json, len);
    if (!obj) return 0;

    cJSON *tags = cJSON_GetObjectItem(obj, "tags");
    if (!tags || !cJSON_IsArray(tags)) { cJSON_Delete(obj); return 0; }

    int deleted = 0;
    int array_size = cJSON_GetArraySize(tags);

    for (int i = 0; i < array_size; i++) {
        cJSON *tag = cJSON_GetArrayItem(tags, i);
        if (!cJSON_IsArray(tag)) continue;
        cJSON *tag_name = cJSON_GetArrayItem(tag, 0);
        if (!tag_name || !cJSON_IsString(tag_name)) continue;
        if (strcmp(tag_name->valuestring, "e") != 0) continue;

        cJSON *tag_val = cJSON_GetArrayItem(tag, 1);
        if (!tag_val || !cJSON_IsString(tag_val)) continue;

        uint8_t event_id[32];
        if (relay_hex_to_bytes(tag_val->valuestring, 64, event_id, 32) != 0) continue;

        storage_error_t err = storage_delete_event(storage, event_id);
        if (err == STORAGE_OK) {
            deleted++;
            ESP_LOGI(TAG, "Deleted event: %.16s...", tag_val->valuestring);
        }
    }

    cJSON_Delete(obj);
    return deleted;
}

static int delete_by_a_tags(storage_engine_t *storage, const char *event_json,
                            size_t len, const char *deleter_pubkey,
                            uint64_t created_at)
{
    cJSON *obj = cJSON_ParseWithLength(event_json, len);
    if (!obj) return 0;

    cJSON *tags = cJSON_GetObjectItem(obj, "tags");
    if (!tags || !cJSON_IsArray(tags)) { cJSON_Delete(obj); return 0; }

    int deleted = 0;
    int array_size = cJSON_GetArraySize(tags);

    for (int i = 0; i < array_size; i++) {
        cJSON *tag = cJSON_GetArrayItem(tags, i);
        if (!cJSON_IsArray(tag)) continue;
        cJSON *tag_name = cJSON_GetArrayItem(tag, 0);
        if (!tag_name || !cJSON_IsString(tag_name)) continue;
        if (strcmp(tag_name->valuestring, "a") != 0) continue;

        cJSON *tag_val = cJSON_GetArrayItem(tag, 1);
        if (!tag_val || !cJSON_IsString(tag_val)) continue;

        int32_t kind;
        char pubkey[65] = {0};
        char d_tag[256] = "";
        if (sscanf(tag_val->valuestring, "%" SCNd32 ":%64[^:]:%255s",
                   &kind, pubkey, d_tag) < 2)
            continue;

        if (strcmp(pubkey, deleter_pubkey) != 0) continue;

        char **results = NULL;
        uint16_t count = 0;
        storage_query_events_json(storage, kind, pubkey, 100, &results, &count);
        for (uint16_t e = 0; e < count; e++) {
            if (storage_delete_event(storage, (const uint8_t *)results[e]) == STORAGE_OK) {
                deleted++;
            }
        }
        storage_free_query_results(results, count);
    }

    cJSON_Delete(obj);
    return deleted;
}

static int delete_by_k_tags(storage_engine_t *storage, const char *event_json,
                            size_t len, const char *deleter_pubkey,
                            uint64_t created_at)
{
    cJSON *obj = cJSON_ParseWithLength(event_json, len);
    if (!obj) return 0;

    cJSON *tags = cJSON_GetObjectItem(obj, "tags");
    if (!tags || !cJSON_IsArray(tags)) { cJSON_Delete(obj); return 0; }

    int deleted = 0;
    int array_size = cJSON_GetArraySize(tags);

    for (int i = 0; i < array_size; i++) {
        cJSON *tag = cJSON_GetArrayItem(tags, i);
        if (!cJSON_IsArray(tag)) continue;
        cJSON *tag_name = cJSON_GetArrayItem(tag, 0);
        if (!tag_name || !cJSON_IsString(tag_name)) continue;
        if (strcmp(tag_name->valuestring, "k") != 0) continue;

        cJSON *tag_val = cJSON_GetArrayItem(tag, 1);
        if (!tag_val || !cJSON_IsString(tag_val)) continue;

        int kind = atoi(tag_val->valuestring);

        char **results = NULL;
        uint16_t count = 0;
        storage_query_events_json(storage, kind, deleter_pubkey, 500, &results, &count);
        for (uint16_t e = 0; e < count; e++) {
            uint8_t eid[32];
            if (extract_event_id_field(results[e], strlen(results[e]), eid) == 0) {
                storage_delete_event(storage, eid);
                deleted++;
            }
        }
        storage_free_query_results(results, count);
    }

    cJSON_Delete(obj);
    return deleted;
}

int deletion_process_json(storage_engine_t *storage, const char *event_json,
                          size_t event_len)
{
    if (!storage || !event_json) return 0;

    cJSON *obj = cJSON_ParseWithLength(event_json, event_len);
    if (!obj) return 0;
    cJSON *kind_item = cJSON_GetObjectItem(obj, "kind");
    int kind = kind_item ? kind_item->valueint : 0;
    cJSON *pk_item = cJSON_GetObjectItem(obj, "pubkey");
    const char *pubkey = pk_item ? pk_item->valuestring : "";
    cJSON *ca_item = cJSON_GetObjectItem(obj, "created_at");
    uint64_t created_at = ca_item ? (uint64_t)ca_item->valuedouble : 0;
    cJSON_Delete(obj);

    if (kind != NOSTR_KIND_DELETION) return 0;

    char *deleter_pk = strdup(pubkey);
    if (!deleter_pk) return 0;

    int deleted = 0;
    deleted += delete_by_e_tags(storage, event_json, event_len, deleter_pk);
    deleted += delete_by_a_tags(storage, event_json, event_len, deleter_pk, created_at);
    deleted += delete_by_k_tags(storage, event_json, event_len, deleter_pk, created_at);

    free(deleter_pk);
    ESP_LOGI(TAG, "Deletion processed: %d events removed", deleted);
    return deleted;
}
