#include "sub_manager.h"
#include "relay_types.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "sub_mgr";

static void filter_clear(sub_filter_t *f)
{
    for (size_t i = 0; i < f->ids_count; i++) free(f->ids[i]);
    for (size_t i = 0; i < f->authors_count; i++) free(f->authors[i]);
    for (size_t i = 0; i < f->e_tags_count; i++) free(f->e_tags[i]);
    for (size_t i = 0; i < f->p_tags_count; i++) free(f->p_tags[i]);
    memset(f, 0, sizeof(sub_filter_t));
}

static bool filter_copy(sub_filter_t *dst, const sub_filter_t *src)
{
    memset(dst, 0, sizeof(sub_filter_t));

    size_t ids_count = src->ids_count > SUB_MAX_FILTER_IDS ? SUB_MAX_FILTER_IDS : src->ids_count;
    for (size_t i = 0; i < ids_count; i++) {
        dst->ids[i] = strdup(src->ids[i]);
        if (!dst->ids[i]) goto fail;
    }
    dst->ids_count = ids_count;

    size_t authors_count = src->authors_count > SUB_MAX_FILTER_AUTHORS ? SUB_MAX_FILTER_AUTHORS : src->authors_count;
    for (size_t i = 0; i < authors_count; i++) {
        dst->authors[i] = strdup(src->authors[i]);
        if (!dst->authors[i]) goto fail;
    }
    dst->authors_count = authors_count;

    size_t kinds_count = src->kinds_count > SUB_MAX_FILTER_KINDS ? SUB_MAX_FILTER_KINDS : src->kinds_count;
    memcpy(dst->kinds, src->kinds, kinds_count * sizeof(int32_t));
    dst->kinds_count = kinds_count;

    size_t e_tags_count = src->e_tags_count > SUB_MAX_FILTER_ETAGS ? SUB_MAX_FILTER_ETAGS : src->e_tags_count;
    for (size_t i = 0; i < e_tags_count; i++) {
        dst->e_tags[i] = strdup(src->e_tags[i]);
        if (!dst->e_tags[i]) goto fail;
    }
    dst->e_tags_count = e_tags_count;

    size_t p_tags_count = src->p_tags_count > SUB_MAX_FILTER_PTAGS ? SUB_MAX_FILTER_PTAGS : src->p_tags_count;
    for (size_t i = 0; i < p_tags_count; i++) {
        dst->p_tags[i] = strdup(src->p_tags[i]);
        if (!dst->p_tags[i]) goto fail;
    }
    dst->p_tags_count = p_tags_count;

    dst->since = src->since;
    dst->until = src->until;
    dst->limit = src->limit;
    return true;

fail:
    filter_clear(dst);
    return false;
}

static void clear_subscription(subscription_t *sub)
{
    for (uint8_t i = 0; i < sub->filter_count; i++) {
        filter_clear(&sub->filters[i]);
    }
    memset(sub, 0, sizeof(subscription_t));
}

esp_err_t sub_manager_init(sub_manager_t *mgr)
{
    memset(mgr, 0, sizeof(sub_manager_t));
    mgr->lock = xSemaphoreCreateMutex();
    if (!mgr->lock) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Initialized (max=%d, per_conn=%d)", SUB_MAX_TOTAL, SUB_MAX_PER_CONN);
    return ESP_OK;
}

void sub_manager_destroy(sub_manager_t *mgr)
{
    if (!mgr) return;
    for (int i = 0; i < SUB_MAX_TOTAL; i++) {
        if (mgr->subs[i].active) clear_subscription(&mgr->subs[i]);
    }
    if (mgr->lock) { vSemaphoreDelete(mgr->lock); mgr->lock = NULL; }
}

static subscription_t *find_sub(sub_manager_t *mgr, int conn_fd, const char *sub_id)
{
    for (int i = 0; i < SUB_MAX_TOTAL; i++) {
        if (mgr->subs[i].active && mgr->subs[i].conn_fd == conn_fd &&
            strcmp(mgr->subs[i].sub_id, sub_id) == 0)
            return &mgr->subs[i];
    }
    return NULL;
}

static subscription_t *find_free_slot(sub_manager_t *mgr)
{
    for (int i = 0; i < SUB_MAX_TOTAL; i++) {
        if (!mgr->subs[i].active) return &mgr->subs[i];
    }
    return NULL;
}

static bool hex_prefix_match(const char *prefix, size_t prefix_len,
                              const char *full, size_t full_len)
{
    if (prefix_len == 0) return true;
    if (prefix_len > full_len) return false;
    return memcmp(prefix, full, prefix_len) == 0;
}

static bool filter_matches_event(const sub_filter_t *f, int event_kind,
                                  const char *pubkey_hex, uint64_t created_at)
{
    if (f->kinds_count > 0) {
        bool found = false;
        for (size_t i = 0; i < f->kinds_count; i++) {
            if (f->kinds[i] == event_kind) { found = true; break; }
        }
        if (!found) return false;
    }

    if (f->authors_count > 0) {
        bool found = false;
        for (size_t i = 0; i < f->authors_count; i++) {
            if (hex_prefix_match(f->authors[i], strlen(f->authors[i]),
                                 pubkey_hex, strlen(pubkey_hex))) {
                found = true; break;
            }
        }
        if (!found) return false;
    }

    if (f->since > 0 && (int64_t)created_at < f->since) return false;
    if (f->until > 0 && (int64_t)created_at > f->until) return false;

    return true;
}

void sub_manager_match_json(sub_manager_t *mgr, const char *event_json,
                            size_t event_len, int event_kind,
                            const char *event_pubkey_hex,
                            uint64_t event_created_at,
                            sub_match_result_t *result)
{
    result->count = 0;
    (void)event_json;
    (void)event_len;

    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    for (int i = 0; i < SUB_MAX_TOTAL; i++) {
        subscription_t *sub = &mgr->subs[i];
        if (!sub->active) continue;

        bool matched = false;
        for (uint8_t f = 0; f < sub->filter_count; f++) {
            if (filter_matches_event(&sub->filters[f], event_kind,
                                     event_pubkey_hex, event_created_at)) {
                matched = true;
                break;
            }
        }
        if (matched) {
            sub_match_entry_t *entry = &result->matches[result->count++];
            entry->conn_fd = sub->conn_fd;
            memcpy(entry->sub_id, sub->sub_id, sizeof(entry->sub_id));
        }
    }
    xSemaphoreGive(mgr->lock);
}

sub_error_t sub_manager_add(sub_manager_t *mgr, int conn_fd,
                            const char *sub_id,
                            const sub_filter_t *filters,
                            size_t filter_count)
{
    if (filter_count > SUB_MAX_FILTERS) filter_count = SUB_MAX_FILTERS;

    xSemaphoreTake(mgr->lock, portMAX_DELAY);

    subscription_t *existing = find_sub(mgr, conn_fd, sub_id);
    if (existing) {
        for (uint8_t i = 0; i < existing->filter_count; i++)
            filter_clear(&existing->filters[i]);
        existing->events_sent = 0;
        for (size_t i = 0; i < filter_count; i++) {
            if (!filter_copy(&existing->filters[i], &filters[i])) {
                existing->filter_count = (uint8_t)i;
                xSemaphoreGive(mgr->lock);
                return SUB_ERR_MEMORY;
            }
        }
        existing->filter_count = (uint8_t)filter_count;
        xSemaphoreGive(mgr->lock);
        return SUB_OK;
    }

    uint8_t conn_count = 0;
    for (int i = 0; i < SUB_MAX_TOTAL; i++) {
        if (mgr->subs[i].active && mgr->subs[i].conn_fd == conn_fd) conn_count++;
    }
    if (conn_count >= SUB_MAX_PER_CONN) {
        xSemaphoreGive(mgr->lock);
        return SUB_ERR_TOO_MANY_FILTERS;
    }

    subscription_t *slot = find_free_slot(mgr);
    if (!slot) { xSemaphoreGive(mgr->lock); return SUB_ERR_MEMORY; }

    memset(slot, 0, sizeof(subscription_t));
    strncpy(slot->sub_id, sub_id, SUB_MAX_ID_LEN);
    slot->sub_id[SUB_MAX_ID_LEN] = '\0';
    slot->conn_fd = conn_fd;

    for (size_t i = 0; i < filter_count; i++) {
        if (!filter_copy(&slot->filters[i], &filters[i])) {
            slot->filter_count = (uint8_t)i;
            clear_subscription(slot);
            xSemaphoreGive(mgr->lock);
            return SUB_ERR_MEMORY;
        }
    }
    slot->filter_count = (uint8_t)filter_count;
    slot->active = true;
    mgr->active_count++;

    ESP_LOGI(TAG, "Added sub=%s fd=%d filters=%zu total=%d",
             sub_id, conn_fd, filter_count, mgr->active_count);
    xSemaphoreGive(mgr->lock);
    return SUB_OK;
}

sub_error_t sub_manager_remove(sub_manager_t *mgr, int conn_fd, const char *sub_id)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    subscription_t *sub = find_sub(mgr, conn_fd, sub_id);
    if (!sub) { xSemaphoreGive(mgr->lock); return SUB_ERR_NOT_FOUND; }
    clear_subscription(sub);
    mgr->active_count--;
    xSemaphoreGive(mgr->lock);
    return SUB_OK;
}

void sub_manager_remove_all(sub_manager_t *mgr, int conn_fd)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    int removed = 0;
    for (int i = 0; i < SUB_MAX_TOTAL; i++) {
        if (mgr->subs[i].active && mgr->subs[i].conn_fd == conn_fd) {
            clear_subscription(&mgr->subs[i]);
            mgr->active_count--;
            removed++;
        }
    }
    if (removed > 0) ESP_LOGI(TAG, "Removed %d subs for fd=%d", removed, conn_fd);
    xSemaphoreGive(mgr->lock);
}

uint8_t sub_manager_count(sub_manager_t *mgr, int conn_fd)
{
    uint8_t count = 0;
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    for (int i = 0; i < SUB_MAX_TOTAL; i++) {
        if (mgr->subs[i].active && mgr->subs[i].conn_fd == conn_fd) count++;
    }
    xSemaphoreGive(mgr->lock);
    return count;
}
