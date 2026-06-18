#include "assembler.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *in = NULL, *out = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
        } else {
            in = argv[i];
        }
    }

    if (!in || !out) {
        fprintf(stderr, "usage: %s input.asm -o output.bin\n", argv[0]);
        return 2;
    }

    return assemble(in, out);
}
