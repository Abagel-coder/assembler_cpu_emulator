#include "isa.h"

#include <stdio.h>
#include <stdlib.h>

static long read_file(const char *path, uint8_t **out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    fread(buf, 1, (size_t)n, f);
    fclose(f);
    *out = buf;
    return n;
}

int main(int argc, char **argv) {
    int raw = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'r') raw = 1;
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-r] program.bin\n", argv[0]);
        return 2;
    }

    uint8_t *buf;
    long n = read_file(path, &buf);
    if (n < 0) return 1;

    char text[64];
    for (long addr = 0; addr + 4 <= n; addr += 4) {
        uint32_t w = (uint32_t)buf[addr] | ((uint32_t)buf[addr+1] << 8)
                   | ((uint32_t)buf[addr+2] << 16) | ((uint32_t)buf[addr+3] << 24);
        isa_disasm(text, sizeof text, w);
        if (raw) printf("%s\n", text);                       /* re-assemblable form */
        else     printf("%04lx:  %08x  %s\n", addr, w, text);
    }

    free(buf);
    return 0;
}
