#ifndef CRYPTO_H
#define CRYPTO_H

#define WLKOM_PASSWORD "zon3p_v6fu6w"

/**
 * @brief Caesar-decrypts the obfuscated password stored in the binary.
 * @param encrypted  ROT-3 encoded password string.
 * @return Pointer to a static buffer holding the plaintext password.
 */
const char *decrypt_caesar(const char *encrypted);

/**
 * @brief Prompts for the operator password and validates it against the
 *        Caesar-decoded reference.
 * @return 1 if correct, 0 otherwise.
 */
int check_password(void);

#endif /* ! CRYPTO_H */
