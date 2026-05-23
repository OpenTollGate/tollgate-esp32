#ifndef TOLLGATE_CORE_PORTAL_H
#define TOLLGATE_CORE_PORTAL_H

#include "tollgate_core_session.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const char *key;
    const char *val;
} tollgate_portal_sub_t;

char *tollgate_core_portal_template_replace(const char *tpl, const char *key, const char *val);

char *tollgate_core_portal_render(const char *tpl,
                                   const tollgate_portal_sub_t *subs,
                                   int nsubs);

bool tollgate_core_portal_calc_usage(const tg_session_t *session,
                                      const char *metric,
                                      int64_t now_ms,
                                      int64_t *remaining_out,
                                      int64_t *total_out);

int tollgate_core_portal_format_usage(const tg_session_t *session,
                                       const char *metric,
                                       int64_t now_ms,
                                       char *buf,
                                       size_t buf_size);

bool tollgate_core_portal_is_captive_uri(const char *uri);

#endif
