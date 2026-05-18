#include "broadcaster.h"
#include "relay_core.h"
#include "router.h"
#include "sub_manager.h"
#include "esp_log.h"

static const char *TAG = "broadcaster";

void broadcaster_fanout_json(relay_ctx_t *ctx, const char *event_json,
                             size_t event_len, int event_kind,
                             const char *event_pubkey_hex,
                             uint64_t event_created_at)
{
    if (!ctx || !ctx->sub_manager) return;

    sub_match_result_t matches;
    sub_manager_match_json(ctx->sub_manager, event_json, event_len, event_kind,
                           event_pubkey_hex, event_created_at, &matches);

    if (matches.count == 0) {
        ESP_LOGD(TAG, "No subscribers for event kind=%d", event_kind);
        return;
    }

    ESP_LOGD(TAG, "Broadcasting event kind=%d to %d subscriptions",
             event_kind, matches.count);

    for (uint8_t i = 0; i < matches.count; i++) {
        sub_match_entry_t *entry = &matches.matches[i];
        router_send_event(ctx, entry->conn_fd, entry->sub_id,
                          event_json, event_len);
    }
}
