#include "test_framework.h"
#include "../../main/nostr_event.h"
#include "../../main/identity.h"
#include "../../components/wisp_relay/relay_validator.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TEST_NSEC = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2";

int main(void)
{
    printf("=== test_relay_validator ===\n");

    identity_init(TEST_NSEC);
    const tollgate_identity_t *id = identity_get();

    printf("\n--- Valid event verification ---\n");
    nostr_event_t event;
    esp_err_t ret = nostr_event_init(&event, id->npub_hex, 1, "[]", "test relay validator");
    ASSERT_EQ_INT(ESP_OK, ret, "event init succeeds");
    ret = nostr_event_sign(&event, id->nsec);
    ASSERT_EQ_INT(ESP_OK, ret, "event sign succeeds");

    char json_buf[2048];
    ret = nostr_event_to_json(&event, json_buf, sizeof(json_buf));
    ASSERT_EQ_INT(ESP_OK, ret, "event to json succeeds");

    bool valid = relay_validator_verify_event(json_buf, strlen(json_buf));
    ASSERT(valid, "Valid event passes verification");

    printf("\n--- Tampered event ID detection ---\n");
    char *tampered = strdup(json_buf);
    char *id_pos = strstr(tampered, event.id);
    ASSERT(id_pos != NULL, "Found ID in JSON");
    id_pos[0] = (id_pos[0] == 'a') ? 'b' : 'a';
    bool tampered_valid = relay_validator_verify_event(tampered, strlen(tampered));
    ASSERT(!tampered_valid, "Tampered event ID fails verification");
    free(tampered);

    printf("\n--- Tampered signature detection ---\n");
    tampered = strdup(json_buf);
    char *sig_pos = strstr(tampered, event.sig);
    ASSERT(sig_pos != NULL, "Found sig in JSON");
    sig_pos[0] = (sig_pos[0] == 'a') ? 'b' : 'a';
    tampered_valid = relay_validator_verify_event(tampered, strlen(tampered));
    ASSERT(!tampered_valid, "Tampered signature fails verification");
    free(tampered);

    printf("\n--- Tampered content detection ---\n");
    tampered = strdup(json_buf);
    char *content_pos = strstr(tampered, "test relay validator");
    ASSERT(content_pos != NULL, "Found content in JSON");
    content_pos[0] = 'X';
    tampered_valid = relay_validator_verify_event(tampered, strlen(tampered));
    ASSERT(!tampered_valid, "Tampered content fails verification");
    free(tampered);

    printf("\n--- Empty/invalid JSON ---\n");
    ASSERT(!relay_validator_verify_event("", 0), "Empty string fails");
    ASSERT(!relay_validator_verify_event("{}", 2), "Empty object fails");
    ASSERT(!relay_validator_verify_event("not json", 8), "Non-JSON fails");
    ASSERT(!relay_validator_verify_event("[1,2,3]", 7), "Array fails");

    printf("\n--- Missing fields ---\n");
    ASSERT(!relay_validator_verify_event("{\"id\":\"" "0000000000000000000000000000000000000000000000000000000000000000" "\"}", 71),
           "Missing pubkey/sig fails");

    printf("\n--- result_string ---\n");
    ASSERT_EQ_STR("ok", relay_validator_result_string(VALIDATION_OK), "OK string");
    ASSERT_EQ_STR("invalid: signature", relay_validator_result_string(VALIDATION_ERR_SIG), "SIG string");
    ASSERT_EQ_STR("invalid: event id", relay_validator_result_string(VALIDATION_ERR_ID), "ID string");

    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
