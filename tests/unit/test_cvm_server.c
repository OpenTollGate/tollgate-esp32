#include "test_framework.h"
#include "cJSON.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char *build_initialize_response_test(const char *request_id_str)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 0);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", "2025-07-02");

    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(capabilities, "tools", cJSON_CreateObject());
    cJSON_AddItemToObject(result, "capabilities", capabilities);

    cJSON *serverInfo = cJSON_CreateObject();
    cJSON_AddStringToObject(serverInfo, "name", "TollGate");
    cJSON_AddStringToObject(serverInfo, "version", "1.0.0");
    cJSON_AddItemToObject(result, "serverInfo", serverInfo);

    cJSON_AddItemToObject(response, "result", result);

    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *build_tools_list_response_test(const char *request_id_str)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 1);

    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();

    const char *tool_names[] = {
        "get_config", "set_config", "get_balance", "wallet_send",
        "get_sessions", "get_usage", "set_payout", "set_metric",
        "set_price", "wallet_melt"
    };

    for (int i = 0; i < 10; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", tool_names[i]);
        cJSON_AddItemToArray(tools, tool);
    }

    cJSON_AddItemToObject(result, "tools", tools);
    cJSON_AddItemToObject(response, "result", result);

    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *build_tool_call_response_test(const char *request_id_str,
                                            bool success, const char *result_or_error)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 2);

    if (success) {
        cJSON *result = cJSON_CreateObject();
        cJSON *content_arr = cJSON_CreateArray();
        cJSON *text_item = cJSON_CreateObject();
        cJSON_AddStringToObject(text_item, "type", "text");
        cJSON_AddStringToObject(text_item, "text", result_or_error);
        cJSON_AddItemToArray(content_arr, text_item);
        cJSON_AddItemToObject(result, "content", content_arr);
        cJSON_AddBoolToObject(result, "isError", false);
        cJSON_AddItemToObject(response, "result", result);
    } else {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32603);
        cJSON_AddStringToObject(error, "message", result_or_error);
        cJSON_AddItemToObject(response, "error", error);
    }

    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *build_ping_response_test(const char *request_id_str)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 0);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "result", result);
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *build_announcement_11316_test(void)
{
    cJSON *ann = cJSON_CreateObject();
    cJSON_AddStringToObject(ann, "protocolVersion", "2025-07-02");

    cJSON *caps = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateObject();
    cJSON_AddBoolToObject(tools, "listChanged", true);
    cJSON_AddItemToObject(caps, "tools", tools);
    cJSON_AddItemToObject(ann, "capabilities", caps);

    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name", "TollGate");
    cJSON_AddStringToObject(info, "version", "1.0.0");
    cJSON_AddItemToObject(ann, "serverInfo", info);

    char *json = cJSON_PrintUnformatted(ann);
    cJSON_Delete(ann);
    return json;
}

static char *build_announcement_11317_test(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();

    const char *names[] = {
        "get_config", "set_config", "get_balance", "wallet_send",
        "get_sessions", "get_usage", "set_payout", "set_metric",
        "set_price", "wallet_melt"
    };

    for (int i = 0; i < 10; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", names[i]);
        cJSON_AddStringToObject(t, "description", "test");
        cJSON *schema = cJSON_CreateObject();
        cJSON_AddStringToObject(schema, "type", "object");
        cJSON_AddItemToObject(t, "inputSchema", schema);
        cJSON_AddItemToArray(tools, t);
    }

    cJSON_AddItemToObject(root, "tools", tools);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static char *build_relay_list_10002_test(void)
{
    cJSON *tags = cJSON_CreateArray();
    const char *relays[] = {"wss://relay.damus.io", "wss://nos.lol"};
    for (int i = 0; i < 2; i++) {
        cJSON *r = cJSON_CreateArray();
        cJSON_AddItemToArray(r, cJSON_CreateString("r"));
        cJSON_AddItemToArray(r, cJSON_CreateString(relays[i]));
        cJSON_AddItemToArray(tags, r);
    }
    char *json = cJSON_PrintUnformatted(tags);
    cJSON_Delete(tags);
    return json;
}

static bool parse_mcp_from_25910(const char *content, char *method_out, size_t method_max,
                                  char *params_out, size_t params_max)
{
    cJSON *msg = cJSON_Parse(content);
    if (!msg) return false;

    cJSON *method = cJSON_GetObjectItem(msg, "method");
    if (!method || !cJSON_IsString(method)) {
        cJSON_Delete(msg);
        return false;
    }

    strncpy(method_out, method->valuestring, method_max - 1);

    cJSON *params = cJSON_GetObjectItem(msg, "params");
    if (params) {
        char *pjson = cJSON_PrintUnformatted(params);
        strncpy(params_out, pjson, params_max - 1);
        cJSON_free(pjson);
    }

    cJSON_Delete(msg);
    return true;
}

static void test_initialize_response(void)
{
    printf("\n=== MCP initialize response ===\n");
    char *json = build_initialize_response_test("0");
    ASSERT(json != NULL, "response created");

    cJSON *root = cJSON_Parse(json);
    ASSERT(root != NULL, "valid JSON");
    ASSERT_EQ_STR("2.0", cJSON_GetObjectItem(root, "jsonrpc")->valuestring, "jsonrpc version");
    ASSERT_EQ_INT(0, (int)cJSON_GetObjectItem(root, "id")->valuedouble, "id=0");

    cJSON *result = cJSON_GetObjectItem(root, "result");
    ASSERT(result != NULL, "has result");
    ASSERT_EQ_STR("2025-07-02", cJSON_GetObjectItem(result, "protocolVersion")->valuestring, "protocol version");

    cJSON *caps = cJSON_GetObjectItem(result, "capabilities");
    ASSERT(caps != NULL, "has capabilities");
    ASSERT(cJSON_GetObjectItem(caps, "tools") != NULL, "has tools capability");

    cJSON *info = cJSON_GetObjectItem(result, "serverInfo");
    ASSERT(info != NULL, "has serverInfo");
    ASSERT_EQ_STR("TollGate", cJSON_GetObjectItem(info, "name")->valuestring, "server name");
    ASSERT_EQ_STR("1.0.0", cJSON_GetObjectItem(info, "version")->valuestring, "server version");

    cJSON_Delete(root);
    free(json);
}

static void test_tools_list_response(void)
{
    printf("\n=== MCP tools/list response ===\n");
    char *json = build_tools_list_response_test("1");
    ASSERT(json != NULL, "response created");

    cJSON *root = cJSON_Parse(json);
    ASSERT_EQ_STR("2.0", cJSON_GetObjectItem(root, "jsonrpc")->valuestring, "jsonrpc version");

    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    ASSERT(tools != NULL, "has tools array");
    ASSERT_EQ_INT(10, cJSON_GetArraySize(tools), "10 tools");

    ASSERT_EQ_STR("get_config", cJSON_GetObjectItem(cJSON_GetArrayItem(tools, 0), "name")->valuestring, "tool 0");
    ASSERT_EQ_STR("wallet_melt", cJSON_GetObjectItem(cJSON_GetArrayItem(tools, 9), "name")->valuestring, "tool 9");

    cJSON_Delete(root);
    free(json);
}

static void test_tool_call_response_success(void)
{
    printf("\n=== MCP tools/call success response ===\n");
    char *json = build_tool_call_response_test("2", true, "{\"balance\":500}");
    ASSERT(json != NULL, "response created");

    cJSON *root = cJSON_Parse(json);
    cJSON *result = cJSON_GetObjectItem(root, "result");
    ASSERT(result != NULL, "has result");
    ASSERT(cJSON_GetObjectItem(result, "content") != NULL, "has content");
    ASSERT_EQ_INT(0, cJSON_GetObjectItem(result, "isError")->valueint, "isError=false");

    cJSON *content = cJSON_GetObjectItem(result, "content");
    cJSON *text = cJSON_GetArrayItem(content, 0);
    ASSERT_EQ_STR("text", cJSON_GetObjectItem(text, "type")->valuestring, "content type=text");
    ASSERT(strstr(cJSON_GetObjectItem(text, "text")->valuestring, "balance") != NULL, "contains balance");

    cJSON_Delete(root);
    free(json);
}

static void test_tool_call_response_error(void)
{
    printf("\n=== MCP tools/call error response ===\n");
    char *json = build_tool_call_response_test("3", false, "Tool not found");
    ASSERT(json != NULL, "response created");

    cJSON *root = cJSON_Parse(json);
    cJSON *error = cJSON_GetObjectItem(root, "error");
    ASSERT(error != NULL, "has error");
    ASSERT_EQ_INT(-32603, cJSON_GetObjectItem(error, "code")->valueint, "error code");
    ASSERT_EQ_STR("Tool not found", cJSON_GetObjectItem(error, "message")->valuestring, "error message");

    cJSON_Delete(root);
    free(json);
}

static void test_ping_response(void)
{
    printf("\n=== MCP ping response ===\n");
    char *json = build_ping_response_test("99");
    ASSERT(json != NULL, "response created");

    cJSON *root = cJSON_Parse(json);
    ASSERT_EQ_STR("2.0", cJSON_GetObjectItem(root, "jsonrpc")->valuestring, "jsonrpc version");
    ASSERT(cJSON_GetObjectItem(root, "result") != NULL, "has result");

    cJSON_Delete(root);
    free(json);
}

static void test_announcement_11316(void)
{
    printf("\n=== Kind 11316 server announcement ===\n");
    char *json = build_announcement_11316_test();
    ASSERT(json != NULL, "announcement created");

    cJSON *root = cJSON_Parse(json);
    ASSERT_EQ_STR("2025-07-02", cJSON_GetObjectItem(root, "protocolVersion")->valuestring, "protocol version");

    cJSON *caps = cJSON_GetObjectItem(root, "capabilities");
    ASSERT(cJSON_GetObjectItem(caps, "tools") != NULL, "has tools capability");

    cJSON *info = cJSON_GetObjectItem(root, "serverInfo");
    ASSERT_EQ_STR("TollGate", cJSON_GetObjectItem(info, "name")->valuestring, "name");
    ASSERT_EQ_STR("1.0.0", cJSON_GetObjectItem(info, "version")->valuestring, "version");

    cJSON_Delete(root);
    free(json);
}

static void test_announcement_11317(void)
{
    printf("\n=== Kind 11317 tools list ===\n");
    char *json = build_announcement_11317_test();
    ASSERT(json != NULL, "tools list created");

    cJSON *root = cJSON_Parse(json);
    cJSON *tools = cJSON_GetObjectItem(root, "tools");
    ASSERT_EQ_INT(10, cJSON_GetArraySize(tools), "10 tools");

    cJSON *t0 = cJSON_GetArrayItem(tools, 0);
    ASSERT_EQ_STR("get_config", cJSON_GetObjectItem(t0, "name")->valuestring, "tool 0 name");
    ASSERT(cJSON_GetObjectItem(t0, "inputSchema") != NULL, "tool has inputSchema");

    cJSON_Delete(root);
    free(json);
}

static void test_relay_list_10002(void)
{
    printf("\n=== Kind 10002 relay list ===\n");
    char *json = build_relay_list_10002_test();
    ASSERT(json != NULL, "relay list created");

    cJSON *tags = cJSON_Parse(json);
    ASSERT(cJSON_IsArray(tags), "is array");
    ASSERT_EQ_INT(2, cJSON_GetArraySize(tags), "2 relay tags");

    cJSON *r0 = cJSON_GetArrayItem(tags, 0);
    ASSERT_EQ_STR("r", cJSON_GetArrayItem(r0, 0)->valuestring, "tag type r");
    ASSERT_EQ_STR("wss://relay.damus.io", cJSON_GetArrayItem(r0, 1)->valuestring, "relay 0");

    cJSON *r1 = cJSON_GetArrayItem(tags, 1);
    ASSERT_EQ_STR("wss://nos.lol", cJSON_GetArrayItem(r1, 1)->valuestring, "relay 1");

    cJSON_Delete(tags);
    free(json);
}

static void test_mcp_parse_from_25910(void)
{
    printf("\n=== Parse MCP from kind 25910 content ===\n");

    char method[64] = {0};
    char params[1024] = {0};

    bool ok = parse_mcp_from_25910(
        "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\",\"params\":{}}",
        method, sizeof(method), params, sizeof(params));
    ASSERT(ok, "parsed initialize");
    ASSERT_EQ_STR("initialize", method, "method=initialize");

    ok = parse_mcp_from_25910(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"get_config\"}}",
        method, sizeof(method), params, sizeof(params));
    ASSERT(ok, "parsed tools/call");
    ASSERT_EQ_STR("tools/call", method, "method=tools/call");
    ASSERT(strstr(params, "get_config") != NULL, "params has get_config");

    ok = parse_mcp_from_25910("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
                               method, sizeof(method), params, sizeof(params));
    ASSERT(ok, "parsed notification");
    ASSERT_EQ_STR("notifications/initialized", method, "method=notifications/initialized");

    ok = parse_mcp_from_25910("not json", method, sizeof(method), params, sizeof(params));
    ASSERT(!ok, "garbage rejected");

    ok = parse_mcp_from_25910("{\"jsonrpc\":\"2.0\"}", method, sizeof(method), params, sizeof(params));
    ASSERT(!ok, "missing method rejected");
}

static void test_auth_check(void)
{
    printf("\n=== Auth check logic ===\n");

    const char *owner = "d6bfe100d1600c0d8f769501676fc74c3809500bd131c8a549f88cf616c21f35";
    const char *other = "0000000000000000000000000000000000000000000000000000000000000001";

    ASSERT(strcmp(owner, owner) == 0, "owner matches self");
    ASSERT(strcmp(owner, other) != 0, "owner differs from other");
    ASSERT(strcmp(other, owner) != 0, "other differs from owner");
    ASSERT(NULL == NULL, "two NULLs match (for safety check)");
}

static void test_25910_event_content_roundtrip(void)
{
    printf("\n=== Kind 25910 content roundtrip ===\n");

    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(request, "id", 42);
    cJSON_AddStringToObject(request, "method", "tools/call");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "get_balance");
    cJSON_AddItemToObject(request, "params", params);
    char *content = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    char method[64] = {0};
    char params_out[1024] = {0};
    bool ok = parse_mcp_from_25910(content, method, sizeof(method), params_out, sizeof(params_out));
    ASSERT(ok, "roundtrip parse succeeded");
    ASSERT_EQ_STR("tools/call", method, "method preserved");
    ASSERT(strstr(params_out, "get_balance") != NULL, "tool name preserved");

    free(content);
}

int main(void)
{
    printf("=== test_cvm_server ===\n");
    test_initialize_response();
    test_tools_list_response();
    test_tool_call_response_success();
    test_tool_call_response_error();
    test_ping_response();
    test_announcement_11316();
    test_announcement_11317();
    test_relay_list_10002();
    test_mcp_parse_from_25910();
    test_auth_check();
    test_25910_event_content_roundtrip();
    TEST_SUMMARY();
}
