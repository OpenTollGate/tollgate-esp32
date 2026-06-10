#ifndef IDENTITY_H
#define IDENTITY_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t nsec[32];
    char nsec_hex[65];
    char npub_hex[65];

    uint8_t sta_mac[6];
    uint8_t ap_mac[6];

    char ap_ssid[32];
    esp_ip4_addr_t ap_ip;
    char ap_ip_str[16];

    uint8_t locking_privkey[32];
    uint8_t locking_pubkey[33];
    char locking_pubkey_hex[67];

    bool initialized;
} tollgate_identity_t;

esp_err_t identity_init(const char *nsec_hex);

const tollgate_identity_t *identity_get(void);

#endif
