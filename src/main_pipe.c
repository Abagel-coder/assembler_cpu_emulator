#include "pipe_sim.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s program.bin\n", argv[0]);
        return 2;
    }

    Memory *mem = malloc(sizeof(Memory));
    mem_init(mem);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); free(mem); return 1; }
    fread(mem->bytes, 1, MEM_SIZE, f);
    fclose(f);

    PipeStats nofwd, fwd;
    {
        Memory *m = malloc(sizeof(Memory));
        *m = *mem;
        nofwd = pipe_run(m, (PipeConfig){ .forwarding = 0 });
        free(m);
    }
    {
        Memory *m = malloc(sizeof(Memory));
        *m = *mem;
        fwd = pipe_run(m, (PipeConfig){ .forwarding = 1 });
        free(m);
    }

    long n = (long)fwd.instructions;
    long ideal = n + 4;

    printf("=== pipeline analysis: %s ===\n", argv[1]);
    printf("dynamic instructions : %ld\n", n);
    printf("ideal cycles (CPI=1) : %ld  (CPI %.3f)\n", ideal, (double)ideal / n);
    printf("\n");
    printf("no forwarding        : %llu cycles  CPI %.3f\n",
           (unsigned long long)nofwd.cycles, (double)nofwd.cycles / n);
    printf("  data stalls        : %llu\n", (unsigned long long)nofwd.data_stalls);
    printf("  control stalls     : %llu\n", (unsigned long long)nofwd.control_stalls);
    printf("\n");
    printf("with forwarding      : %llu cycles  CPI %.3f\n",
           (unsigned long long)fwd.cycles, (double)fwd.cycles / n);
    printf("  data stalls        : %llu\n", (unsigned long long)fwd.data_stalls);
    printf("  control stalls     : %llu\n", (unsigned long long)fwd.control_stalls);
    printf("\n");
    printf("forwarding speedup   : %.2fx\n",
           (double)nofwd.cycles / (double)fwd.cycles);

    free(mem);
    return 0;
}
