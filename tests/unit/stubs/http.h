#ifndef STUBS_HTTP_H
#define STUBS_HTTP_H

#include "esp_err.h"
#include <stddef.h>

typedef struct {
    int status;
    char *body;
    size_t body_len;
} http_response_t;

static inline esp_err_t http_get(const char *url, http_response_t *resp)
{
    (void)url; (void)resp;
    return ESP_FAIL;
}

static inline esp_err_t http_post_json(const char *url, const char *json_body,
                                        http_response_t *resp)
{
    (void)url; (void)json_body; (void)resp;
    return ESP_FAIL;
}

static inline esp_err_t http_post_json_timeout(const char *url, const char *json_body,
                                                http_response_t *resp, int timeout_ms)
{
    (void)url; (void)json_body; (void)resp; (void)timeout_ms;
    return ESP_FAIL;
}

static inline void http_response_free(http_response_t *resp)
{
    if (resp && resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
}

#endif
