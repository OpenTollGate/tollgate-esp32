#ifndef OPENSSL_SHA_COMPAT_H
#define OPENSSL_SHA_COMPAT_H

#include "mbedtls/sha256.h"
#include <stddef.h>

#define SHA256_DIGEST_LENGTH 32

static inline int SHA256_compat(const unsigned char *d, size_t n, unsigned char *md)
{
    return mbedtls_sha256(d, n, md, 0);
}

#define SHA256 SHA256_compat

#endif
