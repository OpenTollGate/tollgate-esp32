#ifndef STUBS_ESP_MAC_H
#define STUBS_ESP_MAC_H

#include <stdint.h>
#include <string.h>

static inline int esp_read_mac(uint8_t *mac, int type) {
    (void)type;
    memset(mac, 0, 6);
    mac[0] = 0x02;
    mac[1] = 0x00;
    mac[2] = 0x00;
    mac[3] = 0x00;
    mac[4] = 0xBE;
    mac[5] = 0xEF;
    return 0;
}

#endif
