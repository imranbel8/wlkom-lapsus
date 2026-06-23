#ifndef NETWORK_H
#define NETWORK_H

#define RECONNECT_DELAY 5

/**
 * @brief Starts the control server connection thread.
 * @param ip    Control server IP address string.
 * @param port  Control server TCP port.
 * @return 0 on success, -1 on thread creation failure.
 */
int connect_init(const char *ip, int port);

/**
 * @brief Stops the connection thread and closes the socket cleanly.
 */
void connect_exit(void);

#endif /* ! NETWORK_H */
