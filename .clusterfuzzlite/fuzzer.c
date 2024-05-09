#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "minctest.h"
#include "tinyexpr.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0;
    }

    char *input = (char*)malloc(size + 1);
    if (!input) {
        return 0;
    }

    memcpy(input, data, size);
    input[size] = '\0';

    te_variable vars[] = {{ "x", 0 }};
    int error;
    te_expr *result = te_compile(input, vars, 1, &error);

    free(input);
    te_free(result);

    return 0;
}
