#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

#include "protocol.h"

typedef struct
{
    int  fd;
    bool connected;
    bool authed;
    char remote_ip[64];
    int  remote_port;
} client_t;

/**
 * @brief Creates a TCP server socket bound to @p port.
 * @param port  TCP port number to listen on.
 * @return Listening socket file descriptor, or -1 on error.
 */
int server_init(int port);

/**
 * @brief Accepts connections in a loop and handles them sequentially.
 * @param srv_fd  Listening server socket.
 */
void server_run(int srv_fd);

/**
 * @brief Closes the server socket.
 * @param srv_fd  Listening server socket.
 */
void server_close(int srv_fd);

#endif /* ! NETWORK_H */
