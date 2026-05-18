#ifndef SYNC_MANAGER_H
#define SYNC_MANAGER_H

#include "esp_err.h"
#include "relay_selector.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>

typedef struct {
    relay_selector_t *selector;
    bool running;
    bool sync_in_progress;
    uint32_t last_negentropy_sync;
    uint32_t last_reqdiff_sync;
    SemaphoreHandle_t lock;
} sync_manager_t;

esp_err_t sync_manager_init(sync_manager_t *mgr, relay_selector_t *selector);
void sync_manager_start(sync_manager_t *mgr);
void sync_manager_stop(sync_manager_t *mgr);

esp_err_t sync_manager_do_negentropy_sync(sync_manager_t *mgr);
esp_err_t sync_manager_do_reqdiff_sync(sync_manager_t *mgr);

#endif
