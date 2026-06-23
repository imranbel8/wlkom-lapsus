#include "crypto.h"

void xor_crypt(char *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++)
        data[i] ^= CRYPTO_KEY[i % CRYPTO_KEYLEN];
}
