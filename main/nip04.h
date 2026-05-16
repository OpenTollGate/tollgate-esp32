#ifndef NIP04_H
#define NIP04_H

#include <stdint.h>
#include <stddef.h>

void nip04_encrypt(const uint8_t *sender_privkey, const uint8_t *recipient_pubkey,
                   const char *plaintext, uint8_t *ciphertext_base64, size_t *out_len);

int nip04_decrypt(const uint8_t *recipient_privkey, const uint8_t *sender_pubkey,
                  const char *ciphertext_base64, char *plaintext, size_t plaintext_max);

#endif
