#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); return 1; } } while(0)

#include "relay_types.h"
#include <cJSON.h>

static int extract_event_id_field(const char *event_json, size_t len, uint8_t id_out[32])
{
    cJSON *obj = cJSON_ParseWithLength(event_json, len);
    if (!obj) return -1;
    cJSON *id_item = cJSON_GetObjectItem(obj, "id");
    if (!id_item || !cJSON_IsString(id_item) || strlen(id_item->valuestring) != 64) {
        cJSON_Delete(obj);
        return -1;
    }
    int ret = relay_hex_to_bytes(id_item->valuestring, 64, id_out, 32);
    cJSON_Delete(obj);
    return ret;
}

static char *extract_pubkey_hex(const char *event_json, size_t len)
{
    cJSON *obj = cJSON_ParseWithLength(event_json, len);
    if (!obj) return NULL;
    cJSON *pk = cJSON_GetObjectItem(obj, "pubkey");
    char *result = NULL;
    if (pk && cJSON_IsString(pk)) result = strdup(pk->valuestring);
    cJSON_Delete(obj);
    return result;
}

int main(void)
{
    int passed = 0;

    printf("--- extract_event_id_field valid ---\n");
    {
        const char *json = "{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\"}";
        uint8_t id[32] = {0};
        int ret = extract_event_id_field(json, strlen(json), id);
        ASSERT(ret == 0, "extract should succeed");
        ASSERT(id[31] == 0x01, "last byte should be 1");
        passed += 2;
    }

    printf("--- extract_event_id_field missing id ---\n");
    {
        const char *json = "{\"kind\":1}";
        uint8_t id[32] = {0};
        int ret = extract_event_id_field(json, strlen(json), id);
        ASSERT(ret == -1, "missing id should fail");
        passed++;
    }

    printf("--- extract_event_id_field short id ---\n");
    {
        const char *json = "{\"id\":\"abcd\"}";
        uint8_t id[32] = {0};
        int ret = extract_event_id_field(json, strlen(json), id);
        ASSERT(ret == -1, "short id should fail");
        passed++;
    }

    printf("--- extract_event_id_field invalid json ---\n");
    {
        uint8_t id[32] = {0};
        int ret = extract_event_id_field("not json", 8, id);
        ASSERT(ret == -1, "invalid json should fail");
        passed++;
    }

    printf("--- extract_event_id_field known vector ---\n");
    {
        const char *json = "{\"id\":\"deadbeef00000000000000000000000000000000000000000000000000000000\"}";
        uint8_t id[32] = {0};
        int ret = extract_event_id_field(json, strlen(json), id);
        ASSERT(ret == 0, "known vector should succeed");
        ASSERT(id[0] == 0xde, "byte 0");
        ASSERT(id[1] == 0xad, "byte 1");
        ASSERT(id[2] == 0xbe, "byte 2");
        ASSERT(id[3] == 0xef, "byte 3");
        passed += 5;
    }

    printf("--- extract_pubkey_hex valid ---\n");
    {
        const char *json = "{\"pubkey\":\"abcdef1234567890\"}";
        char *pk = extract_pubkey_hex(json, strlen(json));
        ASSERT(pk != NULL, "pubkey should not be null");
        ASSERT(strcmp(pk, "abcdef1234567890") == 0, "pubkey value");
        free(pk);
        passed += 2;
    }

    printf("--- extract_pubkey_hex missing ---\n");
    {
        const char *json = "{\"kind\":1}";
        char *pk = extract_pubkey_hex(json, strlen(json));
        ASSERT(pk == NULL, "missing pubkey should return null");
        passed++;
    }

    printf("--- extract_pubkey_hex invalid json ---\n");
    {
        char *pk = extract_pubkey_hex("not json", 8);
        ASSERT(pk == NULL, "invalid json should return null");
        passed++;
    }

    printf("\n=== Results: %d passed, 0 failed ===\n", passed);
    return 0;
}
