#include "../include/crypto.h"

#include <stdio.h>
#include <string.h>

#define PASS_SHIFT  3
#define ALPHA_SIZE  26
#define DIGIT_SIZE  10
#define BUFFER_SIZE 256

/**
 * @brief Caesar-decrypts the obfuscated password stored in the binary.
 * @param encrypted  ROT-3 encoded password string.
 * @return Pointer to a static buffer holding the plaintext password.
 */
const char *decrypt_caesar(const char *encrypted)
{
    static char decrypted[BUFFER_SIZE];
    int         i;

    for (i = 0; encrypted[i]; i++)
    {
        char c = encrypted[i];
        if (c >= 'a' && c <= 'z')
            decrypted[i] = 'a' + (c - 'a' - PASS_SHIFT + ALPHA_SIZE) % ALPHA_SIZE;
        else if (c >= 'A' && c <= 'Z')
            decrypted[i] = 'A' + (c - 'A' - PASS_SHIFT + ALPHA_SIZE) % ALPHA_SIZE;
        else if (c >= '0' && c <= '9')
            decrypted[i] = '0' + (c - '0' - PASS_SHIFT + DIGIT_SIZE) % DIGIT_SIZE;
        else
            decrypted[i] = c;
    }
    decrypted[i] = '\0';
    return decrypted;
}

/**
 * @brief Prompts the operator for the control password and validates it.
 * @return 1 if the password matches, 0 otherwise.
 */
int check_password(void)
{
    char input[BUFFER_SIZE];

    printf("Password: ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin))
        return 0;
    input[strcspn(input, "\n")] = '\0';
    return strcmp(input, decrypt_caesar(WLKOM_PASSWORD)) == 0;
}
