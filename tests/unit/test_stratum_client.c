#include "test_framework.h"
#include "../../components/tollgate_core/src/tollgate_core_stratum_client.h"
#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("=== test_stratum_client ===\n");

    printf("\n--- hex_to_bytes ---\n");
    {
        const char *hex = "0a1b2c3d";
        uint8_t out[4] = {0};
        tollgate_core_stratum_hex_to_bytes(hex, out, 4);
        ASSERT_EQ_INT(0x0a, (int)out[0], "byte 0");
        ASSERT_EQ_INT(0x1b, (int)out[1], "byte 1");
        ASSERT_EQ_INT(0x2c, (int)out[2], "byte 2");
        ASSERT_EQ_INT(0x3d, (int)out[3], "byte 3");
    }

    printf("\n--- hex_to_bytes zeros ---\n");
    {
        const char *hex = "00000000";
        uint8_t out[4] = {0xff, 0xff, 0xff, 0xff};
        tollgate_core_stratum_hex_to_bytes(hex, out, 4);
        ASSERT_EQ_INT(0, (int)out[0], "zero byte 0");
        ASSERT_EQ_INT(0, (int)out[1], "zero byte 1");
    }

    printf("\n--- parse_notify valid ---\n");
    {
        cJSON *params = cJSON_Parse("[\"123\",\"0000000000000000000000000000000000000000000000000000000000000000\","
                                     "\"\""
                                     ",\"\""
                                     ",\"\""
                                     ",\"20000000\""
                                     ",\"1705e3c4\""
                                     ",\"65a3b2c1\"]");
        tollgate_stratum_job_t job = {0};
        uint32_t nbits = 0;
        bool ok = tollgate_core_stratum_parse_notify(params, &job, &nbits);
        ASSERT(ok, "parse_notify returns true");
        ASSERT_EQ_INT(123, (int)job.job_id, "job_id parsed");
        ASSERT(job.valid, "job is valid");
        ASSERT_EQ_INT(0x20000000, (int)job.version, "version parsed");
        ASSERT_EQ_INT(0x1705e3c4, (int)job.nbits, "nbits parsed");
        ASSERT_EQ_INT(0x65a3b2c1, (int)job.ntime, "ntime parsed");
        ASSERT_EQ_INT(0x1705e3c4, (int)nbits, "out_nbits set");
        ASSERT_EQ_INT(32, job.target_len, "target_len is 32");
        cJSON_Delete(params);
    }

    printf("\n--- parse_notify too few params ---\n");
    {
        cJSON *params = cJSON_Parse("[\"123\",\"abcd\"]");
        tollgate_stratum_job_t job = {0};
        bool ok = tollgate_core_stratum_parse_notify(params, &job, NULL);
        ASSERT(!ok, "parse_notify returns false for insufficient params");
        cJSON_Delete(params);
    }

    printf("\n--- parse_notify NULL ---\n");
    {
        tollgate_stratum_job_t job = {0};
        bool ok = tollgate_core_stratum_parse_notify(NULL, &job, NULL);
        ASSERT(!ok, "parse_notify returns false for NULL");
    }

    printf("\n--- parse_difficulty valid ---\n");
    {
        cJSON *params = cJSON_Parse("[512]");
        uint64_t diff = 0;
        bool ok = tollgate_core_stratum_parse_difficulty(params, &diff);
        ASSERT(ok, "parse_difficulty returns true");
        ASSERT_EQ_UINT64(512, (unsigned long long)diff, "difficulty parsed");
        cJSON_Delete(params);
    }

    printf("\n--- parse_difficulty invalid ---\n");
    {
        cJSON *params = cJSON_Parse("[]");
        uint64_t diff = 0;
        bool ok = tollgate_core_stratum_parse_difficulty(params, &diff);
        ASSERT(!ok, "parse_difficulty returns false for empty array");
        cJSON_Delete(params);
    }

    printf("\n--- build_subscribe ---\n");
    {
        char buf[256];
        int len = tollgate_core_stratum_build_subscribe(buf, sizeof(buf), 1);
        ASSERT(len > 0, "build_subscribe returns positive length");
        ASSERT(strstr(buf, "mining.subscribe") != NULL, "contains method");
        ASSERT(strstr(buf, "\"id\":1") != NULL, "contains id");
        ASSERT(strstr(buf, "TollGate/1.0") != NULL, "contains user agent");
        ASSERT(buf[len - 1] == '\n', "ends with newline");
    }

    printf("\n--- build_authorize ---\n");
    {
        char buf[512];
        int len = tollgate_core_stratum_build_authorize(buf, sizeof(buf), 2, "user1", "pass1");
        ASSERT(len > 0, "build_authorize returns positive length");
        ASSERT(strstr(buf, "mining.authorize") != NULL, "contains method");
        ASSERT(strstr(buf, "user1") != NULL, "contains user");
        ASSERT(strstr(buf, "pass1") != NULL, "contains pass");
    }

    printf("\n--- build_submit ---\n");
    {
        char buf[512];
        int len = tollgate_core_stratum_build_submit(buf, sizeof(buf), 3, "worker1",
                                                       100, "00000000", 0x65a3b2c1, 0x12345678);
        ASSERT(len > 0, "build_submit returns positive length");
        ASSERT(strstr(buf, "mining.submit") != NULL, "contains method");
        ASSERT(strstr(buf, "worker1") != NULL, "contains worker");
        ASSERT(strstr(buf, "100") != NULL, "contains job_id");
        ASSERT(strstr(buf, "00000000") != NULL, "contains extranonce2");
        ASSERT(strstr(buf, "65a3b2c1") != NULL, "contains ntime");
        ASSERT(strstr(buf, "12345678") != NULL, "contains nonce");

        cJSON *root = cJSON_Parse(buf);
        ASSERT(root != NULL, "submit json parses");
        cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        ASSERT(cJSON_IsArray(params), "params is array");
        ASSERT_EQ_INT(5, cJSON_GetArraySize(params), "submit has 5 params");
        ASSERT_EQ_STR("worker1", cJSON_GetArrayItem(params, 0)->valuestring, "param[0] = user");
        ASSERT_EQ_STR("100", cJSON_GetArrayItem(params, 1)->valuestring, "param[1] = job_id");
        ASSERT_EQ_STR("00000000", cJSON_GetArrayItem(params, 2)->valuestring, "param[2] = extranonce2");
        ASSERT_EQ_STR("65a3b2c1", cJSON_GetArrayItem(params, 3)->valuestring, "param[3] = ntime");
        ASSERT_EQ_STR("12345678", cJSON_GetArrayItem(params, 4)->valuestring, "param[4] = nonce");
        cJSON_Delete(root);
    }

    printf("\n--- build_submit with large extranonce2 ---\n");
    {
        char en2[65];
        memset(en2, 'a', 64);
        en2[64] = '\0';
        char buf[512];
        int len = tollgate_core_stratum_build_submit(buf, sizeof(buf), 4, "w2",
                                                       42, en2, 0x11111111, 0x22222222);
        ASSERT(len > 0, "build_submit large en2 returns positive length");
        ASSERT(strstr(buf, en2) != NULL, "contains full 64-char extranonce2");

        cJSON *root = cJSON_Parse(buf);
        cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        ASSERT_EQ_INT(5, cJSON_GetArraySize(params), "still 5 params with large en2");
        ASSERT_EQ_STR(en2, cJSON_GetArrayItem(params, 2)->valuestring, "large en2 in param[2]");
        cJSON_Delete(root);
    }

    printf("\n--- parse_token valid ---\n");
    {
        cJSON *params = cJSON_Parse("[\"cashuAeyJwcm9vZnMiOlt7fV19\"]");
        char token[512];
        bool ok = tollgate_core_stratum_parse_token(params, token, sizeof(token));
        ASSERT(ok, "parse_token returns true");
        ASSERT(strcmp(token, "cashuAeyJwcm9vZnMiOlt7fV19") == 0, "token matches");
        cJSON_Delete(params);
    }

    printf("\n--- parse_token empty params ---\n");
    {
        cJSON *params = cJSON_Parse("[]");
        char token[512];
        bool ok = tollgate_core_stratum_parse_token(params, token, sizeof(token));
        ASSERT(!ok, "parse_token returns false for empty array");
        cJSON_Delete(params);
    }

    printf("\n--- parse_token null ---\n");
    {
        char token[512];
        bool ok = tollgate_core_stratum_parse_token(NULL, token, sizeof(token));
        ASSERT(!ok, "parse_token returns false for NULL");
    }

    printf("\n--- parse_token buffer too small ---\n");
    {
        cJSON *params = cJSON_Parse("[\"cashuAeyJwcm9vZnMiOlt7fV19\"]");
        char token[5];
        bool ok = tollgate_core_stratum_parse_token(params, token, sizeof(token));
        ASSERT(!ok, "parse_token returns false when buffer too small");
        cJSON_Delete(params);
    }

    printf("\n--- parse_token non-string param ---\n");
    {
        cJSON *params = cJSON_Parse("[42]");
        char token[512];
        bool ok = tollgate_core_stratum_parse_token(params, token, sizeof(token));
        ASSERT(!ok, "parse_token returns false for non-string param");
        cJSON_Delete(params);
    }

    TEST_SUMMARY();
}
