#ifndef STUBS_DNS_SERVER_H
#define STUBS_DNS_SERVER_H

#include <stdint.h>
#include <stdbool.h>

static inline void dns_server_set_client_authenticated(uint32_t ip, bool auth) {
    (void)ip; (void)auth;
}

#endif
