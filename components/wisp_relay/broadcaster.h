#ifndef BROADCASTER_H
#define BROADCASTER_H

#include "relay_core.h"

void broadcaster_fanout_json(relay_ctx_t *ctx, const char *event_json,
                             size_t event_len, int event_kind,
                             const char *event_pubkey_hex,
                             uint64_t event_created_at);

#endif
