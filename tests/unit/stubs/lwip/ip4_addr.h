#ifndef STUBS_LWIP_IP4_ADDR_H
#define STUBS_LWIP_IP4_ADDR_H

#include <stdint.h>

typedef uint32_t u32_t;
typedef uint16_t u16_t;

typedef struct {
    uint32_t addr;
} ip4_addr_t;

typedef ip4_addr_t esp_ip4_addr_t;

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ip) ((ip)->addr & 0xff), (((ip)->addr >> 8) & 0xff), (((ip)->addr >> 16) & 0xff), (((ip)->addr >> 24) & 0xff)

static inline void IP4_ADDR(esp_ip4_addr_t *ip, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    ip->addr = ((uint32_t)a) | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

#endif
