#ifndef COMMANDS_H
#define COMMANDS_H

#include "network.h"

#define MAX_ARGS 16

/**
 * @brief Interactive command loop: reads user input, dispatches commands.
 * @param c  Connected and authenticated client.
 * @return 0 on normal disconnect, 1 if operator typed 'exit' (server should stop).
 */
int commands_loop(client_t *c);

#endif /* ! COMMANDS_H */
