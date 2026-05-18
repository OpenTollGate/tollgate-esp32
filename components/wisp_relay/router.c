#include "router.h"
#include "ws_server.h"
#include "handlers.h"
#include "sub_manager.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "router";

esp_err_t router_send_notice(relay_ctx_t *ctx, int conn_fd, const char *message)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("NOTICE"));
    cJSON_AddItemToArray(arr, cJSON_CreateString(message));
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    esp_err_t ret = ws_server_send(&ctx->ws_server, conn_fd, json, strlen(json));
    cJSON_free(json);
    return ret;
}

esp_err_t router_send_ok(relay_ctx_t *ctx, int conn_fd, const char *event_id_hex,
                         bool accepted, const char *message)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("OK"));
    cJSON_AddItemToArray(arr, cJSON_CreateString(event_id_hex));
    cJSON_AddItemToArray(arr, cJSON_CreateBool(accepted));
    cJSON_AddItemToArray(arr, cJSON_CreateString(message ? message : ""));
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    esp_err_t ret = ws_server_send(&ctx->ws_server, conn_fd, json, strlen(json));
    cJSON_free(json);
    return ret;
}

esp_err_t router_send_eose(relay_ctx_t *ctx, int conn_fd, const char *sub_id)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("EOSE"));
    cJSON_AddItemToArray(arr, cJSON_CreateString(sub_id));
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    esp_err_t ret = ws_server_send(&ctx->ws_server, conn_fd, json, strlen(json));
    cJSON_free(json);
    return ret;
}

esp_err_t router_send_closed(relay_ctx_t *ctx, int conn_fd, const char *sub_id,
                             const char *message)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("CLOSED"));
    cJSON_AddItemToArray(arr, cJSON_CreateString(sub_id));
    cJSON_AddItemToArray(arr, cJSON_CreateString(message ? message : ""));
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    esp_err_t ret = ws_server_send(&ctx->ws_server, conn_fd, json, strlen(json));
    cJSON_free(json);
    return ret;
}

esp_err_t router_send_event(relay_ctx_t *ctx, int conn_fd, const char *sub_id,
                             const char *event_json, size_t event_len)
{
    size_t buf_size = event_len + strlen(sub_id) + 32;
    char *buf = malloc(buf_size);
    if (!buf) return ESP_ERR_NO_MEM;
    int n = snprintf(buf, buf_size, "[\"EVENT\",\"%s\",%.*s]", sub_id, (int)event_len, event_json);
    esp_err_t ret = ws_server_send(&ctx->ws_server, conn_fd, buf, n);
    free(buf);
    return ret;
}

static void on_ws_message(int fd, const char *data, size_t len)
{
    extern relay_ctx_t g_relay_ctx;
    router_dispatch(&g_relay_ctx, fd, data, len);
}

static void on_ws_disconnect(int fd)
{
    extern relay_ctx_t g_relay_ctx;
    if (g_relay_ctx.sub_manager) {
        sub_manager_remove_all(g_relay_ctx.sub_manager, fd);
    }
}

void router_dispatch(relay_ctx_t *ctx, int conn_fd, const char *data, size_t len)
{
    cJSON *arr = cJSON_ParseWithLength(data, len);
    if (!arr || !cJSON_IsArray(arr)) {
        router_send_notice(ctx, conn_fd, "invalid JSON");
        if (arr) cJSON_Delete(arr);
        return;
    }

    int array_size = cJSON_GetArraySize(arr);
    if (array_size < 2) {
        router_send_notice(ctx, conn_fd, "array too short");
        cJSON_Delete(arr);
        return;
    }

    cJSON *cmd = cJSON_GetArrayItem(arr, 0);
    if (!cmd || !cJSON_IsString(cmd)) {
        router_send_notice(ctx, conn_fd, "invalid command");
        cJSON_Delete(arr);
        return;
    }

    const char *cmd_str = cmd->valuestring;

    if (strcmp(cmd_str, "EVENT") == 0 && array_size >= 2) {
        cJSON *event_obj = cJSON_GetArrayItem(arr, 1);
        if (event_obj) {
            char *event_json = cJSON_PrintUnformatted(event_obj);
            handle_event(ctx, conn_fd, event_json, strlen(event_json));
            cJSON_free(event_json);
        }
    } else if (strcmp(cmd_str, "REQ") == 0 && array_size >= 3) {
        cJSON *sub_id_item = cJSON_GetArrayItem(arr, 1);
        if (sub_id_item && cJSON_IsString(sub_id_item)) {
            cJSON *filter_obj = cJSON_GetArrayItem(arr, 2);
            char *filter_json = filter_obj ? cJSON_PrintUnformatted(filter_obj) : strdup("{}");
            handle_req(ctx, conn_fd, sub_id_item->valuestring, filter_json);
            free(filter_json);
        }
    } else if (strcmp(cmd_str, "CLOSE") == 0 && array_size >= 2) {
        cJSON *sub_id_item = cJSON_GetArrayItem(arr, 1);
        if (sub_id_item && cJSON_IsString(sub_id_item)) {
            handle_close(ctx, conn_fd, sub_id_item->valuestring);
        }
    } else {
        router_send_notice(ctx, conn_fd, "unknown command");
    }

    cJSON_Delete(arr);
}
