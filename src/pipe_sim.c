/* Decoupled functional + structural timing simulator.
 *
 * Functional path: the reference CPU (cpu_step) executes the program, so
 * architectural results are correct by construction. Each instruction is decoded
 * into an InstInfo describing its register/flag reads and writes, control
 * outcome, and data address.
 *
 * Timing path: a real 5-stage (IF/ID/EX/MEM/WB) pipeline advanced one cycle at a
 * time. A hazard-detection unit inspects the EX/MEM latches; a forwarding toggle
 * switches between full EX/MEM forwarding (load-use stalls only) and none.
 * Branch predictors and a cache hierarchy drive the redirect and memory stalls.
 *
 * Instructions are generated on demand into a small ring buffer rather than
 * materialized as a full trace, so memory is O(pipeline depth), not O(program
 * length); multi-million-instruction runs use a few KB.
 */
#include "pipe_sim.h"
#include "isa.h"

#include <stdlib.h>

#define FLAG_REG        8
#define CONTROL_PENALTY 2
#define WIN_CAP         32          /* >> max instructions in flight */
#define WIN_MASK        (WIN_CAP - 1)
#define GEN_LIMIT       50000000L

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
    int latency;          /* functional-unit latency (cycles), for the OoO model */
} InstInfo;

static int op_latency(Opcode op) {
    switch (op) {
        case OP_DIV:  return 20;
        case OP_MUL:  return 3;
        case OP_LOAD: case OP_POP: return 2;
        default:      return 1;
    }
}

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
    d->latency = op_latency(op);

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

/* On-demand instruction generator with a bounded ring buffer. Instruction k is
 * produced by stepping the reference CPU; only the live pipeline window is kept. */
typedef struct {
    CPU      cpu;
    long     count;          /* instructions produced so far */
    int      done;
    InstInfo win[WIN_CAP];
} Gen;

static void gen_init(Gen *g, Memory *mem) {
    cpu_init(&g->cpu, mem);
    g->count = 0;
    g->done = 0;
}

static void gen_one(Gen *g) {
    CPU *cpu = &g->cpu;
    uint32_t raw = mem_read32(cpu->mem, cpu->pc);
    Opcode op = DECODE_OPCODE(raw);
    uint8_t rd = DECODE_RDEST(raw);
    uint8_t rs = DECODE_RSRC(raw);
    int32_t imm = DECODE_IMM(raw);

    InstInfo *d = &g->win[g->count & WIN_MASK];
    decode_meta(d, op, rd, rs);
    d->accesses_mem = 1;
    switch (op) {
        case OP_LOAD:  d->mem_addr = cpu->regs[rs] + (uint32_t)imm; break;
        case OP_STORE: d->mem_addr = cpu->regs[rd] + (uint32_t)imm; break;
        case OP_PUSH:  case OP_CALL: d->mem_addr = cpu->sp - 4; break;
        case OP_POP:   case OP_RET:  d->mem_addr = cpu->sp;      break;
        default:       d->accesses_mem = 0; break;
    }

    uint32_t pc_before = cpu->pc;
    cpu_step(cpu);
    d->pc = pc_before;
    d->target = cpu->pc;
    d->taken_control = (cpu->pc != pc_before + 4);
    g->count++;
    if (cpu->halted || g->count >= GEN_LIMIT) g->done = 1;
}

/* Produce up to index k (k stays within the live pipeline window). */
static int gen_ensure(Gen *g, long k) {
    while (g->count <= k && !g->done) gen_one(g);
    return k < g->count;
}

static const InstInfo *gen_at(const Gen *g, long idx) {
    return &g->win[idx & WIN_MASK];
}

static int produces(const InstInfo *p, int slot) {
    if (p->has_write && p->write_reg == slot) return 1;
    if (p->sets_flags && slot == FLAG_REG) return 1;
    return 0;
}

/* Hazard-detection unit: does the instruction in ID need to stall this cycle,
 * given what currently occupies the EX and MEM latches? */
static int needs_stall(const Gen *g, long id, long ex, long mem, int fwd) {
    if (id < 0) return 0;
    const InstInfo *d = gen_at(g, id);
    for (int r = 0; r < d->nreads; r++) {
        int s = d->reads[r];
        if (fwd) {
            if (ex >= 0 && gen_at(g, ex)->is_load && produces(gen_at(g, ex), s))
                return 1;
        } else {
            if (ex >= 0 && produces(gen_at(g, ex), s)) return 1;
            if (mem >= 0 && produces(gen_at(g, mem), s)) return 1;
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

    int prev_silent = cpu_silent;
    cpu_silent = 1;   /* analysis run: suppress the program's own I/O */

    Gen g;
    gen_init(&g, mem);

    BPredictor bp;
    if (cfg.bp != BP_NONE) {
        bp_init(&bp, cfg.bp,
                cfg.bp_idx_bits ? cfg.bp_idx_bits : 10,
                cfg.bp_ghist_bits ? cfg.bp_ghist_bits : 8);
    }
    Cache ic, dc, l2;
    if (cfg.icache.enabled) cache_init(&ic, cfg.icache);
    if (cfg.dcache.enabled) cache_init(&dc, cfg.dcache);
    if (cfg.l2.enabled)     cache_init(&l2, cfg.l2);

    long IF = -1, ID = -1, EX = -1, MEM = -1, WB = -1;
    long next = 0, retired = 0, pending_fetch = -1;
    long ctrl_pending = 0, fetch_stall = 0, mem_stall = 0;
    uint64_t cycles = 0, data_stalls = 0, control_stalls = 0;
    int per_inst = 20 + cfg.icache.miss_penalty + cfg.dcache.miss_penalty;

    while (!(g.done && retired >= g.count)) {
        cycles++;
        uint64_t bound = (uint64_t)(g.count + 16) * (uint64_t)per_inst + 64;

        if (mem_stall > 0) {           /* D-cache miss: whole pipe holds behind MEM */
            mem_stall--;
            st.mem_stalls++;
            if (cycles > bound) break;
            continue;
        }

        int stall = needs_stall(&g, ID, EX, MEM, cfg.forwarding);

        WB = MEM;
        MEM = EX;
        if (cfg.dcache.enabled && MEM >= 0 && gen_at(&g, MEM)->accesses_mem) {
            uint32_t addr = gen_at(&g, MEM)->mem_addr;
            if (!cache_access(&dc, addr)) {
                long pen = cfg.dcache.miss_penalty;
                if (cfg.l2.enabled)
                    pen = cache_access(&l2, addr) ? cfg.l2.hit_latency
                                                  : cfg.l2.miss_penalty;
                mem_stall = pen;
            }
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
                IF = pending_fetch; pending_fetch = -1;
                const InstInfo *d = gen_at(&g, IF);
                if (d->is_control || d->taken_control)
                    ctrl_pending = control_penalty(&bp, cfg.bp, d, &st);
            } else if (gen_ensure(&g, next)) {
                long k = next++;
                const InstInfo *d = gen_at(&g, k);
                if (cfg.icache.enabled && !cache_access(&ic, d->pc)) {
                    long pen = cfg.icache.miss_penalty;
                    if (cfg.l2.enabled)
                        pen = cache_access(&l2, d->pc) ? cfg.l2.hit_latency
                                                       : cfg.l2.miss_penalty;
                    pending_fetch = k;
                    fetch_stall = pen - 1;
                    IF = -1;
                    st.mem_stalls++;
                } else {
                    IF = k;
                    if (d->is_control || d->taken_control)
                        ctrl_pending = control_penalty(&bp, cfg.bp, d, &st);
                }
            } else {
                IF = -1;     /* generation exhausted */
            }
        }

        if (WB >= 0) retired++;
        if (cycles > bound) break;     /* safety net */
    }

    cpu_silent = prev_silent;

    st.instructions = (uint64_t)g.count;
    st.cycles = cycles;
    st.data_stalls = data_stalls;
    st.control_stalls = control_stalls;

    for (int i = 0; i < NUM_REGS; i++) st.regs[i] = g.cpu.regs[i];
    st.pc = g.cpu.pc;
    st.sp = g.cpu.sp;
    st.fz = g.cpu.flags.z; st.fc = g.cpu.flags.c;
    st.fo = g.cpu.flags.o; st.fn = g.cpu.flags.n;

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
    if (cfg.l2.enabled) {
        st.l2_accesses = l2.accesses;
        st.l2_misses = l2.misses;
        cache_free(&l2);
    }
    if (cfg.bp != BP_NONE) bp_free(&bp);
    return st;
}

/* Tomasulo-style out-of-order timing model.
 *
 * Instructions are processed in program order (dispatch order). Renaming is
 * implicit: reg_ready[r] tracks the completion cycle of r's most recent
 * producer, so only true (RAW) dependencies constrain execution. Execution
 * starts when operands are ready (OoO) or, with in_order set, no earlier than
 * the previous instruction's execution. A finite ROB bounds the window: an
 * instruction cannot dispatch until the slot ROB entries older has committed.
 * Dispatch and commit are in order, each up to `width` per cycle. */
PipeStats pipe_run_ooo(Memory *mem, PipeConfig cfg, int width, int rob_size,
                       int in_order) {
    PipeStats st = {0};

    int prev_silent = cpu_silent;
    cpu_silent = 1;

    Gen g;
    gen_init(&g, mem);

    BPredictor bp;
    if (cfg.bp != BP_NONE) {
        bp_init(&bp, cfg.bp,
                cfg.bp_idx_bits ? cfg.bp_idx_bits : 10,
                cfg.bp_ghist_bits ? cfg.bp_ghist_bits : 8);
    }

    long reg_ready[FLAG_REG + 1] = {0};
    long *rob = calloc((size_t)rob_size, sizeof(long));
    long disp_cycle = 1, com_cycle = 1;
    int  disp_count = 0, com_count = 0;
    long fetch_block = 0, last_exec = 0, last_commit = 0;

    for (long i = 0; ; i++) {
        if (!gen_ensure(&g, i)) break;
        const InstInfo *d = gen_at(&g, i);

        /* dispatch: in order, width/cycle, gated by a free ROB slot and branches */
        long disp = (disp_count < width) ? disp_cycle : disp_cycle + 1;
        if (disp < fetch_block) disp = fetch_block;
        if (i >= rob_size) {
            long slot_free = rob[i % rob_size] + 1;
            if (disp < slot_free) disp = slot_free;
        }
        if (disp == disp_cycle && disp_count < width) disp_count++;
        else { disp_cycle = disp; disp_count = 1; }

        /* execute: when operands ready (OoO), or after the prior instr (in-order) */
        long exec = disp;
        for (int r = 0; r < d->nreads; r++)
            if (reg_ready[d->reads[r]] > exec) exec = reg_ready[d->reads[r]];
        if (in_order && exec < last_exec) exec = last_exec;
        last_exec = exec;

        long complete = exec + d->latency;
        if (d->has_write)  reg_ready[d->write_reg] = complete;
        if (d->sets_flags) reg_ready[FLAG_REG] = complete;

        /* commit: in order, width/cycle, after completion */
        long commit = (com_count < width) ? com_cycle : com_cycle + 1;
        if (commit < complete) commit = complete;
        if (commit == com_cycle && com_count < width) com_count++;
        else { com_cycle = commit; com_count = 1; }
        rob[i % rob_size] = commit;
        last_commit = commit;

        if (d->is_control || d->taken_control) {
            long pen = control_penalty(&bp, cfg.bp, d, &st);
            if (pen) fetch_block = exec + pen;
        }
    }

    cpu_silent = prev_silent;

    st.instructions = (uint64_t)g.count;
    st.cycles = g.count ? (uint64_t)last_commit : 0;
    for (int i = 0; i < NUM_REGS; i++) st.regs[i] = g.cpu.regs[i];
    st.pc = g.cpu.pc;
    st.sp = g.cpu.sp;
    st.fz = g.cpu.flags.z; st.fc = g.cpu.flags.c;
    st.fo = g.cpu.flags.o; st.fn = g.cpu.flags.n;

    free(rob);
    if (cfg.bp != BP_NONE) bp_free(&bp);
    return st;
}

/* In-order superscalar timing via a per-register/flag ready-cycle scoreboard.
 * Each instruction is assigned a fetch cycle `iff`; up to `width` instructions
 * may share a cycle (a bundle). An instruction's EX happens at iff+2, so a
 * source operand produced into ready[s] must satisfy iff+2 >= ready[s]. */
PipeStats pipe_run_ss(Memory *mem, PipeConfig cfg, int width) {
    PipeStats st = {0};

    int prev_silent = cpu_silent;
    cpu_silent = 1;

    Gen g;
    gen_init(&g, mem);

    BPredictor bp;
    if (cfg.bp != BP_NONE) {
        bp_init(&bp, cfg.bp,
                cfg.bp_idx_bits ? cfg.bp_idx_bits : 10,
                cfg.bp_ghist_bits ? cfg.bp_ghist_bits : 8);
    }

    long ready[FLAG_REG + 1] = {0};
    long cur_if = 0, last_if = 0, fetch_block = 0;
    int  cur_count = 0;

    for (long i = 0; ; i++) {
        if (!gen_ensure(&g, i)) break;
        const InstInfo *d = gen_at(&g, i);

        long earliest;
        if (i == 0)                  earliest = 1;
        else if (cur_count < width)  earliest = cur_if;       /* same bundle */
        else                         earliest = cur_if + 1;   /* next cycle */
        if (earliest < fetch_block) earliest = fetch_block;

        long iff = earliest;
        for (int r = 0; r < d->nreads; r++) {
            long need = ready[d->reads[r]] - 2;   /* operand must be ready by EX (iff+2) */
            if (need > iff) iff = need;
        }

        if (iff == cur_if && cur_count < width) {
            cur_count++;
        } else {
            cur_if = iff;
            cur_count = 1;
        }

        if (d->has_write)
            ready[d->write_reg] = cfg.forwarding ? (d->is_load ? iff + 4 : iff + 3)
                                                 : iff + 5;
        if (d->sets_flags)
            ready[FLAG_REG] = cfg.forwarding ? iff + 3 : iff + 5;

        if (d->is_control || d->taken_control) {
            long pen = control_penalty(&bp, cfg.bp, d, &st);
            if (pen) fetch_block = iff + 1 + pen;
        }
        last_if = iff;
    }

    cpu_silent = prev_silent;

    st.instructions = (uint64_t)g.count;
    st.cycles = g.count ? (uint64_t)(last_if + 4) : 0;
    for (int i = 0; i < NUM_REGS; i++) st.regs[i] = g.cpu.regs[i];
    st.pc = g.cpu.pc;
    st.sp = g.cpu.sp;
    st.fz = g.cpu.flags.z; st.fc = g.cpu.flags.c;
    st.fo = g.cpu.flags.o; st.fn = g.cpu.flags.n;

    if (cfg.bp != BP_NONE) bp_free(&bp);
    return st;
}
