#include "tinyexpr.h"
#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

static int eval(const char *str) {
    int err = 0;
    double r = te_interp(str, &err);
    if (err != 0) {
        printf("Error at position %i\n", err);
        return -1;
    } else {
        printf("%g\n", r);
        return 0;
    }
}

static void repl() {
    while (1) {
        char *line = readline("> ");
        if (line == NULL) {
            break;
        } else if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            break;
        }

        if (eval(line) != -1) {
            add_history(line);
        }

        free(line);
    }
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "-e") == 0) {
        if (eval(argv[2]) == -1) {
            return 1;
        } else {
            return 0;
        }
    } else if (argc == 1) {
        repl();
        return 0;
    } else {
        printf("Usage: %s\n", argv[0]);
        printf("       %s -e <expression>\n", argv[0]);
        return 1;
    }
}
