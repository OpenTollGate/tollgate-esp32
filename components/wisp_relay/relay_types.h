#ifndef RELAY_TYPES_H
#define RELAY_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RELAY_MAX_EVENT_SIZE 8192
#define RELAY_ID_SIZE 32
#define RELAY_SIG_SIZE 64
#define RELAY_MAX_TAGS 100
#define RELAY_MAX_TAG_VALUES 10

typedef struct relay_event {
    uint8_t id[RELAY_ID_SIZE];
    uint8_t pubkey[RELAY_ID_SIZE];
    uint64_t created_at;
    int kind;
    uint8_t sig[RELAY_SIG_SIZE];
    char content[RELAY_MAX_EVENT_SIZE];
    size_t content_len;
} relay_event_t;

typedef struct {
    char **ids;
    size_t ids_count;
    char **authors;
    size_t authors_count;
    int32_t *kinds;
    size_t kinds_count;
    char **e_tags;
    size_t e_tags_count;
    char **p_tags;
    size_t p_tags_count;
    int64_t since;
    int64_t until;
    int limit;
} relay_filter_t;

int relay_hex_to_bytes(const char *hex, size_t hex_len, uint8_t *out, size_t out_len);
void relay_bytes_to_hex(const uint8_t *bytes, size_t len, char *hex);

#endif
