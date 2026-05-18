#ifndef RELAY_VALIDATOR_H
#define RELAY_VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    VALIDATION_OK = 0,
    VALIDATION_ERR_SCHEMA,
    VALIDATION_ERR_ID,
    VALIDATION_ERR_SIG,
    VALIDATION_ERR_EXPIRED,
    VALIDATION_ERR_FUTURE,
    VALIDATION_ERR_DUPLICATE,
    VALIDATION_ERR_POW,
    VALIDATION_ERR_BLOCKED,
    VALIDATION_ERR_TOO_OLD,
} validation_result_t;

typedef struct {
    uint32_t max_event_age_sec;
    int64_t max_future_sec;
    uint8_t min_pow_difficulty;
    bool check_duplicates;
} validator_config_t;

typedef struct relay_event relay_event_t;
typedef struct storage_engine storage_engine_t;

validation_result_t relay_validator_check(const uint8_t *id,
                                           const uint8_t *pubkey,
                                           uint64_t created_at,
                                           int kind,
                                           const char *content,
                                           size_t content_len,
                                           const char *tags_json,
                                           const uint8_t *sig,
                                           const validator_config_t *config);

bool relay_validator_verify_event(const char *event_json, size_t event_len);

const char *relay_validator_result_string(validation_result_t result);

#endif
