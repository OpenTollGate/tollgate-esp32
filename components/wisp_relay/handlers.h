#ifndef HANDLERS_H
#define HANDLERS_H

#include "relay_core.h"

int handle_event(relay_ctx_t *ctx, int conn_fd, const char *event_json, size_t event_len);
void handle_req(relay_ctx_t *ctx, int conn_fd, const char *sub_id, const char *filters_json);
int handle_close(relay_ctx_t *ctx, int conn_fd, const char *sub_id);

#endif
