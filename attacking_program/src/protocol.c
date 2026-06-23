#include "../include/protocol.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

typedef struct __attribute__((packed))
{
    uint8_t  opcode;
    uint32_t length;
} packet_hdr_t;

/**
 * @brief XOR-encrypts or decrypts a buffer in place with the shared key.
 * @param data  Buffer to transform.
 * @param len   Length of the buffer.
 */
static void xor_crypt(char *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++)
        data[i] ^= CRYPTO_KEY[i % CRYPTO_KEYLEN];
}

/**
 * @brief Sends exactly @p len bytes, retrying on short writes.
 * @param fd   Socket file descriptor.
 * @param buf  Data to send.
 * @param len  Number of bytes.
 * @return 0 on success, -1 on error or connection close.
 */
static int send_all(int fd, const void *buf, size_t len)
{
    const char *ptr = buf;
    size_t      sent = 0;
    ssize_t     n;

    while (sent < len)
    {
        n = send(fd, ptr + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        sent += n;
    }
    return 0;
}

/**
 * @brief Receives exactly @p len bytes, retrying on short reads.
 * @param fd   Socket file descriptor.
 * @param buf  Destination buffer.
 * @param len  Number of bytes to read.
 * @return 0 on success, -1 on error or connection close.
 */
static int recv_all(int fd, void *buf, size_t len)
{
    char   *ptr = buf;
    size_t  got = 0;
    ssize_t n;

    while (got < len)
    {
        n = recv(fd, ptr + got, len - got, MSG_WAITALL);
        if (n <= 0)
            return -1;
        got += n;
    }
    return 0;
}

int send_packet(int fd, uint8_t opcode, const char *payload, uint32_t len)
{
    packet_hdr_t hdr;
    char        *enc;
    int          ret;

    hdr.opcode = opcode;
    hdr.length = htonl(len);
    if (send_all(fd, &hdr, sizeof(hdr)) < 0)
        return -1;
    if (len == 0 || !payload)
        return 0;
    enc = malloc(len);
    if (!enc)
        return -1;
    memcpy(enc, payload, len);
    xor_crypt(enc, len);
    ret = send_all(fd, enc, len);
    free(enc);
    return ret;
}

int recv_packet(int fd, uint8_t *opcode, char **payload, uint32_t *len)
{
    packet_hdr_t hdr;

    if (recv_all(fd, &hdr, sizeof(hdr)) < 0)
        return -1;
    *opcode = hdr.opcode;
    *len    = ntohl(hdr.length);
    if (*len == 0)
    {
        *payload = NULL;
        return 0;
    }
    *payload = malloc(*len + 1);
    if (!*payload)
        return -1;
    if (recv_all(fd, *payload, *len) < 0)
    {
        free(*payload);
        *payload = NULL;
        return -1;
    }
    (*payload)[*len] = '\0';
    xor_crypt(*payload, *len);
    return 0;
}
