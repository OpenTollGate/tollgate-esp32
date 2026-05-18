#include "storage_engine.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "storage";

#define INDEX_NVS_NAMESPACE "nostr_idx"
#define EVENTS_DIR "/littlefs/events"

static void get_event_path(const uint8_t event_id[32], uint32_t file_index,
                           char *path, size_t len)
{
    char id_hex[33];
    for (int i = 0; i < 16; i++) sprintf(id_hex + i * 2, "%02x", event_id[i]);
    snprintf(path, len, EVENTS_DIR "/%02x/%s_%08" PRIx32 ".json",
             event_id[0], id_hex, file_index);
}

static int save_index_to_nvs(storage_engine_t *engine)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(INDEX_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return STORAGE_ERR_IO;

    nvs_set_u16(nvs, "count", engine->index_count);
    nvs_set_u32(nvs, "next_idx", engine->next_file_index);

    const uint16_t chunk_size = 50;
    for (uint16_t i = 0; i < engine->index_count; i += chunk_size) {
        char key[16];
        snprintf(key, sizeof(key), "idx_%u", i / chunk_size);
        uint16_t entries = engine->index_count - i;
        if (entries > chunk_size) entries = chunk_size;
        nvs_set_blob(nvs, key, &engine->index[i], entries * sizeof(storage_index_entry_t));
    }
    nvs_commit(nvs);
    nvs_close(nvs);
    return STORAGE_OK;
}

static int load_index_from_nvs(storage_engine_t *engine)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(INDEX_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return STORAGE_OK;
    if (err != ESP_OK) return STORAGE_ERR_IO;

    err = nvs_get_u16(nvs, "count", &engine->index_count);
    if (err != ESP_OK) { nvs_close(nvs); return STORAGE_ERR_IO; }
    if (engine->index_count > engine->max_index_entries) engine->index_count = engine->max_index_entries;

    err = nvs_get_u32(nvs, "next_idx", &engine->next_file_index);
    if (err != ESP_OK) { nvs_close(nvs); return STORAGE_ERR_IO; }

    const uint16_t chunk_size = 50;
    for (uint16_t i = 0; i < engine->index_count; i += chunk_size) {
        char key[16];
        snprintf(key, sizeof(key), "idx_%u", i / chunk_size);
        uint16_t entries = engine->index_count - i;
        if (entries > chunk_size) entries = chunk_size;
        size_t len = entries * sizeof(storage_index_entry_t);
        nvs_get_blob(nvs, key, &engine->index[i], &len);
    }
    nvs_close(nvs);
    return STORAGE_OK;
}

static storage_index_entry_t *find_index_entry(storage_engine_t *engine,
                                               const uint8_t event_id[32])
{
    for (uint16_t i = 0; i < engine->index_count; i++) {
        if (memcmp(engine->index[i].event_id, event_id, 32) == 0 &&
            !(engine->index[i].flags & STORAGE_FLAG_DELETED)) {
            return &engine->index[i];
        }
    }
    return NULL;
}

static void parse_event_meta(const char *json, size_t len,
                             uint8_t *id_out, uint8_t *pubkey_out,
                             uint64_t *created_at_out, int *kind_out)
{
    extern int relay_hex_to_bytes(const char *hex, size_t hex_len, uint8_t *out, size_t out_len);
    extern void relay_bytes_to_hex(const uint8_t *bytes, size_t len, char *hex);

    id_out[0] = 0; pubkey_out[0] = 0; *created_at_out = 0; *kind_out = 0;

    const char *p;
    p = strstr(json, "\"id\":\"");
    if (p) relay_hex_to_bytes(p + 6, 64, id_out, 32);
    p = strstr(json, "\"pubkey\":\"");
    if (p) relay_hex_to_bytes(p + 10, 64, pubkey_out, 32);
    p = strstr(json, "\"created_at\":");
    if (p) *created_at_out = strtoull(p + 13, NULL, 10);
    p = strstr(json, "\"kind\":");
    if (p) *kind_out = atoi(p + 7);
}

esp_err_t storage_init(storage_engine_t *engine, uint32_t default_ttl_sec)
{
    memset(engine, 0, sizeof(storage_engine_t));
    engine->default_ttl_sec = default_ttl_sec;
    strcpy(engine->mount_point, "/littlefs");

    engine->lock = xSemaphoreCreateMutex();
    if (!engine->lock) return ESP_ERR_NO_MEM;

    engine->max_index_entries = STORAGE_INDEX_ENTRIES;
    engine->index = heap_caps_calloc(engine->max_index_entries,
                                     sizeof(storage_index_entry_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!engine->index) {
        engine->max_index_entries = 1000;
        engine->index = calloc(engine->max_index_entries, sizeof(storage_index_entry_t));
        if (!engine->index) { vSemaphoreDelete(engine->lock); return ESP_ERR_NO_MEM; }
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = STORAGE_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        free(engine->index);
        vSemaphoreDelete(engine->lock);
        return ret;
    }

    mkdir(EVENTS_DIR, 0755);
    for (int i = 0; i < 256; i++) {
        char subdir[64];
        snprintf(subdir, sizeof(subdir), EVENTS_DIR "/%02x", i);
        mkdir(subdir, 0755);
    }

    int load_err = load_index_from_nvs(engine);
    if (load_err != STORAGE_OK) {
        ESP_LOGW(TAG, "Failed to load index, starting fresh");
        engine->index_count = 0;
        engine->next_file_index = 0;
    }

    engine->initialized = true;

    size_t total, used;
    esp_littlefs_info(STORAGE_PARTITION_LABEL, &total, &used);
    ESP_LOGI(TAG, "Storage initialized: %" PRIu16 " events, %zu/%zu bytes used",
             engine->index_count, used, total);
    return ESP_OK;
}

void storage_destroy(storage_engine_t *engine)
{
    if (!engine->initialized) return;
    if (engine->cleanup_task) {
        engine->cleanup_stop = true;
        while (engine->cleanup_task != NULL) vTaskDelay(pdMS_TO_TICKS(100));
    }
    save_index_to_nvs(engine);
    esp_vfs_littlefs_unregister(STORAGE_PARTITION_LABEL);
    if (engine->index) { free(engine->index); engine->index = NULL; }
    if (engine->lock) { vSemaphoreDelete(engine->lock); engine->lock = NULL; }
    engine->initialized = false;
}

storage_error_t storage_save_event_json(storage_engine_t *engine,
                                        const char *event_json,
                                        size_t event_json_len)
{
    if (!engine->initialized) return STORAGE_ERR_NOT_INITIALIZED;

    uint8_t id[32] = {0}, pubkey[32] = {0};
    uint64_t created_at = 0;
    int kind = 0;
    parse_event_meta(event_json, event_json_len, id, pubkey, &created_at, &kind);

    xSemaphoreTake(engine->lock, portMAX_DELAY);

    if (find_index_entry(engine, id)) {
        xSemaphoreGive(engine->lock);
        return STORAGE_ERR_DUPLICATE;
    }
    if (engine->index_count >= engine->max_index_entries) {
        xSemaphoreGive(engine->lock);
        return STORAGE_ERR_FULL;
    }

    char path[128];
    get_event_path(id, engine->next_file_index, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        char dir[64];
        snprintf(dir, sizeof(dir), EVENTS_DIR "/%02x", id[0]);
        mkdir(dir, 0755);
        f = fopen(path, "wb");
    }
    if (!f) { xSemaphoreGive(engine->lock); return STORAGE_ERR_IO; }

    fwrite(event_json, 1, event_json_len, f);
    fclose(f);

    storage_index_entry_t *entry = &engine->index[engine->index_count];
    memcpy(entry->event_id, id, 32);
    entry->created_at = (uint32_t)created_at;
    entry->kind = kind;
    memcpy(entry->pubkey_prefix, pubkey, 4);
    entry->file_index = engine->next_file_index;
    entry->flags = 0;
    entry->expires_at = (uint32_t)time(NULL) + engine->default_ttl_sec;

    engine->index_count++;
    engine->next_file_index++;
    if (engine->index_count % 10 == 0) save_index_to_nvs(engine);

    xSemaphoreGive(engine->lock);
    return STORAGE_OK;
}

bool storage_event_exists(storage_engine_t *engine, const uint8_t event_id[32])
{
    if (!engine->initialized) return false;
    xSemaphoreTake(engine->lock, portMAX_DELAY);
    bool exists = (find_index_entry(engine, event_id) != NULL);
    xSemaphoreGive(engine->lock);
    return exists;
}

storage_error_t storage_query_events_json(storage_engine_t *engine,
                                          int kind,
                                          const char *author_hex,
                                          int limit,
                                          char ***results,
                                          uint16_t *count)
{
    if (!engine->initialized) return STORAGE_ERR_NOT_INITIALIZED;
    *results = NULL;
    *count = 0;
    if (limit > 500) limit = 500;
    if (limit <= 0) limit = 100;

    char **out = calloc(limit, sizeof(char *));
    if (!out) return STORAGE_ERR_NO_MEM;

    xSemaphoreTake(engine->lock, portMAX_DELAY);
    uint32_t now = (uint32_t)time(NULL);
    uint16_t found = 0;

    uint8_t author_prefix[4] = {0};
    int have_author = 0;
    if (author_hex && strlen(author_hex) >= 8) {
        extern int relay_hex_to_bytes(const char *, size_t, uint8_t *, size_t);
        relay_hex_to_bytes(author_hex, 8, author_prefix, 4);
        have_author = 1;
    }

    for (int i = engine->index_count - 1; i >= 0 && found < limit; i--) {
        storage_index_entry_t *e = &engine->index[i];
        if (e->flags & STORAGE_FLAG_DELETED) continue;
        if (e->expires_at > 0 && e->expires_at < now) continue;
        if (kind > 0 && e->kind != kind) continue;
        if (have_author && memcmp(e->pubkey_prefix, author_prefix, 4) != 0) continue;

        char path[128];
        get_event_path(e->event_id, e->file_index, path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > STORAGE_MAX_EVENT_SIZE) { fclose(f); continue; }
        char *buf = malloc(sz + 1);
        fread(buf, 1, sz, f);
        buf[sz] = '\0';
        fclose(f);
        out[found++] = buf;
    }

    xSemaphoreGive(engine->lock);
    *results = out;
    *count = found;
    return STORAGE_OK;
}

void storage_free_query_results(char **results, uint16_t count)
{
    if (!results) return;
    for (uint16_t i = 0; i < count; i++) free(results[i]);
    free(results);
}

storage_error_t storage_delete_event(storage_engine_t *engine, const uint8_t event_id[32])
{
    if (!engine->initialized) return STORAGE_ERR_NOT_INITIALIZED;
    xSemaphoreTake(engine->lock, portMAX_DELAY);
    storage_index_entry_t *e = find_index_entry(engine, event_id);
    if (!e) { xSemaphoreGive(engine->lock); return STORAGE_ERR_NOT_FOUND; }
    char path[128];
    get_event_path(e->event_id, e->file_index, path, sizeof(path));
    unlink(path);
    e->flags |= STORAGE_FLAG_DELETED;
    save_index_to_nvs(engine);
    xSemaphoreGive(engine->lock);
    return STORAGE_OK;
}

int storage_purge_expired(storage_engine_t *engine)
{
    if (!engine->initialized) return 0;
    xSemaphoreTake(engine->lock, portMAX_DELAY);
    uint32_t now = (uint32_t)time(NULL);
    int purged = 0;
    for (uint16_t i = 0; i < engine->index_count; i++) {
        if (engine->index[i].flags & STORAGE_FLAG_DELETED) continue;
        if (engine->index[i].expires_at > 0 && engine->index[i].expires_at < now) {
            char path[128];
            get_event_path(engine->index[i].event_id, engine->index[i].file_index, path, sizeof(path));
            unlink(path);
            engine->index[i].flags |= STORAGE_FLAG_DELETED;
            purged++;
        }
    }
    if (purged > 0) { save_index_to_nvs(engine); ESP_LOGI(TAG, "Purged %d expired events", purged); }
    xSemaphoreGive(engine->lock);
    return purged;
}

int storage_compact_index(storage_engine_t *engine)
{
    if (!engine->initialized) return 0;
    xSemaphoreTake(engine->lock, portMAX_DELAY);
    uint16_t write_idx = 0;
    int compacted = 0;
    for (uint16_t read_idx = 0; read_idx < engine->index_count; read_idx++) {
        if (!(engine->index[read_idx].flags & STORAGE_FLAG_DELETED)) {
            if (write_idx != read_idx)
                memcpy(&engine->index[write_idx], &engine->index[read_idx], sizeof(storage_index_entry_t));
            write_idx++;
        } else {
            compacted++;
        }
    }
    if (compacted > 0) {
        engine->index_count = write_idx;
        save_index_to_nvs(engine);
        ESP_LOGI(TAG, "Compacted: removed %d, %" PRIu16 " remaining", compacted, engine->index_count);
    }
    xSemaphoreGive(engine->lock);
    return compacted;
}

void storage_get_stats(storage_engine_t *engine, storage_stats_t *stats)
{
    memset(stats, 0, sizeof(storage_stats_t));
    if (!engine->initialized) return;
    xSemaphoreTake(engine->lock, portMAX_DELAY);
    uint32_t now = (uint32_t)time(NULL);
    for (uint16_t i = 0; i < engine->index_count; i++) {
        if (engine->index[i].flags & STORAGE_FLAG_DELETED) continue;
        if (engine->index[i].expires_at > 0 && engine->index[i].expires_at < now) continue;
        stats->total_events++;
    }
    size_t total, used;
    esp_littlefs_info(STORAGE_PARTITION_LABEL, &total, &used);
    stats->total_bytes = total;
    stats->free_bytes = total - used;
    xSemaphoreGive(engine->lock);
}

static void storage_cleanup_task(void *arg)
{
    storage_engine_t *engine = (storage_engine_t *)arg;
    int cycles = 0;
    while (!engine->cleanup_stop) {
        for (int i = 0; i < 60 && !engine->cleanup_stop; i++) vTaskDelay(pdMS_TO_TICKS(1000));
        if (engine->cleanup_stop) break;
        storage_purge_expired(engine);
        if (++cycles >= 10) { storage_compact_index(engine); cycles = 0; }
    }
    engine->cleanup_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t storage_start_cleanup_task(storage_engine_t *engine)
{
    engine->cleanup_stop = false;
    BaseType_t ret = xTaskCreate(storage_cleanup_task, "relay_cleanup", 4096, engine, 2, &engine->cleanup_task);
    if (ret != pdPASS) { engine->cleanup_task = NULL; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
