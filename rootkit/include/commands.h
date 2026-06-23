#ifndef COMMANDS_H
#define COMMANDS_H

#include "protocol.h"

/**
 * @brief Dispatches an incoming packet to the appropriate handler.
 *        Updates ctx->authed on successful authentication.
 * @param ctx      Connection context.
 * @param opcode   Command opcode.
 * @param payload  Decrypted payload (may be NULL).
 * @param len      Payload length.
 * @return 0 to continue the loop, -1 to disconnect.
 */
int handle_packet(conn_ctx_t *ctx, uint8_t opcode, char *payload,
                  uint32_t len);

#endif /* ! COMMANDS_H */
