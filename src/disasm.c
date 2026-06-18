#include "isa.h"

#include <stdio.h>
#include <stdlib.h>

static void print_operands(FILE *f, Opcode op, uint8_t rd, uint8_t rs, int32_t imm) {
    switch (opcode_format(op)) {
        case OPF_NONE:
            break;
        case OPF_RD_RS:
            fprintf(f, " R%u, R%u", rd, rs);
            break;
        case OPF_RD:
            fprintf(f, " R%u", rd);
            break;
        case OPF_RD_IMM:
            fprintf(f, " R%u, %d", rd, imm);
            break;
        case OPF_RD_MEM:
            if (imm < 0) fprintf(f, " R%u, [R%u-%d]", rd, rs, -imm);
            else         fprintf(f, " R%u, [R%u+%d]", rd, rs, imm);
            break;
        case OPF_MEM_RS:
            if (imm < 0) fprintf(f, " [R%u-%d], R%u", rd, -imm, rs);
            else         fprintf(f, " [R%u+%d], R%u", rd, imm, rs);
            break;
        case OPF_IMM:
            fprintf(f, " %d", imm);
            break;
    }
}

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

    for (long addr = 0; addr + 4 <= n; addr += 4) {
        uint32_t w = (uint32_t)buf[addr] | ((uint32_t)buf[addr+1] << 8)
                   | ((uint32_t)buf[addr+2] << 16) | ((uint32_t)buf[addr+3] << 24);
        Opcode op = DECODE_OPCODE(w);
        if (raw) {
            printf("%s", opcode_name(op));
        } else {
            printf("%04lx:  %08x  %-5s", addr, w, opcode_name(op));
        }
        print_operands(stdout, op, DECODE_RDEST(w), DECODE_RSRC(w), DECODE_IMM(w));
        printf("\n");
    }

    free(buf);
    return 0;
}
