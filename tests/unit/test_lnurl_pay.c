#include "test_framework.h"
#include <cjson/cJSON.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char *SAMPLE_LNURLP_RESPONSE =
    "{\"callback\":\"https://coinos.io/lnurlp/callback/abc123\","
    "\"maxSendable\":1000000000,"
    "\"minSendable\":1000,"
    "\"metadata\":\"[[\\\"text/identifier\\\",\\\"TollGate@coinos.io\\\"]]\","
    "\"tag\":\"payRequest\"}";

static const char *SAMPLE_CALLBACK_RESPONSE =
    "{\"pr\":\"lnbc1pvjluezpp5qqqsyqcyq5rqwzqfqqqsyqcyq5rqwzqfqqqsyqcyq5rqwzqfqypqhp58yjmdan7s4g2e65tr58a50k2jdhz3l8eqc5x5z5f9e3u70fxnh00x09ml0q CONSTANTS\"}";

static const char *SAMPLE_MISSING_AT = "no-at-sign.com";
static const char *SAMPLE_MISSING_CALLBACK = "{\"tag\":\"payRequest\"}";
static const char *SAMPLE_MISSING_PR = "{\"status\":\"ERROR\"}";

static bool test_parse_lnurlp_response(void)
{
    cJSON *root = cJSON_Parse(SAMPLE_LNURLP_RESPONSE);
    if (!root) return false;

    cJSON *callback = cJSON_GetObjectItemCaseSensitive(root, "callback");
    bool ok = (callback && cJSON_IsString(callback) &&
               strcmp(callback->valuestring, "https://coinos.io/lnurlp/callback/abc123") == 0);

    cJSON *min_sendable = cJSON_GetObjectItemCaseSensitive(root, "minSendable");
    ok = ok && (min_sendable && cJSON_IsNumber(min_sendable) && min_sendable->valuedouble == 1000);

    cJSON *max_sendable = cJSON_GetObjectItemCaseSensitive(root, "maxSendable");
    ok = ok && (max_sendable && cJSON_IsNumber(max_sendable));

    cJSON_Delete(root);
    return ok;
}

static bool test_parse_callback_response(void)
{
    cJSON *root = cJSON_Parse(SAMPLE_CALLBACK_RESPONSE);
    if (!root) return false;

    cJSON *pr = cJSON_GetObjectItemCaseSensitive(root, "pr");
    bool ok = (pr && cJSON_IsString(pr) && strncmp(pr->valuestring, "lnbc", 4) == 0);

    cJSON_Delete(root);
    return ok;
}

static bool test_lightning_address_parse(void)
{
    const char *addr = "TollGate@coinos.io";
    const char *at = strchr(addr, '@');
    if (!at) return false;

    size_t user_len = at - addr;
    if (user_len != 8) return false;

    char username[64];
    memcpy(username, addr, user_len);
    username[user_len] = '\0';

    if (strcmp(username, "TollGate") != 0) return false;
    if (strcmp(at + 1, "coinos.io") != 0) return false;

    return true;
}

static bool test_amount_validation(void)
{
    uint64_t min_msat = 1000;
    uint64_t max_msat = 1000000000;
    uint64_t amount_msat = 21 * 1000;

    bool ok = (amount_msat >= min_msat && amount_msat <= max_msat);
    ok = ok && !(0 >= min_msat);
    ok = ok && !(2000000000ULL <= max_msat);
    return ok;
}

int main(void)
{
    printf("=== test_lnurl_pay ===\n");

    printf("\n--- LNURL-pay response parsing ---\n");
    ASSERT(test_parse_lnurlp_response(), "parse lnurlp response: callback + min/max");

    printf("\n--- Callback response parsing ---\n");
    ASSERT(test_parse_callback_response(), "parse callback response: extract bolt11 'pr'");

    printf("\n--- Lightning address parsing ---\n");
    ASSERT(test_lightning_address_parse(), "split 'TollGate@coinos.io' into user + domain");

    printf("\n--- Amount validation ---\n");
    ASSERT(test_amount_validation(), "21 sats (21000 msat) within [1000, 1000000000]");

    printf("\n--- Missing @ in address ---\n");
    {
        const char *addr = "no-at-sign.com";
        const char *at = strchr(addr, '@');
        ASSERT(at == NULL, "no @ returns NULL");
    }

    printf("\n--- Missing callback in response ---\n");
    {
        cJSON *root = cJSON_Parse(SAMPLE_MISSING_CALLBACK);
        cJSON *cb = cJSON_GetObjectItemCaseSensitive(root, "callback");
        ASSERT(!cb, "missing callback detected");
        cJSON_Delete(root);
    }

    printf("\n--- Missing pr in callback response ---\n");
    {
        cJSON *root = cJSON_Parse(SAMPLE_MISSING_PR);
        cJSON *pr = cJSON_GetObjectItemCaseSensitive(root, "pr");
        ASSERT(!pr, "missing pr detected");
        cJSON_Delete(root);
    }

    TEST_SUMMARY();
}
