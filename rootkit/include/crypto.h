#ifndef CRYPTO_H
#define CRYPTO_H

#include <linux/types.h>

#define CRYPTO_KEY    "wlk0m_xor_k3y_32bytes_padding__"
#define CRYPTO_KEYLEN 32

/**
 * @brief XOR-encrypts or decrypts a buffer in place with the shared key.
 * @param data  Buffer to transform.
 * @param len   Length of the buffer.
 */
void xor_crypt(char *data, size_t len);

#endif /* ! CRYPTO_H */
