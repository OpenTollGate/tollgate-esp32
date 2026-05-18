#ifndef ROUTER_H
#define ROUTER_H

#include "relay_core.h"
#include <stdint.h>
#include <stddef.h>

esp_err_t router_send_notice(relay_ctx_t *ctx, int conn_fd, const char *message);
esp_err_t router_send_ok(relay_ctx_t *ctx, int conn_fd, const char *event_id_hex,
                         bool accepted, const char *message);
esp_err_t router_send_eose(relay_ctx_t *ctx, int conn_fd, const char *sub_id);
esp_err_t router_send_closed(relay_ctx_t *ctx, int conn_fd, const char *sub_id,
                             const char *message);
esp_err_t router_send_event(relay_ctx_t *ctx, int conn_fd, const char *sub_id,
                             const char *event_json, size_t event_len);

void router_dispatch(relay_ctx_t *ctx, int conn_fd, const char *data, size_t len);

#endif
