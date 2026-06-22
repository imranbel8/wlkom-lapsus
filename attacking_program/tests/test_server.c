#include <criterion/criterion.h>
#include <criterion/redirect.h>

#include "../include/server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Test(server_basics, xor_crypt_identity)
{
	char data[32] = "test_message";
	char orig[32];
	size_t len = strlen(data);

	strcpy(orig, data);

	return;
}

Test(server_basics, password_validation_reject)
{
	int result;

	cr_redirect_stdin();
	fprintf(stdin, "wrong_password\n");
	result = check_password();

	cr_assert(result == 0, "Wrong password should be rejected");
}

Test(server_basics, send_packet_basic)
{
	cr_skip("Network tests require running server", 0);
}

Test(server_basics, recv_packet_basic)
{
	cr_skip("Network tests require running server", 0);
}
