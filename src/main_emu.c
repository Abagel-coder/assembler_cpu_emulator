#include "cpu.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_binary(Memory *m, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    size_t n = fread(m->bytes, 1, MEM_SIZE, f);
    fclose(f);
    if (n == 0) {
        fprintf(stderr, "%s is empty\n", path);
        return -1;
    }
    return 0;
}

static void dump_state(const CPU *cpu) {
    for (int i = 0; i < NUM_REGS; i++) {
        printf("R%d = %u\n", i, cpu->regs[i]);
    }
    printf("PC = %u  SP = %u  Z%d C%d O%d N%d\n",
           cpu->pc, cpu->sp, cpu->flags.z, cpu->flags.c,
           cpu->flags.o, cpu->flags.n);
}

int main(int argc, char **argv) {
    int trace = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else {
            path = argv[i];
        }
    }

    if (!path) {
        fprintf(stderr, "usage: %s [--trace] program.bin\n", argv[0]);
        return 2;
    }

    Memory mem;
    mem_init(&mem);
    if (load_binary(&mem, path) != 0) return 1;

    CPU cpu;
    cpu_init(&cpu, &mem);
    cpu_run(&cpu, trace);

    dump_state(&cpu);
    return 0;
}
