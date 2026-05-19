#ifndef STUBS_LWIP_NETIF_H
#define STUBS_LWIP_NETIF_H

#include <stdint.h>
#include <stddef.h>

struct pbuf {
    void *payload;
    uint16_t len;
};

static inline uint32_t lwip_ntohl(uint32_t n) {
    return ((n & 0xFF) << 24) | ((n & 0xFF00) << 8) | ((n >> 8) & 0xFF00) | ((n >> 24) & 0xFF);
}

static inline uint16_t lwip_ntohs(uint16_t n) {
    return ((n & 0xFF) << 8) | ((n >> 8) & 0xFF);
}

#endif
