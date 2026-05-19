#ifndef STUBS_LWIP_ETHARP_H
#define STUBS_LWIP_ETHARP_H

#include <stdint.h>
#include <stddef.h>
#include "lwip/ip4_addr.h"

struct eth_addr {
    uint8_t addr[6];
};

struct netif;

typedef int err_t;
#define ERR_OK 0

static inline err_t etharp_get_entry(ssize_t i, ip4_addr_t **ip, struct netif **netif, struct eth_addr **eth) {
    (void)i; (void)ip; (void)netif; (void)eth;
    return -1;
}

#endif
