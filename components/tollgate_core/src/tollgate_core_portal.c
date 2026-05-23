#include "tollgate_core_portal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *CAPTIVE_URIS[] = {
    "/generate_204",
    "/hotspot-detect.html",
    "/canonical.html",
    "/success.txt",
    "/ncsi.txt",
    "/connecttest.txt",
    "/wpad.dat",
    NULL
};

char *tollgate_core_portal_template_replace(const char *tpl, const char *key, const char *val)
{
    const char *p;
    size_t klen = strlen(key);
    size_t vlen = strlen(val);
    size_t tlen = strlen(tpl);
    size_t extra = 0;

    p = tpl;
    while ((p = strstr(p, key)) != NULL) {
        extra += vlen - klen;
        p += klen;
    }

    size_t out_size = tlen + extra + 1;
    char *out = malloc(out_size);
    if (!out) return NULL;

    char *dst = out;
    p = tpl;
    while (*p) {
        const char *found = strstr(p, key);
        if (found) {
            memcpy(dst, p, found - p);
            dst += found - p;
            memcpy(dst, val, vlen);
            dst += vlen;
            p = found + klen;
        } else {
            strcpy(dst, p);
            dst += strlen(p);
            break;
        }
    }
    *dst = '\0';
    return out;
}

char *tollgate_core_portal_render(const char *tpl,
                                   const tollgate_portal_sub_t *subs,
                                   int nsubs)
{
    size_t tpl_len = strlen(tpl);

    size_t extra = 0;
    for (int i = 0; i < nsubs; i++) {
        const char *p = tpl;
        size_t klen = strlen(subs[i].key);
        while ((p = strstr(p, subs[i].key)) != NULL) {
            extra += strlen(subs[i].val) - klen;
            p += klen;
        }
    }

    size_t out_size = tpl_len + extra + 1;
    char *html = malloc(out_size);
    if (!html) return NULL;

    char *out = html;
    const char *src = tpl;
    while (*src) {
        const char *earliest = NULL;
        int ei = -1;
        for (int i = 0; i < nsubs; i++) {
            const char *found = strstr(src, subs[i].key);
            if (found && (earliest == NULL || found < earliest)) {
                earliest = found;
                ei = i;
            }
        }
        if (earliest) {
            size_t vlen = strlen(subs[ei].val);
            memcpy(out, src, earliest - src);
            out += earliest - src;
            memcpy(out, subs[ei].val, vlen);
            out += vlen;
            src = earliest + strlen(subs[ei].key);
        } else {
            strcpy(out, src);
            out += strlen(src);
            break;
        }
    }
    *out = '\0';
    return html;
}

bool tollgate_core_portal_calc_usage(const tg_session_t *session,
                                      const char *metric,
                                      int64_t now_ms,
                                      int64_t *remaining_out,
                                      int64_t *total_out)
{
    if (!session || !session->active) return false;

    bool is_bytes = (strcmp(metric, "bytes") == 0);

    if (is_bytes) {
        int64_t remaining = (int64_t)session->allotment_bytes - (int64_t)session->bytes_consumed;
        if (remaining < 0) remaining = 0;
        *remaining_out = remaining;
        *total_out = (int64_t)session->allotment_bytes;
    } else {
        int64_t elapsed = now_ms - session->start_time_ms;
        int64_t remaining = (int64_t)session->allotment_ms - elapsed;
        if (remaining < 0) remaining = 0;
        *remaining_out = remaining;
        *total_out = (int64_t)session->allotment_ms;
    }
    return true;
}

int tollgate_core_portal_format_usage(const tg_session_t *session,
                                       const char *metric,
                                       int64_t now_ms,
                                       char *buf,
                                       size_t buf_size)
{
    int64_t remaining = 0, total = 0;
    if (!tollgate_core_portal_calc_usage(session, metric, now_ms, &remaining, &total)) {
        return snprintf(buf, buf_size, "-1/-1");
    }
    return snprintf(buf, buf_size, "%lld/%llu", (long long)remaining, (unsigned long long)total);
}

bool tollgate_core_portal_is_captive_uri(const char *uri)
{
    if (!uri) return false;
    for (int i = 0; CAPTIVE_URIS[i] != NULL; i++) {
        if (strcmp(uri, CAPTIVE_URIS[i]) == 0) return true;
    }
    return false;
}
