#include "mcp_handler.h"
#include "config.h"
#include "nucula_wallet.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mcp_handler";

mcp_tool_t mcp_parse_tool(const char *method)
{
    if (!method) return MCP_TOOL_UNKNOWN;
    if (strcmp(method, "get_config") == 0) return MCP_TOOL_GET_CONFIG;
    if (strcmp(method, "set_config") == 0) return MCP_TOOL_SET_CONFIG;
    if (strcmp(method, "get_balance") == 0) return MCP_TOOL_GET_BALANCE;
    if (strcmp(method, "wallet_send") == 0) return MCP_TOOL_WALLET_SEND;
    return MCP_TOOL_UNKNOWN;
}

mcp_response_t mcp_handle_get_config(void)
{
    mcp_response_t resp = {0};
    const tollgate_config_t *cfg = tollgate_config_get();
    if (!cfg) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Config not loaded");
        return resp;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", cfg->ap_ssid);
    cJSON_AddStringToObject(root, "metric", cfg->metric);
    cJSON_AddNumberToObject(root, "price_per_step", cfg->price_per_step);
    cJSON_AddNumberToObject(root, "step_size_ms", cfg->step_size_ms);
    cJSON_AddNumberToObject(root, "step_size_bytes", cfg->step_size_bytes);
    cJSON_AddStringToObject(root, "mint_url", cfg->mint_url);
    cJSON_AddBoolToObject(root, "client_enabled", cfg->client_enabled);
    cJSON_AddBoolToObject(root, "payout_enabled", cfg->payout.enabled);
    cJSON_AddStringToObject(root, "wifi_ssid",
        cfg->network_count > 0 ? cfg->networks[0].ssid : "");

    char *json = cJSON_PrintUnformatted(root);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_handle_set_config(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    tollgate_config_t *cfg = (tollgate_config_t *)tollgate_config_get();
    if (!cfg) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Config not loaded");
        return resp;
    }

    cJSON *item;
    item = cJSON_GetObjectItem(root, "price_per_step");
    if (item && cJSON_IsNumber(item)) cfg->price_per_step = item->valueint;
    item = cJSON_GetObjectItem(root, "step_size_ms");
    if (item && cJSON_IsNumber(item)) cfg->step_size_ms = item->valueint;
    item = cJSON_GetObjectItem(root, "step_size_bytes");
    if (item && cJSON_IsNumber(item)) cfg->step_size_bytes = item->valueint;
    item = cJSON_GetObjectItem(root, "client_enabled");
    if (item && cJSON_IsBool(item)) cfg->client_enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(root, "payout_enabled");
    if (item && cJSON_IsBool(item)) cfg->payout.enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(root, "metric");
    if (item && cJSON_IsString(item)) {
        strncpy(cfg->metric, item->valuestring, sizeof(cfg->metric) - 1);
    }

    cJSON_Delete(root);
    resp.success = true;
    snprintf(resp.result_json, sizeof(resp.result_json), "{\"status\":\"ok\"}");
    return resp;
}

mcp_response_t mcp_handle_get_balance(void)
{
    mcp_response_t resp = {0};
    uint64_t balance = nucula_wallet_balance();
    int proof_count = nucula_wallet_proof_count();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "balance_sats", (double)balance);
    cJSON_AddNumberToObject(root, "proof_count", proof_count);

    char *json = cJSON_PrintUnformatted(root);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_handle_wallet_send(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    cJSON *amount_item = cJSON_GetObjectItem(root, "amount");
    if (!amount_item || !cJSON_IsNumber(amount_item)) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Missing 'amount' field");
        return resp;
    }

    uint64_t amount = (uint64_t)amount_item->valuedouble;
    char token_out[2048] = {0};
    int rc = nucula_wallet_send(amount, token_out, sizeof(token_out));

    if (rc != 0) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Send failed: %d", rc);
        return resp;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "token", token_out);
    cJSON_AddNumberToObject(result, "amount", (double)amount);
    char *json = cJSON_PrintUnformatted(result);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(result);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_dispatch(const mcp_request_t *req)
{
    if (!req) {
        mcp_response_t resp = {0};
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "NULL request");
        return resp;
    }

    switch (req->tool) {
        case MCP_TOOL_GET_CONFIG:
            return mcp_handle_get_config();
        case MCP_TOOL_SET_CONFIG:
            return mcp_handle_set_config(req->params_json);
        case MCP_TOOL_GET_BALANCE:
            return mcp_handle_get_balance();
        case MCP_TOOL_WALLET_SEND:
            return mcp_handle_wallet_send(req->params_json);
        default:
            break;
    }

    mcp_response_t resp = {0};
    resp.success = false;
    snprintf(resp.error, sizeof(resp.error), "Unknown tool: %s", req->method);
    return resp;
}
