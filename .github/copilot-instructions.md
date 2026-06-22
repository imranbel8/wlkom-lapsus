# Prompt pour générer un projet EPITA conforme
---

## 📋 Prompt

Tu vas générer un projet C conforme aux normes EPITA. Voici les détails :

### 1. Structure du projet

```
project-name/
├── Makefile
├── src/
│   ├── main.c
│   ├── main.h
│   ├── module.c
│   └── module.h
├── tests/
│   └── test_*.c
└── .gitignore
```

### 2. Normes de code EPITA

- **Norme C99** : `#include <stdint.h>`, `#include <stdbool.h>`, `#include <string.h>`
- **Max 80 caractères** par ligne
- **Indentation** : 4 espaces (ou 1 tab = 4 espaces)
- **Nommage** :
  - Variables/fonctions : `snake_case`
  - Constantes : `UPPER_SNAKE_CASE`
- **Commentaires** : Pas de Commentaires.
- **Pas de variable globale** sauf constantes.
- **Fonctions courtes** : max 40 lignes, une responsabilité
- **Aucun magic number** : utiliser des `#define`
- **Toujours une ligne vide à la fin du fichier.
- **ATTENTION** AUCUN Cast explicite, les casts implicites de char vers void sont ok mais aucune autre forme de cast n'est autorisée. 
- **Maximum 4 parametres par fonction. 


### 3. Test-Driven Development (TDD)

- **Framework** : Criterion
- **Convention** : `tests/test_<module>.c`
- **Avant d'écrire du code** : écrire les tests, demander une validation des uses_cases ensuite coder les fonctions.
- **Tests unitaires** : chaque fonction doit avoir au moins 3 cas de test
- **Compilation** : `gcc -std=c99 -pedantic -Werror -Wall -Wextra -Wvla`

### 4. Makefile

```makefile
CC ?= gcc
CFLAGS = -std=c99 -pedantic -Werror -Wall -Wextra -Wvla
LDLIBS = -lcriterion

SRC = src/main.c src/module.c
OBJ = $(SRC:.c=.o)
RESULT = binary_name

TEST_SRC = tests/test_main.c tests/test_module.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_BIN = test_suite

.PHONY: all check clean

all: $(RESULT)

$(RESULT): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_BIN): $(OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

check: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	$(RM) $(OBJ) $(TEST_BIN) $(TEST_OBJ) $(RESULT)
```

### 5. Headers (.h)

```c


#ifndef HEADER_NAME_H
#define HEADER_NAME_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	int field1;
	char *field2;
} my_struct_t;

int my_function(int param1, const char *param2);

#endif /* ! HEADER_NAME_H */
```

### 6. Sources (.c)

```c


#include <stdio.h>
#include <stdlib.h>

#include "../include/[header].h"

int my_function(int param1, const char *param2)
{
	/* max 40 lines, clear logic */
	return 0;
}
```

### 7. Tests (test_*.c)

```c


#include <criterion/criterion.h>
#include <criterion/redirect.h>

#include "../include/[header].h"

Test(module_name, test_case_1)
{
	/* normal case */
	cr_assert_eq(function_result, expected_value);
}

Test(module_name, test_case_2)
{
	/* edge case */
	cr_assert_eq(function_result, expected_value);
}

Test(module_name, test_case_3)
{
	/* error case */
	cr_assert_eq(function_result, expected_error);
}
```

### 8. .gitignore

```
*.o
*.a
a.out
test_suite
[binary_name]
*.swp
*.swo

