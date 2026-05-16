#ifndef STUBS_ESP_SPIFFS_H
#define STUBS_ESP_SPIFFS_H

#include "esp_err.h"

typedef struct {
    const char *base_path;
    const char *partition_label;
    int max_files;
    bool format_if_mount_failed;
} esp_vfs_spiffs_conf_t;

static inline esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf) { (void)conf; return ESP_OK; }

#endif
