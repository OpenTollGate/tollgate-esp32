#include "tollgate_core_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

bool tollgate_core_client_parse_discovery(const char *json_str, tollgate_discovery_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    if (!kind || !cJSON_IsNumber(kind) || kind->valueint != 10021) {
        cJSON_Delete(root);
        return false;
    }

    memset(out, 0, sizeof(tollgate_discovery_t));
    out->is_tollgate = true;
    strncpy(out->unit, "sat", sizeof(out->unit) - 1);

    cJSON *tags = cJSON_GetObjectItemCaseSensitive(root, "tags");
    if (!tags || !cJSON_IsArray(tags)) {
        cJSON_Delete(root);
        return true;
    }

    int tag_count = cJSON_GetArraySize(tags);
    for (int i = 0; i < tag_count; i++) {
        cJSON *tag = cJSON_GetArrayItem(tags, i);
        if (!tag || !cJSON_IsArray(tag)) continue;

        int tag_len = cJSON_GetArraySize(tag);
        if (tag_len < 2) continue;

        cJSON *tag_name = cJSON_GetArrayItem(tag, 0);
        if (!tag_name || !cJSON_IsString(tag_name)) continue;

        if (strcmp(tag_name->valuestring, "metric") == 0) {
            cJSON *val = cJSON_GetArrayItem(tag, 1);
            if (val && cJSON_IsString(val)) {
                strncpy(out->metric, val->valuestring, sizeof(out->metric) - 1);
            }
        } else if (strcmp(tag_name->valuestring, "step_size") == 0) {
            cJSON *val = cJSON_GetArrayItem(tag, 1);
            if (val && cJSON_IsString(val)) {
                out->step_size_ms = atoi(val->valuestring);
            }
        } else if (strcmp(tag_name->valuestring, "price_per_step") == 0 && tag_len >= 3) {
            cJSON *payment_type = cJSON_GetArrayItem(tag, 1);

            if (cJSON_IsString(payment_type) && strcmp(payment_type->valuestring, "mining") == 0 && tag_len >= 3) {
                out->mining_available = true;
                cJSON *port_val = cJSON_GetArrayItem(tag, 2);
                if (port_val && cJSON_IsString(port_val)) {
                    out->mining_port = (uint16_t)atoi(port_val->valuestring);
                }
            } else if (cJSON_IsString(payment_type) && strcmp(payment_type->valuestring, "cashu") == 0 && tag_len >= 6) {
                cJSON *amount = cJSON_GetArrayItem(tag, 2);
                cJSON *unit_val = cJSON_GetArrayItem(tag, 3);
                cJSON *mint = cJSON_GetArrayItem(tag, 4);

                if (amount && cJSON_IsString(amount)) {
                    out->price_per_step = atoi(amount->valuestring);
                }
                if (unit_val && cJSON_IsString(unit_val)) {
                    strncpy(out->unit, unit_val->valuestring, sizeof(out->unit) - 1);
                }
                if (mint && cJSON_IsString(mint)) {
                    strncpy(out->mint_url, mint->valuestring, sizeof(out->mint_url) - 1);
                }
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

bool tollgate_core_client_parse_session(const char *json_str, int64_t *allotment_ms_out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    if (!kind || !cJSON_IsNumber(kind)) {
        cJSON_Delete(root);
        return false;
    }

    if (kind->valueint != 1022) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *tags = cJSON_GetObjectItemCaseSensitive(root, "tags");
    if (tags && cJSON_IsArray(tags)) {
        int tag_count = cJSON_GetArraySize(tags);
        for (int i = 0; i < tag_count; i++) {
            cJSON *tag = cJSON_GetArrayItem(tags, i);
            if (!tag || !cJSON_IsArray(tag)) continue;
            cJSON *tag_name = cJSON_GetArrayItem(tag, 0);
            if (tag_name && cJSON_IsString(tag_name) && strcmp(tag_name->valuestring, "allotment") == 0) {
                cJSON *val = cJSON_GetArrayItem(tag, 1);
                if (val && cJSON_IsString(val)) {
                    *allotment_ms_out = atoll(val->valuestring);
                }
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

bool tollgate_core_client_parse_usage(const char *resp, int64_t *remaining_out, int64_t *total_out)
{
    char remaining_str[32] = {0};
    char total_str[32] = {0};
    const char *slash = strchr(resp, '/');
    if (!slash) return false;

    size_t rlen = slash - resp;
    if (rlen >= sizeof(remaining_str)) return false;
    memcpy(remaining_str, resp, rlen);
    strncpy(total_str, slash + 1, sizeof(total_str) - 1);

    *remaining_out = atoll(remaining_str);
    *total_out = atoll(total_str);
    return true;
}

bool tollgate_core_client_should_renew(int64_t remaining_ms, int64_t allotment_ms, int threshold_pct)
{
    if (threshold_pct <= 0) threshold_pct = 20;
    if (allotment_ms <= 0 || remaining_ms < 0) return false;

    int remaining_pct = (int)((remaining_ms * 100) / allotment_ms);
    return remaining_pct <= threshold_pct;
}

int tollgate_core_client_calc_price_per_min(int price_per_step, int step_size_ms)
{
    if (step_size_ms <= 0) step_size_ms = 1;
    return price_per_step * 60000 / step_size_ms;
}

int tollgate_core_client_calc_steps(int steps_to_buy, int price_per_step, int discovery_price)
{
    if (steps_to_buy <= 0) steps_to_buy = 1;
    return steps_to_buy * discovery_price;
}
