#include "handlers.h"
#include "router.h"
#include "storage_engine.h"
#include "sub_manager.h"
#include "relay_validator.h"
#include "broadcaster.h"
#include "deletion.h"
#include "rate_limiter.h"
#include "relay_types.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "handlers";

int handle_event(relay_ctx_t *ctx, int conn_fd, const char *event_json, size_t event_len)
{
    if (!ctx || !event_json) return -1;

    if (ctx->rate_limiter) {
        if (!rate_limiter_check(ctx->rate_limiter, conn_fd, RATE_TYPE_EVENT)) {
            router_send_ok(ctx, conn_fd, "", false, "rate limited");
            return -1;
        }
    }

    cJSON *obj = cJSON_ParseWithLength(event_json, event_len);
    if (!obj) {
        router_send_ok(ctx, conn_fd, "", false, "invalid JSON");
        return -1;
    }

    cJSON *id_item = cJSON_GetObjectItem(obj, "id");
    cJSON *pubkey_item = cJSON_GetObjectItem(obj, "pubkey");
    cJSON *kind_item = cJSON_GetObjectItem(obj, "kind");
    cJSON *ca_item = cJSON_GetObjectItem(obj, "created_at");

    if (!id_item || !pubkey_item || !kind_item || !ca_item) {
        cJSON_Delete(obj);
        router_send_ok(ctx, conn_fd, "", false, "missing required fields");
        return -1;
    }

    const char *id_hex = id_item->valuestring;
    const char *pubkey_hex = pubkey_item->valuestring;
    int kind = kind_item->valueint;
    uint64_t created_at = (uint64_t)ca_item->valuedouble;

    if (ctx->config.max_future_sec > 0) {
        int64_t now = (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
        if ((int64_t)created_at > now + ctx->config.max_future_sec) {
            cJSON_Delete(obj);
            router_send_ok(ctx, conn_fd, id_hex, false, "created_at too far in future");
            return -1;
        }
    }

    uint8_t event_id[32];
    if (relay_hex_to_bytes(id_hex, 64, event_id, 32) != 0) {
        cJSON_Delete(obj);
        router_send_ok(ctx, conn_fd, "", false, "invalid event id");
        return -1;
    }

    if (storage_event_exists(ctx->storage, event_id)) {
        cJSON_Delete(obj);
        router_send_ok(ctx, conn_fd, id_hex, true, "duplicate");
        return 0;
    }

    if (!relay_validator_verify_event(event_json, event_len)) {
        cJSON_Delete(obj);
        router_send_ok(ctx, conn_fd, id_hex, false, "invalid signature");
        return -1;
    }

    cJSON_Delete(obj);

    storage_error_t err = storage_save_event_json(ctx->storage, event_json, event_len);
    if (err != STORAGE_OK) {
        const char *msg = (err == STORAGE_ERR_FULL) ? "relay full" :
                          (err == STORAGE_ERR_DUPLICATE) ? "duplicate" : "storage error";
        router_send_ok(ctx, conn_fd, id_hex, false, msg);
        return -1;
    }

    router_send_ok(ctx, conn_fd, id_hex, true, "");

    if (kind == NOSTR_KIND_DELETION) {
        deletion_process_json(ctx->storage, event_json, event_len);
    }

    broadcaster_fanout_json(ctx, event_json, event_len, kind, pubkey_hex, created_at);

    return 0;
}

static void parse_filter_json(const char *json, sub_filter_t *filter)
{
    memset(filter, 0, sizeof(sub_filter_t));
    cJSON *obj = cJSON_Parse(json);
    if (!obj) return;

    cJSON *arr;

    arr = cJSON_GetObjectItem(obj, "ids");
    if (arr && cJSON_IsArray(arr)) {
        filter->ids_count = cJSON_GetArraySize(arr);
        if (filter->ids_count > SUB_MAX_FILTER_IDS) filter->ids_count = SUB_MAX_FILTER_IDS;
        for (size_t i = 0; i < filter->ids_count; i++)
            filter->ids[i] = strdup(cJSON_GetArrayItem(arr, i)->valuestring);
    }

    arr = cJSON_GetObjectItem(obj, "authors");
    if (arr && cJSON_IsArray(arr)) {
        filter->authors_count = cJSON_GetArraySize(arr);
        if (filter->authors_count > SUB_MAX_FILTER_AUTHORS) filter->authors_count = SUB_MAX_FILTER_AUTHORS;
        for (size_t i = 0; i < filter->authors_count; i++)
            filter->authors[i] = strdup(cJSON_GetArrayItem(arr, i)->valuestring);
    }

    arr = cJSON_GetObjectItem(obj, "kinds");
    if (arr && cJSON_IsArray(arr)) {
        filter->kinds_count = cJSON_GetArraySize(arr);
        if (filter->kinds_count > SUB_MAX_FILTER_KINDS) filter->kinds_count = SUB_MAX_FILTER_KINDS;
        for (size_t i = 0; i < filter->kinds_count; i++)
            filter->kinds[i] = cJSON_GetArrayItem(arr, i)->valueint;
    }

    arr = cJSON_GetObjectItem(obj, "#e");
    if (arr && cJSON_IsArray(arr)) {
        filter->e_tags_count = cJSON_GetArraySize(arr);
        if (filter->e_tags_count > SUB_MAX_FILTER_ETAGS) filter->e_tags_count = SUB_MAX_FILTER_ETAGS;
        for (size_t i = 0; i < filter->e_tags_count; i++)
            filter->e_tags[i] = strdup(cJSON_GetArrayItem(arr, i)->valuestring);
    }

    arr = cJSON_GetObjectItem(obj, "#p");
    if (arr && cJSON_IsArray(arr)) {
        filter->p_tags_count = cJSON_GetArraySize(arr);
        if (filter->p_tags_count > SUB_MAX_FILTER_PTAGS) filter->p_tags_count = SUB_MAX_FILTER_PTAGS;
        for (size_t i = 0; i < filter->p_tags_count; i++)
            filter->p_tags[i] = strdup(cJSON_GetArrayItem(arr, i)->valuestring);
    }

    cJSON *since = cJSON_GetObjectItem(obj, "since");
    if (since) filter->since = (int64_t)since->valuedouble;
    cJSON *until = cJSON_GetObjectItem(obj, "until");
    if (until) filter->until = (int64_t)until->valuedouble;
    cJSON *limit = cJSON_GetObjectItem(obj, "limit");
    if (limit) filter->limit = limit->valueint;

    cJSON_Delete(obj);
}

void handle_req(relay_ctx_t *ctx, int conn_fd, const char *sub_id, const char *filters_json)
{
    if (!ctx || !sub_id) return;

    if (ctx->rate_limiter) {
        if (!rate_limiter_check(ctx->rate_limiter, conn_fd, RATE_TYPE_REQ)) {
            router_send_closed(ctx, conn_fd, sub_id, "rate limited");
            return;
        }
    }

    sub_filter_t filter;
    parse_filter_json(filters_json, &filter);

    int query_kind = -1;
    const char *query_author = NULL;
    int query_limit = filter.limit > 0 ? filter.limit : 100;

    if (filter.kinds_count > 0) query_kind = filter.kinds[0];
    if (filter.authors_count > 0) query_author = filter.authors[0];

    char **results = NULL;
    uint16_t count = 0;
    storage_query_events_json(ctx->storage, query_kind, query_author,
                              query_limit, &results, &count);

    for (uint16_t i = 0; i < count; i++) {
        router_send_event(ctx, conn_fd, sub_id, results[i], strlen(results[i]));
    }
    storage_free_query_results(results, count);

    router_send_eose(ctx, conn_fd, sub_id);

    sub_manager_add(ctx->sub_manager, conn_fd, sub_id, &filter, 1);

    sub_filter_t *f = &filter;
    for (size_t i = 0; i < f->ids_count; i++) free(f->ids[i]);
    for (size_t i = 0; i < f->authors_count; i++) free(f->authors[i]);
    for (size_t i = 0; i < f->e_tags_count; i++) free(f->e_tags[i]);
    for (size_t i = 0; i < f->p_tags_count; i++) free(f->p_tags[i]);
}

int handle_close(relay_ctx_t *ctx, int conn_fd, const char *sub_id)
{
    if (!ctx || !sub_id) return -1;
    sub_manager_remove(ctx->sub_manager, conn_fd, sub_id);
    return 0;
}
