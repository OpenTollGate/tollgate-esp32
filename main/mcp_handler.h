#ifndef MCP_HANDLER_H
#define MCP_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MCP_TOOL_GET_CONFIG = 0,
    MCP_TOOL_SET_CONFIG = 1,
    MCP_TOOL_GET_BALANCE = 2,
    MCP_TOOL_WALLET_SEND = 3,
    MCP_TOOL_UNKNOWN = 99
} mcp_tool_t;

typedef struct {
    mcp_tool_t tool;
    char method[64];
    char params_json[1024];
} mcp_request_t;

typedef struct {
    bool success;
    char result_json[2048];
    char error[256];
} mcp_response_t;

mcp_tool_t mcp_parse_tool(const char *method);

mcp_response_t mcp_handle_get_config(void);
mcp_response_t mcp_handle_set_config(const char *params_json);
mcp_response_t mcp_handle_get_balance(void);
mcp_response_t mcp_handle_wallet_send(const char *params_json);

mcp_response_t mcp_dispatch(const mcp_request_t *req);

#endif
