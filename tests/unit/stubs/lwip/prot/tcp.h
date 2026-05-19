#ifndef STUBS_LWIP_PROT_TCP_H
#define STUBS_LWIP_PROT_TCP_H

#include <stdint.h>

#define TCP_HLEN 20

struct tcp_hdr {
    uint16_t src;
    uint16_t dest;
};

#endif
