#include "nip11_relay.h"
#include <string.h>

static const char *NIP11_JSON =
"{"
  "\"name\":\"TollGate Relay\","
  "\"description\":\"Local Nostr relay with 21-day TTL and negentropy sync\","
  "\"pubkey\":\"\","
  "\"contact\":\"\","
  "\"supported_nips\":[1,9,11,20,40,77],"
  "\"software\":\"https://github.com/nicobao/esp32-tollgate\","
  "\"version\":\"1.0.0\","
  "\"limitation\":{"
    "\"max_message_length\":65536,"
    "\"max_subscriptions\":8,"
    "\"max_filters\":4,"
    "\"max_limit\":500,"
    "\"max_subid_length\":64,"
    "\"max_event_tags\":100,"
    "\"max_content_length\":32768,"
    "\"min_pow_difficulty\":0,"
    "\"auth_required\":false,"
    "\"payment_required\":false"
  "},"
  "\"retention\":[{\"kinds\":[0,1,2,3,4,5,6,7],\"time\":1814400}],"
  "\"relay_countries\":[\"DE\"]"
"}";

esp_err_t relay_nip11_handler(httpd_req_t *req)
{
    char accept[64] = "";
    httpd_req_get_hdr_value_str(req, "Accept", accept, sizeof(accept));

    if (strstr(accept, "application/nostr+json")) {
        httpd_resp_set_type(req, "application/nostr+json");
    } else {
        httpd_resp_set_type(req, "application/json");
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Accept");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    return httpd_resp_send(req, NIP11_JSON, strlen(NIP11_JSON));
}

esp_err_t relay_nip11_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Accept");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}
