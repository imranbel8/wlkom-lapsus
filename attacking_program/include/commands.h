#ifndef COMMANDS_H
#define COMMANDS_H

#include "network.h"

#define MAX_ARGS 16

/**
 * @brief Interactive command loop: reads user input, dispatches commands.
 * @param c  Connected and authenticated client.
 */
void commands_loop(client_t *c);

#endif /* ! COMMANDS_H */
