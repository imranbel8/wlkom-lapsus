#include "protocol.h"

#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/socket.h>

struct packet_hdr
{
    uint8_t  opcode;
    uint32_t length;
} __attribute__((packed));

/**
 * @brief Sends raw bytes over a kernel socket.
 * @param sock  Target socket.
 * @param buf   Data to send.
 * @param len   Number of bytes.
 * @return Number of bytes sent, or negative on error.
 */
static int sock_send(struct socket *sock, void *buf, size_t len)
{
    struct msghdr msg = { .msg_flags = MSG_NOSIGNAL };
    struct kvec   iov = { buf, len };

    return kernel_sendmsg(sock, &msg, &iov, 1, len);
}

/**
 * @brief Receives raw bytes from a kernel socket (blocking).
 * @param sock  Source socket.
 * @param buf   Destination buffer.
 * @param len   Number of bytes to receive.
 * @return Number of bytes received, or negative on error.
 */
static int sock_recv(struct socket *sock, void *buf, size_t len)
{
    struct msghdr msg = { .msg_flags = MSG_NOSIGNAL | MSG_WAITALL };
    struct kvec   iov = { buf, len };

    return kernel_recvmsg(sock, &msg, &iov, 1, len, msg.msg_flags);
}

int send_packet(conn_ctx_t *ctx, uint8_t opcode, const char *payload,
                uint32_t len)
{
    struct packet_hdr hdr;
    char             *enc;
    int               ret;

    if (!ctx || !ctx->sock)
        return -1;
    hdr.opcode = opcode;
    hdr.length = htonl(len);
    if (sock_send(ctx->sock, &hdr, sizeof(hdr)) < 0)
        return -1;
    if (len == 0 || !payload)
        return 0;
    enc = kmalloc(len, GFP_KERNEL);
    if (!enc)
        return -1;
    memcpy(enc, payload, len);
    xor_crypt(enc, len);
    ret = sock_send(ctx->sock, enc, len);
    kfree(enc);
    return ret;
}

int recv_packet(conn_ctx_t *ctx, uint8_t *opcode, char **payload,
                uint32_t *len)
{
    struct packet_hdr hdr;
    int              ret;

    if (!ctx || !ctx->sock)
        return -1;
    ret = sock_recv(ctx->sock, &hdr, sizeof(hdr));
    if (ret <= 0)
        return -1;
    *opcode = hdr.opcode;
    *len    = ntohl(hdr.length);
    if (*len == 0)
    {
        *payload = NULL;
        return 0;
    }
    *payload = kmalloc(*len + 1, GFP_KERNEL);
    if (!*payload)
        return -1;
    ret = sock_recv(ctx->sock, *payload, *len);
    if (ret <= 0)
    {
        kfree(*payload);
        *payload = NULL;
        return -1;
    }
    (*payload)[*len] = '\0';
    xor_crypt(*payload, *len);
    return 0;
}
