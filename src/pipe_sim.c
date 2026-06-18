/* Decoupled functional + structural timing simulator.
 *
 * Functional path: the program is executed by the reference CPU (cpu_step), so
 * architectural results are correct by construction. Each retired instruction
 * is recorded as an InstInfo describing its register/flag reads and writes and
 * its actual control outcome.
 *
 * Timing path: that instruction stream is pushed through a real 5-stage
 * (IF/ID/EX/MEM/WB) pipeline advanced one cycle at a time. A hazard-detection
 * unit inspects the EX/MEM stage latches to decide stalls; a forwarding toggle
 * switches between full EX/MEM forwarding (only load-use stalls remain) and no
 * forwarding (a consumer waits until its producer reaches WB). Taken control
 * transfers inject a fixed redirect penalty.
 */
#include "pipe_sim.h"
#include "isa.h"

#include <stdlib.h>

#define FLAG_REG        8
#define CONTROL_PENALTY 2

typedef struct {
    int reads[3];
    int nreads;
    int has_write, write_reg;
    int is_load;
    int sets_flags;
    int taken_control;
} InstInfo;

static int op_sets_flags(Opcode op) {
    switch (op) {
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_AND: case OP_OR:  case OP_XOR: case OP_NOT:
        case OP_SHL: case OP_SHR:
            return 1;
        default:
            return 0;
    }
}

static void decode_meta(InstInfo *d, Opcode op, uint8_t rd, uint8_t rs) {
    d->nreads = 0;
    d->has_write = 0;
    d->is_load = 0;
    d->sets_flags = op_sets_flags(op);

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

static InstInfo *capture(Memory *mem, long *out_n, PipeStats *st) {
    CPU cpu;
    cpu_init(&cpu, mem);

    long cap = 4096, n = 0;
    InstInfo *trace = malloc((size_t)cap * sizeof(InstInfo));

    const long LIMIT = 50000000;
    while (!cpu.halted && n < LIMIT) {
        uint32_t raw = mem_read32(mem, cpu.pc);
        Opcode op = DECODE_OPCODE(raw);
        uint8_t rd = DECODE_RDEST(raw);
        uint8_t rs = DECODE_RSRC(raw);

        if (n == cap) {
            cap *= 2;
            trace = realloc(trace, (size_t)cap * sizeof(InstInfo));
        }
        decode_meta(&trace[n], op, rd, rs);

        uint32_t pc_before = cpu.pc;
        cpu_step(&cpu);
        trace[n].taken_control = (cpu.pc != pc_before + 4);
        n++;
    }

    for (int i = 0; i < NUM_REGS; i++) st->regs[i] = cpu.regs[i];
    st->pc = cpu.pc;
    st->sp = cpu.sp;
    st->fz = cpu.flags.z; st->fc = cpu.flags.c;
    st->fo = cpu.flags.o; st->fn = cpu.flags.n;

    *out_n = n;
    return trace;
}

static int produces(const InstInfo *p, int slot) {
    if (p->has_write && p->write_reg == slot) return 1;
    if (p->sets_flags && slot == FLAG_REG) return 1;
    return 0;
}

/* Hazard-detection unit: does the instruction in ID need to stall this cycle,
 * given what currently occupies the EX and MEM latches? */
static int needs_stall(const InstInfo *trace, int id, int ex, int mem, int fwd) {
    if (id < 0) return 0;
    const InstInfo *d = &trace[id];
    for (int r = 0; r < d->nreads; r++) {
        int s = d->reads[r];
        if (fwd) {
            if (ex >= 0 && trace[ex].is_load && produces(&trace[ex], s)) return 1;
        } else {
            if (ex >= 0 && produces(&trace[ex], s)) return 1;
            if (mem >= 0 && produces(&trace[mem], s)) return 1;
        }
    }
    return 0;
}

PipeStats pipe_run(Memory *mem, PipeConfig cfg) {
    PipeStats st = {0};
    long n = 0;
    InstInfo *trace = capture(mem, &n, &st);
    st.instructions = (uint64_t)n;

    int IF = -1, ID = -1, EX = -1, MEM = -1, WB = -1;
    long next = 0, retired = 0;
    long ctrl_pending = 0;
    uint64_t cycles = 0, data_stalls = 0, control_stalls = 0;

    while (retired < n) {
        cycles++;
        int stall = needs_stall(trace, ID, EX, MEM, cfg.forwarding);

        WB = MEM;
        MEM = EX;

        if (stall) {
            EX = -1;
            data_stalls++;
        } else {
            EX = ID;
            ID = IF;
            if (ctrl_pending > 0) {
                IF = -1;
                ctrl_pending--;
                control_stalls++;
            } else if (next < n) {
                IF = (int)next++;
                if (trace[IF].taken_control) ctrl_pending = CONTROL_PENALTY;
            } else {
                IF = -1;
            }
        }

        if (WB >= 0) retired++;

        if (cycles > (uint64_t)(n + 16) * 8 + 64) break;  /* safety net */
    }

    st.cycles = cycles;
    st.data_stalls = data_stalls;
    st.control_stalls = control_stalls;

    free(trace);
    return st;
}
