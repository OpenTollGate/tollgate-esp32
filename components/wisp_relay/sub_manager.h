#ifndef SUB_MANAGER_H
#define SUB_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "relay_types.h"

#define SUB_MAX_TOTAL         64
#define SUB_MAX_PER_CONN      8
#define SUB_MAX_FILTERS       4
#define SUB_MAX_ID_LEN        64

#define SUB_MAX_FILTER_IDS    20
#define SUB_MAX_FILTER_AUTHORS 20
#define SUB_MAX_FILTER_KINDS  20
#define SUB_MAX_FILTER_ETAGS  20
#define SUB_MAX_FILTER_PTAGS  20

typedef enum {
    SUB_OK = 0,
    SUB_ERR_INVALID,
    SUB_ERR_TOO_MANY_FILTERS,
    SUB_ERR_MEMORY,
    SUB_ERR_NOT_FOUND,
} sub_error_t;

typedef struct {
    char *ids[SUB_MAX_FILTER_IDS];
    size_t ids_count;
    char *authors[SUB_MAX_FILTER_AUTHORS];
    size_t authors_count;
    int32_t kinds[SUB_MAX_FILTER_KINDS];
    size_t kinds_count;
    char *e_tags[SUB_MAX_FILTER_ETAGS];
    size_t e_tags_count;
    char *p_tags[SUB_MAX_FILTER_PTAGS];
    size_t p_tags_count;
    int64_t since;
    int64_t until;
    int limit;
} sub_filter_t;

typedef struct {
    char sub_id[SUB_MAX_ID_LEN + 1];
    int conn_fd;
    sub_filter_t filters[SUB_MAX_FILTERS];
    uint8_t filter_count;
    uint16_t events_sent;
    bool active;
} subscription_t;

typedef struct sub_manager {
    subscription_t subs[SUB_MAX_TOTAL];
    SemaphoreHandle_t lock;
    uint16_t active_count;
} sub_manager_t;

typedef struct {
    int conn_fd;
    char sub_id[SUB_MAX_ID_LEN + 1];
} sub_match_entry_t;

typedef struct {
    sub_match_entry_t matches[SUB_MAX_TOTAL];
    uint8_t count;
} sub_match_result_t;

esp_err_t sub_manager_init(sub_manager_t *mgr);
void sub_manager_destroy(sub_manager_t *mgr);

sub_error_t sub_manager_add(sub_manager_t *mgr, int conn_fd,
                            const char *sub_id,
                            const sub_filter_t *filters,
                            size_t filter_count);

sub_error_t sub_manager_remove(sub_manager_t *mgr, int conn_fd,
                               const char *sub_id);

void sub_manager_remove_all(sub_manager_t *mgr, int conn_fd);

void sub_manager_match_json(sub_manager_t *mgr, const char *event_json,
                            size_t event_len, int event_kind,
                            const char *event_pubkey_hex,
                            uint64_t event_created_at,
                            sub_match_result_t *result);

uint8_t sub_manager_count(sub_manager_t *mgr, int conn_fd);

#endif
