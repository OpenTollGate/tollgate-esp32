#include "tls_worker.h"
#include "nucula_wallet.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tls_worker";
static QueueHandle_t s_wallet_queue = NULL;

void tls_worker_set_queue(QueueHandle_t q)
{
    s_wallet_queue = q;
}

void tls_worker_submit(const char *token)
{
    if (!s_wallet_queue) {
        ESP_LOGW(TAG, "No wallet queue, receiving synchronously");
        nucula_wallet_receive(token);
        return;
    }

    char *copy = strdup(token);
    if (!copy) return;

    if (xQueueSend(s_wallet_queue, &copy, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Wallet queue full, receiving synchronously");
        nucula_wallet_receive(copy);
        free(copy);
    }
}
