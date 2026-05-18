#ifndef LWIP_TOLLGATE_HOOKS_H
#define LWIP_TOLLGATE_HOOKS_H

#include "lwip/pbuf.h"

int tollgate_core_ip4_canforward_filter(struct pbuf *p, u32_t dest_addr_hostorder);

#define LWIP_HOOK_IP4_CANFORWARD(p, addr) tollgate_core_ip4_canforward_filter(p, addr)

#endif
