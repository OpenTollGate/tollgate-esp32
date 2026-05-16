#include "test_framework.h"
#include "../../main/nostr_event.h"
#include "../../main/identity.h"
#include <string.h>
#include <stdio.h>

static const char *TEST_NSEC = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2";

static int time_override = 1700000000;

int main(void)
{
    printf("=== test_nostr_event ===\n");

    identity_init(TEST_NSEC);
    const tollgate_identity_t *id = identity_get();

    printf("\n--- Event ID computation (NIP-01) ---\n");
    nostr_event_t event;
    esp_err_t ret = nostr_event_init(&event, id->npub_hex, 1, "[]", "Hello TollGate");
    ASSERT_EQ_INT(ESP_OK, ret, "nostr_event_init succeeds");

    ASSERT_EQ_STR(id->npub_hex, event.pubkey, "Event pubkey matches npub");
    ASSERT_EQ_INT(1, event.kind, "Event kind is 1");
    ASSERT_EQ_INT(64, (int)strlen(event.id), "Event ID is 64 hex chars");

    printf("\n--- Schnorr signing ---\n");
    ret = nostr_event_sign(&event, id->nsec);
    ASSERT_EQ_INT(ESP_OK, ret, "nostr_event_sign succeeds");
    ASSERT_EQ_INT(128, (int)strlen(event.sig), "Signature is 128 hex chars");
    ASSERT(event.sig[0] != '\0', "Signature is not empty");

    printf("\n--- JSON serialization ---\n");
    char json_buf[2048];
    ret = nostr_event_to_json(&event, json_buf, sizeof(json_buf));
    ASSERT_EQ_INT(ESP_OK, ret, "nostr_event_to_json succeeds");

    ASSERT(strstr(json_buf, "\"id\"") != NULL, "JSON has 'id' field");
    ASSERT(strstr(json_buf, "\"pubkey\"") != NULL, "JSON has 'pubkey' field");
    ASSERT(strstr(json_buf, "\"created_at\"") != NULL, "JSON has 'created_at' field");
    ASSERT(strstr(json_buf, "\"kind\"") != NULL, "JSON has 'kind' field");
    ASSERT(strstr(json_buf, "\"tags\"") != NULL, "JSON has 'tags' field");
    ASSERT(strstr(json_buf, "\"content\"") != NULL, "JSON has 'content' field");
    ASSERT(strstr(json_buf, "\"sig\"") != NULL, "JSON has 'sig' field");
    ASSERT(strstr(json_buf, "Hello TollGate") != NULL, "JSON contains content");

    printf("\n--- Buffer too small ---\n");
    char tiny_buf[10];
    ret = nostr_event_to_json(&event, tiny_buf, sizeof(tiny_buf));
    ASSERT(ret != ESP_OK, "Returns error when buffer too small");

    printf("\n--- Kind 38787 event (wifistr) ---\n");
    nostr_event_t ws_event;
    const char *ws_tags = "[[\"d\",\"test-npub\"],[\"ssid\",\"TollGate-TEST\"],[\"g\",\"u281w0dfz\"]]";
    ret = nostr_event_init(&ws_event, id->npub_hex, 38787, ws_tags,
                           "TollGate WiFi hotspot: TollGate-TEST");
    ASSERT_EQ_INT(ESP_OK, ret, "Kind 38787 init succeeds");
    ASSERT_EQ_INT(38787, ws_event.kind, "Event kind is 38787");
    ASSERT_EQ_INT(64, (int)strlen(ws_event.id), "Kind 38787 event has valid ID");

    ret = nostr_event_sign(&ws_event, id->nsec);
    ASSERT_EQ_INT(ESP_OK, ret, "Kind 38787 signing succeeds");
    ASSERT_EQ_INT(128, (int)strlen(ws_event.sig), "Kind 38787 signature is 128 hex chars");

    printf("\n--- Determinism: same input → same ID ---\n");
    nostr_event_t event2;
    nostr_event_init(&event2, id->npub_hex, 1, "[]", "Hello TollGate");
    ASSERT(strcmp(event.id, event2.id) == 0 || event.created_at != event2.created_at,
           "Same input produces same ID (if timestamp matches) or differs only by time");

    TEST_SUMMARY();
}
