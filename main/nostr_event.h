#ifndef NOSTR_EVENT_H
#define NOSTR_EVENT_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char pubkey[65];
    uint64_t created_at;
    int kind;
    const char *tags_json;
    const char *content;
    char id[65];
    char sig[129];
} nostr_event_t;

esp_err_t nostr_event_init(nostr_event_t *event, const char *npub_hex,
                           int kind, const char *tags_json, const char *content);

esp_err_t nostr_event_sign(nostr_event_t *event, const uint8_t nsec[32]);

esp_err_t nostr_event_to_json(const nostr_event_t *event, char *buf, size_t buf_len);

#endif
