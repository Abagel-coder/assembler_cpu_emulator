/* Trace-driven 5-stage (IF/ID/EX/MEM/WB) timing simulator.
 *
 * Model assumptions: in-order issue, split-cycle register file, branches
 * resolved in EX (2-cycle taken penalty), ALU result forwardable from EX,
 * load result forwardable from MEM. Flags are tracked as a 9th register so
 * that flag-setter -> conditional-branch dependencies are modeled too.
 */
#include "cpu.h"
#include "isa.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>

#define FLAG_REG        8
#define NUM_SLOTS       9
#define CONTROL_PENALTY 2

typedef struct {
    int reads[3];
    int nreads;
    int has_write, write_reg;
    int is_load;
    int sets_flags;
    int redirect;
} DynInst;

static int sets_flags(Opcode op) {
    switch (op) {
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_AND: case OP_OR:  case OP_XOR: case OP_NOT:
        case OP_SHL: case OP_SHR:
            return 1;
        default:
            return 0;
    }
}

static void build_meta(DynInst *d, Opcode op, uint8_t rd, uint8_t rs) {
    d->nreads = 0;
    d->has_write = 0;
    d->is_load = 0;
    d->sets_flags = sets_flags(op);

    switch (op) {
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_AND: case OP_OR:  case OP_XOR: case OP_SHL: case OP_SHR:
            d->reads[d->nreads++] = rd;
            d->reads[d->nreads++] = rs;
            d->has_write = 1; d->write_reg = rd;
            break;
        case OP_NOT:
            d->reads[d->nreads++] = rd;
            d->has_write = 1; d->write_reg = rd;
            break;
        case OP_MOV:
            d->reads[d->nreads++] = rs;
            d->has_write = 1; d->write_reg = rd;
            break;
        case OP_LOAD:
            d->reads[d->nreads++] = rs;
            d->has_write = 1; d->write_reg = rd;
            d->is_load = 1;
            break;
        case OP_STORE:
            d->reads[d->nreads++] = rd;
            d->reads[d->nreads++] = rs;
            break;
        case OP_LOADI: case OP_IN:
            d->has_write = 1; d->write_reg = rd;
            break;
        case OP_PUSH: case OP_OUT:
            d->reads[d->nreads++] = rd;
            break;
        case OP_POP:
            d->has_write = 1; d->write_reg = rd;
            d->is_load = 1;
            break;
        case OP_JZ: case OP_JNZ: case OP_JG: case OP_JL:
            d->reads[d->nreads++] = FLAG_REG;
            break;
        default:
            break;
    }
}

typedef struct {
    long  cycles;
    long  data_stalls;
    long  control_stalls;
    long  last_if;
} Timing;

static Timing simulate(const DynInst *trace, long n, int forwarding) {
    long ready[NUM_SLOTS] = {0};
    Timing t = {0, 0, 0, 0};
    long if_prev = 0;

    for (long i = 0; i < n; i++) {
        const DynInst *d = &trace[i];
        long if_cycle = (i == 0) ? 1 : if_prev + 1;

        if (i > 0 && trace[i - 1].redirect) {
            if_cycle += CONTROL_PENALTY;
            t.control_stalls += CONTROL_PENALTY;
        }

        long ex_start = if_cycle + 2;
        long stall = 0;
        for (int r = 0; r < d->nreads; r++) {
            long need = ready[d->reads[r]];
            if (need > ex_start + stall) stall = need - ex_start;
        }
        if_cycle += stall;
        ex_start = if_cycle + 2;
        t.data_stalls += stall;

        if (d->has_write) {
            long avail = forwarding ? (d->is_load ? if_cycle + 4 : if_cycle + 3)
                                    : if_cycle + 5;
            ready[d->write_reg] = avail;
        }
        if (d->sets_flags) {
            ready[FLAG_REG] = forwarding ? if_cycle + 3 : if_cycle + 5;
        }

        if_prev = if_cycle;
        t.last_if = if_cycle;
    }

    t.cycles = (n == 0) ? 0 : t.last_if + 4;
    return t;
}

static long capture_trace(Memory *mem, DynInst **out) {
    CPU cpu;
    cpu_init(&cpu, mem);

    long cap = 4096, n = 0;
    DynInst *trace = malloc((size_t)cap * sizeof(DynInst));

    const long LIMIT = 50000000;
    while (!cpu.halted && n < LIMIT) {
        uint32_t raw = mem_read32(mem, cpu.pc);
        Opcode op = DECODE_OPCODE(raw);
        uint8_t rd = DECODE_RDEST(raw);
        uint8_t rs = DECODE_RSRC(raw);

        if (n == cap) {
            cap *= 2;
            trace = realloc(trace, (size_t)cap * sizeof(DynInst));
        }
        DynInst *d = &trace[n];
        build_meta(d, op, rd, rs);

        uint32_t pc_before = cpu.pc;
        cpu_step(&cpu);
        d->redirect = (cpu.pc != pc_before + 4);
        n++;
    }

    *out = trace;
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s program.bin\n", argv[0]);
        return 2;
    }

    Memory *mem = malloc(sizeof(Memory));
    mem_init(mem);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fread(mem->bytes, 1, MEM_SIZE, f);
    fclose(f);

    DynInst *trace;
    long n = capture_trace(mem, &trace);

    Timing nofwd = simulate(trace, n, 0);
    Timing fwd   = simulate(trace, n, 1);
    long ideal   = (n == 0) ? 0 : n + 4;

    printf("=== pipeline analysis: %s ===\n", argv[1]);
    printf("dynamic instructions : %ld\n", n);
    printf("ideal cycles (CPI=1) : %ld  (CPI %.3f)\n", ideal, (double)ideal / n);
    printf("\n");
    printf("no forwarding        : %ld cycles  CPI %.3f\n",
           nofwd.cycles, (double)nofwd.cycles / n);
    printf("  data stalls        : %ld\n", nofwd.data_stalls);
    printf("  control stalls     : %ld\n", nofwd.control_stalls);
    printf("\n");
    printf("with forwarding      : %ld cycles  CPI %.3f\n",
           fwd.cycles, (double)fwd.cycles / n);
    printf("  data stalls        : %ld\n", fwd.data_stalls);
    printf("  control stalls     : %ld\n", fwd.control_stalls);
    printf("\n");
    printf("forwarding speedup   : %.2fx\n",
           (double)nofwd.cycles / (double)fwd.cycles);

    free(trace);
    free(mem);
    return 0;
}
