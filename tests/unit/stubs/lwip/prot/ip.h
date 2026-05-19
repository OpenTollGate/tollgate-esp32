#ifndef STUBS_LWIP_PROT_IP_H
#define STUBS_LWIP_PROT_IP_H

#include <stdint.h>

#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17
#define IP_HLEN 20

struct ip_hdr {
    uint8_t _proto;
    union {
        uint32_t addr;
    } src;
    union {
        uint32_t addr;
    } dest;
};

#endif
