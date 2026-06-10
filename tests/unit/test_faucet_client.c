#include "test_framework.h"
#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static bool parse_faucet_response(const char *json, char *token_out, int token_out_size, int *amount_out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
    cJSON *token_item = cJSON_GetObjectItemCaseSensitive(root, "token");
    cJSON *amount_item = cJSON_GetObjectItemCaseSensitive(root, "amount");

    bool result = false;
    if (success && cJSON_IsTrue(success) && token_item && cJSON_IsString(token_item)) {
        strncpy(token_out, token_item->valuestring, token_out_size - 1);
        token_out[token_out_size - 1] = '\0';
        if (amount_item) *amount_out = amount_item->valueint;
        result = true;
    }

    cJSON_Delete(root);
    return result;
}

static bool parse_faucet_config(const char *json, char *faucet_url_out, int faucet_url_size, int *poll_interval_out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *mining = cJSON_GetObjectItem(root, "mining");
    if (!mining) { cJSON_Delete(root); return false; }

    cJSON *fu = cJSON_GetObjectItem(mining, "faucet_url");
    if (fu && cJSON_IsString(fu)) {
        strncpy(faucet_url_out, fu->valuestring, faucet_url_size - 1);
        faucet_url_out[faucet_url_size - 1] = '\0';
    }

    cJSON *fi = cJSON_GetObjectItem(mining, "faucet_poll_interval_s");
    if (fi) *poll_interval_out = fi->valueint;

    cJSON_Delete(root);
    return true;
}

int main(void)
{
    printf("=== test_faucet_client ===\n");

    printf("\n--- parse faucet response success ---\n");
    {
        char token[512] = {0};
        int amount = 0;
        bool ok = parse_faucet_response(
            "{\"amount\":32,\"success\":true,\"token\":\"cashuBo2FtdWh0dHA6Ly9sb2NhbGhvc3Q6MzMzOA\"}",
            token, sizeof(token), &amount);
        ASSERT(ok, "parse succeeds");
        ASSERT_EQ_STR("cashuBo2FtdWh0dHA6Ly9sb2NhbGhvc3Q6MzMzOA", token, "token parsed");
        ASSERT_EQ_INT(32, amount, "amount parsed");
    }

    printf("\n--- parse faucet response failure ---\n");
    {
        char token[512] = {0};
        int amount = 0;
        bool ok = parse_faucet_response(
            "{\"amount\":0,\"success\":false,\"error\":\"no tokens available\"}",
            token, sizeof(token), &amount);
        ASSERT(!ok, "parse returns false for failed response");
    }

    printf("\n--- parse faucet response missing token ---\n");
    {
        char token[512] = {0};
        int amount = 0;
        bool ok = parse_faucet_response(
            "{\"amount\":10,\"success\":true}",
            token, sizeof(token), &amount);
        ASSERT(!ok, "parse returns false when token missing");
    }

    printf("\n--- parse faucet response invalid json ---\n");
    {
        char token[512] = {0};
        int amount = 0;
        bool ok = parse_faucet_response("not json", token, sizeof(token), &amount);
        ASSERT(!ok, "parse returns false for invalid json");
    }

    printf("\n--- parse faucet config with url ---\n");
    {
        char url[256] = {0};
        int interval = 0;
        bool ok = parse_faucet_config(
            "{\"mining\":{\"enabled\":true,\"faucet_url\":\"http://66.92.204.38:8083/mint/tokens\",\"faucet_poll_interval_s\":90}}",
            url, sizeof(url), &interval);
        ASSERT(ok, "parse config succeeds");
        ASSERT_EQ_STR("http://66.92.204.38:8083/mint/tokens", url, "faucet_url parsed");
        ASSERT_EQ_INT(90, interval, "poll interval parsed");
    }

    printf("\n--- parse faucet config without faucet fields ---\n");
    {
        char url[256] = {0};
        int interval = 0;
        bool ok = parse_faucet_config(
            "{\"mining\":{\"enabled\":true}}",
            url, sizeof(url), &interval);
        ASSERT(ok, "parse config succeeds without faucet fields");
        ASSERT_EQ_STR("", url, "faucet_url empty when not specified");
        ASSERT_EQ_INT(0, interval, "poll interval 0 when not specified");
    }

    printf("\n--- parse faucet config no mining section ---\n");
    {
        char url[256] = {0};
        int interval = 0;
        bool ok = parse_faucet_config("{\"wifi\":{}}", url, sizeof(url), &interval);
        ASSERT(!ok, "parse returns false without mining section");
    }

    TEST_SUMMARY();
}
