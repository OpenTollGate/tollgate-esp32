#include "test_framework.h"
#include "mcp_handler.h"
#include "config.h"
#include "nucula_wallet.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static tollgate_config_t g_test_config;
static uint64_t g_wallet_balance = 0;
static int g_wallet_proof_count = 0;
static int g_wallet_send_rc = 0;
static char g_wallet_send_token[256] = "cashuA_test_token";

const tollgate_config_t *tollgate_config_get(void) {
    return &g_test_config;
}

uint64_t nucula_wallet_balance(void) {
    return g_wallet_balance;
}

int nucula_wallet_proof_count(void) {
    return g_wallet_proof_count;
}

int nucula_wallet_send(uint64_t amount, char *token_out, size_t token_max) {
    (void)amount;
    (void)token_max;
    if (g_wallet_send_rc == 0) {
        strncpy(token_out, g_wallet_send_token, token_max - 1);
    }
    return g_wallet_send_rc;
}

static void test_mcp_parse_tool(void)
{
    printf("\n=== MCP tool parsing ===\n");
    ASSERT_EQ_INT(MCP_TOOL_GET_CONFIG, mcp_parse_tool("get_config"), "get_config");
    ASSERT_EQ_INT(MCP_TOOL_SET_CONFIG, mcp_parse_tool("set_config"), "set_config");
    ASSERT_EQ_INT(MCP_TOOL_GET_BALANCE, mcp_parse_tool("get_balance"), "get_balance");
    ASSERT_EQ_INT(MCP_TOOL_WALLET_SEND, mcp_parse_tool("wallet_send"), "wallet_send");
    ASSERT_EQ_INT(MCP_TOOL_UNKNOWN, mcp_parse_tool("foo"), "unknown tool");
    ASSERT_EQ_INT(MCP_TOOL_UNKNOWN, mcp_parse_tool(NULL), "NULL tool");
}

static void test_mcp_get_config(void)
{
    printf("\n=== MCP get_config ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.ap_ssid, "TollGate-TEST", sizeof(g_test_config.ap_ssid) - 1);
    strncpy(g_test_config.metric, "bytes", sizeof(g_test_config.metric) - 1);
    g_test_config.price_per_step = 21;
    g_test_config.step_size_ms = 60000;
    g_test_config.step_size_bytes = 22020096;
    strncpy(g_test_config.mint_url, "https://testnut.cashu.space", sizeof(g_test_config.mint_url) - 1);

    mcp_response_t resp = mcp_handle_get_config();
    ASSERT(resp.success, "get_config succeeds");

    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT_EQ_STR("bytes", cJSON_GetObjectItem(result, "metric")->valuestring, "metric=bytes");
    ASSERT_EQ_INT(21, cJSON_GetObjectItem(result, "price_per_step")->valueint, "price=21");
    cJSON_Delete(result);
}

static void test_mcp_set_config(void)
{
    printf("\n=== MCP set_config ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    g_test_config.price_per_step = 21;

    const char *params = "{\"price_per_step\":42,\"metric\":\"milliseconds\"}";
    mcp_response_t resp = mcp_handle_set_config(params);
    ASSERT(resp.success, "set_config succeeds");
    ASSERT_EQ_INT(42, g_test_config.price_per_step, "price updated to 42");
    ASSERT_EQ_STR("milliseconds", g_test_config.metric, "metric updated");

    resp = mcp_handle_set_config("not json");
    ASSERT(!resp.success, "invalid JSON fails");
}

static void test_mcp_get_balance(void)
{
    printf("\n=== MCP get_balance ===\n");
    g_wallet_balance = 500;
    g_wallet_proof_count = 8;

    mcp_response_t resp = mcp_handle_get_balance();
    ASSERT(resp.success, "get_balance succeeds");

    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT_EQ_INT(500, (int)cJSON_GetObjectItem(result, "balance_sats")->valuedouble, "balance=500");
    ASSERT_EQ_INT(8, cJSON_GetObjectItem(result, "proof_count")->valueint, "proofs=8");
    cJSON_Delete(result);
}

static void test_mcp_wallet_send(void)
{
    printf("\n=== MCP wallet_send ===\n");
    g_wallet_send_rc = 0;
    strncpy(g_wallet_send_token, "cashuA_send_test", sizeof(g_wallet_send_token) - 1);

    const char *params = "{\"amount\":21}";
    mcp_response_t resp = mcp_handle_wallet_send(params);
    ASSERT(resp.success, "wallet_send succeeds");

    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT_EQ_STR("cashuA_send_test", cJSON_GetObjectItem(result, "token")->valuestring, "token matches");
    cJSON_Delete(result);

    printf("\n--- wallet_send missing amount ---\n");
    resp = mcp_handle_wallet_send("{}");
    ASSERT(!resp.success, "missing amount fails");

    printf("\n--- wallet_send send fails ---\n");
    g_wallet_send_rc = -1;
    resp = mcp_handle_wallet_send("{\"amount\":100}");
    ASSERT(!resp.success, "send failure reported");
}

static void test_mcp_dispatch(void)
{
    printf("\n=== MCP dispatch ===\n");
    mcp_request_t req = {0};
    req.tool = MCP_TOOL_UNKNOWN;
    strncpy(req.method, "bogus", sizeof(req.method) - 1);
    mcp_response_t resp = mcp_dispatch(&req);
    ASSERT(!resp.success, "unknown tool dispatch fails");

    resp = mcp_dispatch(NULL);
    ASSERT(!resp.success, "NULL request dispatch fails");
}

int main(void)
{
    printf("=== test_mcp_handler ===\n");
    test_mcp_parse_tool();
    test_mcp_get_config();
    test_mcp_set_config();
    test_mcp_get_balance();
    test_mcp_wallet_send();
    test_mcp_dispatch();
    TEST_SUMMARY();
}
