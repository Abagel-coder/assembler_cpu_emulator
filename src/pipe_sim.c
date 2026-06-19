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
    int is_control;
    int is_conditional;
    int taken_control;    /* actual outcome: fetch was redirected */
    uint32_t pc;
    uint32_t target;      /* actual next pc when taken */
    int accesses_mem;
    uint32_t mem_addr;
} InstInfo;

static int op_is_conditional(Opcode op) {
    return op == OP_JZ || op == OP_JNZ || op == OP_JG || op == OP_JL;
}

static int op_is_control(Opcode op) {
    return op_is_conditional(op) ||
           op == OP_JMP || op == OP_CALL || op == OP_RET;
}

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
    d->is_control = op_is_control(op);
    d->is_conditional = op_is_conditional(op);

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

    int prev_silent = cpu_silent;
    cpu_silent = 1;   /* analysis run: suppress the program's own I/O */

    long cap = 4096, n = 0;
    InstInfo *trace = malloc((size_t)cap * sizeof(InstInfo));

    const long LIMIT = 50000000;
    while (!cpu.halted && n < LIMIT) {
        uint32_t raw = mem_read32(mem, cpu.pc);
        Opcode op = DECODE_OPCODE(raw);
        uint8_t rd = DECODE_RDEST(raw);
        uint8_t rs = DECODE_RSRC(raw);
        int32_t imm = DECODE_IMM(raw);

        if (n == cap) {
            cap *= 2;
            trace = realloc(trace, (size_t)cap * sizeof(InstInfo));
        }
        InstInfo *d = &trace[n];
        decode_meta(d, op, rd, rs);

        d->accesses_mem = 1;
        switch (op) {
            case OP_LOAD:  d->mem_addr = cpu.regs[rs] + (uint32_t)imm; break;
            case OP_STORE: d->mem_addr = cpu.regs[rd] + (uint32_t)imm; break;
            case OP_PUSH:  case OP_CALL: d->mem_addr = cpu.sp - 4; break;
            case OP_POP:   case OP_RET:  d->mem_addr = cpu.sp;      break;
            default:       d->accesses_mem = 0; break;
        }

        uint32_t pc_before = cpu.pc;
        cpu_step(&cpu);
        d->pc = pc_before;
        d->target = cpu.pc;
        d->taken_control = (cpu.pc != pc_before + 4);
        n++;
    }

    cpu_silent = prev_silent;

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

/* Decide the fetch penalty for a control instruction and update predictor/BTB.
 * BP_NONE reproduces the Phase A model: every taken control costs the penalty. */
static long control_penalty(BPredictor *bp, BpKind kind, const InstInfo *d,
                            PipeStats *st) {
    if (kind == BP_NONE) {
        return d->taken_control ? CONTROL_PENALTY : 0;
    }

    int pred_taken = d->is_conditional ? bp_predict(bp, d->pc) : 1;
    uint32_t pred_target = 0;
    int btb_hit = btb_lookup(bp, d->pc, &pred_target);
    int actual = d->taken_control;

    int mispredict = 0;
    if (pred_taken != actual) {
        mispredict = 1;
    } else if (actual && (!btb_hit || pred_target != d->target)) {
        mispredict = 1;
    }

    if (d->is_conditional) {
        st->cond_branches++;
        if (pred_taken != actual) st->branch_mispredicts++;
        bp_update(bp, d->pc, actual);
    }
    if (actual) btb_update(bp, d->pc, d->target);

    return mispredict ? CONTROL_PENALTY : 0;
}

PipeStats pipe_run(Memory *mem, PipeConfig cfg) {
    PipeStats st = {0};
    long n = 0;
    InstInfo *trace = capture(mem, &n, &st);
    st.instructions = (uint64_t)n;

    BPredictor bp;
    if (cfg.bp != BP_NONE) {
        bp_init(&bp, cfg.bp,
                cfg.bp_idx_bits ? cfg.bp_idx_bits : 10,
                cfg.bp_ghist_bits ? cfg.bp_ghist_bits : 8);
    }
    Cache ic, dc;
    if (cfg.icache.enabled) cache_init(&ic, cfg.icache);
    if (cfg.dcache.enabled) cache_init(&dc, cfg.dcache);

    int IF = -1, ID = -1, EX = -1, MEM = -1, WB = -1;
    long next = 0, retired = 0, pending_fetch = -1;
    long ctrl_pending = 0, fetch_stall = 0, mem_stall = 0;
    uint64_t cycles = 0, data_stalls = 0, control_stalls = 0;
    uint64_t bound = (uint64_t)(n + 16) *
        (uint64_t)(20 + cfg.icache.miss_penalty + cfg.dcache.miss_penalty) + 64;

    while (retired < n) {
        cycles++;

        if (mem_stall > 0) {           /* D-cache miss: whole pipe holds behind MEM */
            mem_stall--;
            st.mem_stalls++;
            if (cycles > bound) break;
            continue;
        }

        int stall = needs_stall(trace, ID, EX, MEM, cfg.forwarding);

        WB = MEM;
        MEM = EX;
        if (cfg.dcache.enabled && MEM >= 0 && trace[MEM].accesses_mem) {
            if (!cache_access(&dc, trace[MEM].mem_addr))
                mem_stall = cfg.dcache.miss_penalty;
        }

        if (stall) {
            EX = -1;
            data_stalls++;
        } else {
            EX = ID;
            ID = IF;
            if (ctrl_pending > 0) {
                IF = -1; ctrl_pending--; control_stalls++;
            } else if (fetch_stall > 0) {
                IF = -1; fetch_stall--; st.mem_stalls++;
            } else if (pending_fetch >= 0) {
                IF = (int)pending_fetch; pending_fetch = -1;
                if (trace[IF].is_control || trace[IF].taken_control)
                    ctrl_pending = control_penalty(&bp, cfg.bp, &trace[IF], &st);
            } else if (next < n) {
                int k = (int)next++;
                if (cfg.icache.enabled && !cache_access(&ic, trace[k].pc)) {
                    pending_fetch = k;
                    fetch_stall = cfg.icache.miss_penalty - 1;
                    IF = -1;
                    st.mem_stalls++;
                } else {
                    IF = k;
                    if (trace[IF].is_control || trace[IF].taken_control)
                        ctrl_pending = control_penalty(&bp, cfg.bp, &trace[IF], &st);
                }
            } else {
                IF = -1;
            }
        }

        if (WB >= 0) retired++;
        if (cycles > bound) break;     /* safety net */
    }

    st.cycles = cycles;
    st.data_stalls = data_stalls;
    st.control_stalls = control_stalls;

    if (cfg.icache.enabled) {
        st.icache_accesses = ic.accesses;
        st.icache_misses = ic.misses;
        st.icache_amat = cache_amat(&ic);
        cache_free(&ic);
    }
    if (cfg.dcache.enabled) {
        st.dcache_accesses = dc.accesses;
        st.dcache_misses = dc.misses;
        st.dcache_amat = cache_amat(&dc);
        cache_free(&dc);
    }
    if (cfg.bp != BP_NONE) bp_free(&bp);
    free(trace);
    return st;
}
