#include "assembler.h"
#include "cache.h"
#include "cpu.h"
#include "memory.h"
#include "pipe_sim.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                                 \
    }                                                               \
} while (0)

static int load_bin(Memory *mem, const char *bin) {
    mem_init(mem);
    FILE *f = fopen(bin, "rb");
    if (!f) return -1;
    fread(mem->bytes, 1, MEM_SIZE, f);
    fclose(f);
    return 0;
}

/* The structural sim's final architectural state must match a plain cpu.c run. */
static void check_oracle(const char *asmf) {
    const char *bin = "/tmp/pipe_test.bin";
    CHECK(assemble(asmf, bin) == 0);

    Memory mref, mpipe;
    CHECK(load_bin(&mref, bin) == 0);
    CHECK(load_bin(&mpipe, bin) == 0);

    CPU ref;
    cpu_init(&ref, &mref);
    cpu_run(&ref, 0);

    PipeStats st = pipe_run(&mpipe, (PipeConfig){ .forwarding = 1 });

    for (int i = 0; i < NUM_REGS; i++) CHECK(st.regs[i] == ref.regs[i]);
    CHECK(st.pc == ref.pc);
    CHECK(st.sp == ref.sp);
    CHECK(st.instructions == ref.icount);
    CHECK(memcmp(mref.bytes, mpipe.bytes, MEM_SIZE) == 0);
}

static void run_counts(const char *asmf, uint64_t *instrs,
                       uint64_t *cyc_nofwd, uint64_t *cyc_fwd) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble(asmf, bin);
    Memory m1, m2;
    load_bin(&m1, bin);
    load_bin(&m2, bin);
    PipeStats a = pipe_run(&m1, (PipeConfig){ .forwarding = 0 });
    PipeStats b = pipe_run(&m2, (PipeConfig){ .forwarding = 1 });
    *instrs = b.instructions;
    *cyc_nofwd = a.cycles;
    *cyc_fwd = b.cycles;
}

/* Cycle counts locked against the validated analytical model. */
static void check_counts(const char *asmf, uint64_t exp_instr,
                         uint64_t exp_nofwd, uint64_t exp_fwd) {
    uint64_t instr, nofwd, fwd;
    run_counts(asmf, &instr, &nofwd, &fwd);
    CHECK(instr == exp_instr);
    CHECK(nofwd == exp_nofwd);
    CHECK(fwd == exp_fwd);
    CHECK(fwd <= nofwd);
    CHECK(fwd >= instr + 4);   /* pipeline fill overhead */
}

static PipeStats run_bp(const char *asmf, BpKind kind) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble(asmf, bin);
    Memory m;
    load_bin(&m, bin);
    return pipe_run(&m, (PipeConfig){ .forwarding = 1, .bp = kind });
}

static PipeStats run_ss_t(const char *asmf, BpKind kind, int w) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble(asmf, bin);
    Memory m;
    load_bin(&m, bin);
    return pipe_run_ss(&m, (PipeConfig){ .forwarding = 1, .bp = kind }, w);
}

static PipeStats run_ooo_t(const char *asmf, int w, int rob, int io) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble(asmf, bin);
    Memory m;
    load_bin(&m, bin);
    return pipe_run_ooo(&m, (PipeConfig){ .bp = BP_BIMODAL2 }, w, rob, io);
}

/* A dynamic predictor must beat static-not-taken on loop-heavy code, and
 * BP_NONE must leave the baseline cycle counts unchanged. */
static void check_predictors(const char *asmf) {
    PipeStats snt = run_bp(asmf, BP_STATIC_NT);
    PipeStats b2  = run_bp(asmf, BP_BIMODAL2);
    PipeStats gsh = run_bp(asmf, BP_GSHARE);

    CHECK(snt.cond_branches > 0);
    CHECK(b2.cond_branches == snt.cond_branches);
    CHECK(gsh.cond_branches == snt.cond_branches);
    /* loops are mostly-taken, so a 2-bit counter mispredicts less than static-NT */
    CHECK(b2.branch_mispredicts < snt.branch_mispredicts);
    /* fewer mispredicts => no more cycles than the naive predictor */
    CHECK(b2.cycles <= snt.cycles);
}

/* Direct-mapped-style cache behavior: compulsory miss then hits within a block,
 * and conflict eviction when a set overflows. */
static void check_cache_unit(void) {
    Cache c;
    cache_init(&c, (CacheConfig){ .enabled = 1, .size_bytes = 256, .block_bytes = 16,
                                  .assoc = 1, .repl = REPL_LRU,
                                  .hit_latency = 1, .miss_penalty = 10 });
    CHECK(cache_access(&c, 0) == 0);    /* compulsory miss */
    CHECK(cache_access(&c, 4) == 1);    /* same 16B block -> hit */
    CHECK(cache_access(&c, 8) == 1);
    CHECK(cache_access(&c, 16) == 0);   /* next block -> miss */
    CHECK(c.accesses == 4 && c.misses == 2);
    cache_free(&c);

    /* 16 sets * 16B = 256B direct-mapped: addr 0 and 256 collide in set 0 */
    Cache d;
    cache_init(&d, (CacheConfig){ .enabled = 1, .size_bytes = 256, .block_bytes = 16,
                                  .assoc = 1, .repl = REPL_LRU,
                                  .hit_latency = 1, .miss_penalty = 10 });
    CHECK(cache_access(&d, 0) == 0);
    CHECK(cache_access(&d, 256) == 0);  /* evicts block 0 */
    CHECK(cache_access(&d, 0) == 0);    /* conflict miss, not a hit */
    cache_free(&d);
}

/* A cache miss must add memory-stall cycles vs. the no-cache baseline. */
static void check_cache_pipeline(const char *asmf) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble(asmf, bin);
    Memory m0, m1;
    load_bin(&m0, bin);
    load_bin(&m1, bin);

    PipeStats base = pipe_run(&m0, (PipeConfig){ .forwarding = 1, .bp = BP_BIMODAL2 });
    CacheConfig l1 = { .enabled = 1, .size_bytes = 1024, .block_bytes = 32,
                       .assoc = 4, .repl = REPL_LRU, .hit_latency = 1, .miss_penalty = 10 };
    PipeStats cached = pipe_run(&m1, (PipeConfig){
        .forwarding = 1, .bp = BP_BIMODAL2, .icache = l1, .dcache = l1 });

    CHECK(cached.icache_accesses == cached.instructions);  /* one I-fetch per instr */
    CHECK(cached.icache_misses > 0);                       /* cold-start misses */
    CHECK(cached.mem_stalls > 0);
    CHECK(cached.cycles > base.cycles);                    /* misses cost cycles */
}

/* Unified L2: every L1 miss probes it, and it catches a working set that spills
 * L1, cutting memory stalls and cycles vs an L1-only hierarchy. */
static void check_l2(void) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble("examples/streaming.asm", bin);
    Memory m1, m2;
    load_bin(&m1, bin);
    load_bin(&m2, bin);
    CacheConfig l1 = { .enabled = 1, .size_bytes = 1024, .block_bytes = 32, .assoc = 4,
                       .repl = REPL_LRU, .hit_latency = 1, .miss_penalty = 100 };
    CacheConfig l2 = { .enabled = 1, .size_bytes = 16384, .block_bytes = 64, .assoc = 8,
                       .repl = REPL_LRU, .hit_latency = 10, .miss_penalty = 100 };
    PipeStats a = pipe_run(&m1, (PipeConfig){ .forwarding = 1, .bp = BP_BIMODAL2,
                                              .icache = l1, .dcache = l1 });
    PipeStats b = pipe_run(&m2, (PipeConfig){ .forwarding = 1, .bp = BP_BIMODAL2,
                                              .icache = l1, .dcache = l1, .l2 = l2 });
    CHECK(b.l2_accesses == b.icache_misses + b.dcache_misses);  /* every L1 miss probes L2 */
    CHECK(b.l2_misses <= b.l2_accesses);
    CHECK(b.cycles < a.cycles);
    CHECK(b.mem_stalls < a.mem_stalls);
}

/* Out-of-order: produces correct architectural state, never slower than the
 * same-resource in-order baseline, and clearly wins when a long-latency op has
 * independent work to overlap. */
static void check_ooo(void) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble("examples/ooo.asm", bin);
    Memory mref, moo;
    load_bin(&mref, bin);
    load_bin(&moo, bin);
    CPU ref;
    cpu_init(&ref, &mref);
    cpu_run(&ref, 0);
    PipeStats oo = pipe_run_ooo(&moo, (PipeConfig){ .bp = BP_BIMODAL2 }, 2, 64, 0);
    for (int i = 0; i < NUM_REGS; i++) CHECK(oo.regs[i] == ref.regs[i]);
    CHECK(oo.pc == ref.pc && oo.sp == ref.sp);
    CHECK(memcmp(mref.bytes, moo.bytes, MEM_SIZE) == 0);

    const char *progs[] = { "examples/factorial.asm", "examples/streaming.asm",
                            "examples/ooo.asm" };
    for (size_t i = 0; i < sizeof progs / sizeof progs[0]; i++)
        CHECK(run_ooo_t(progs[i], 2, 64, 0).cycles
              <= run_ooo_t(progs[i], 2, 64, 1).cycles);

    /* independent work behind a long-latency DIV -> OoO well over 2x */
    CHECK(run_ooo_t("examples/ooo.asm", 2, 64, 0).cycles * 2
          < run_ooo_t("examples/ooo.asm", 2, 64, 1).cycles);
}

/* Superscalar: width 1 must reproduce the scalar pipeline with the same
 * predictor; wider issue is never slower; and ILP yields a real speedup. */
static void check_superscalar(void) {
    const char *progs[] = { "examples/factorial.asm", "examples/fibonacci.asm",
                            "examples/bubble_sort.asm", "examples/nested_loops.asm",
                            "examples/streaming.asm" };
    for (size_t i = 0; i < sizeof progs / sizeof progs[0]; i++) {
        PipeStats sc = run_bp(progs[i], BP_BIMODAL2);
        PipeStats w1 = run_ss_t(progs[i], BP_BIMODAL2, 1);
        PipeStats w2 = run_ss_t(progs[i], BP_BIMODAL2, 2);
        CHECK(w1.cycles == sc.cycles);     /* width 1 == scalar pipeline */
        CHECK(w2.cycles <= w1.cycles);     /* wider is never slower */
        CHECK(w1.instructions == sc.instructions);
    }
    /* streaming has independent address/counter chains -> measurable ILP */
    CHECK(run_ss_t("examples/streaming.asm", BP_BIMODAL2, 2).cycles
          < run_ss_t("examples/streaming.asm", BP_BIMODAL2, 1).cycles);
}

/* The 3-way tournament (static fallback) recovers mostly-not-taken tiny kernels
 * relative to the 2-way tournament, while still beating naive static on loops. */
static void check_static_fallback(void) {
    CHECK(run_bp("examples/gcd.asm", BP_TOURNAMENT3).branch_mispredicts
          <= run_bp("examples/gcd.asm", BP_TOURNAMENT).branch_mispredicts);
    CHECK(run_bp("examples/recursive.asm", BP_TOURNAMENT3).branch_mispredicts
          <= run_bp("examples/recursive.asm", BP_TOURNAMENT).branch_mispredicts);
    CHECK(run_bp("examples/recursive.asm", BP_TOURNAMENT3).branch_mispredicts
          <= run_bp("examples/recursive.asm", BP_STATIC_NT).branch_mispredicts);
    CHECK(run_bp("examples/nested_loops.asm", BP_TOURNAMENT3).branch_mispredicts
          < run_bp("examples/nested_loops.asm", BP_STATIC_NT).branch_mispredicts);
}

/* On correlated branches (a short fixed inner loop) the textbook ordering holds:
 * gshare > 2-bit > 1-bit > static-NT. */
static void check_correlation_ordering(void) {
    const char *asmf = "examples/nested_loops.asm";
    PipeStats s  = run_bp(asmf, BP_STATIC_NT);
    PipeStats b1 = run_bp(asmf, BP_BIMODAL1);
    PipeStats b2 = run_bp(asmf, BP_BIMODAL2);
    PipeStats g  = run_bp(asmf, BP_GSHARE);
    CHECK(g.branch_mispredicts  < b2.branch_mispredicts);
    CHECK(b2.branch_mispredicts < b1.branch_mispredicts);
    CHECK(b1.branch_mispredicts < s.branch_mispredicts);
    CHECK(g.cycles < b2.cycles);
}

/* The tournament predictor must never do worse than its worse component, and
 * on correlated code it must clearly track the gshare half. */
static void check_tournament(const char *asmf) {
    PipeStats b2 = run_bp(asmf, BP_BIMODAL2);
    PipeStats g  = run_bp(asmf, BP_GSHARE);
    PipeStats t  = run_bp(asmf, BP_TOURNAMENT);
    uint64_t worse = b2.branch_mispredicts > g.branch_mispredicts
                   ? b2.branch_mispredicts : g.branch_mispredicts;
    CHECK(t.branch_mispredicts <= worse);
}

int main(void) {
    check_oracle("examples/factorial.asm");
    check_oracle("examples/fibonacci.asm");
    check_oracle("examples/bubble_sort.asm");
    check_oracle("examples/nested_loops.asm");
    check_oracle("examples/gcd.asm");
    check_oracle("examples/recursive.asm");
    check_oracle("examples/streaming.asm");

    check_counts("examples/factorial.asm", 20, 43, 32);
    check_counts("examples/fibonacci.asm", 66, 138, 88);
    check_counts("examples/bubble_sort.asm", 144, 299, 199);

    check_predictors("examples/factorial.asm");
    check_predictors("examples/fibonacci.asm");
    check_predictors("examples/bubble_sort.asm");

    check_correlation_ordering();
    check_superscalar();
    check_ooo();
    check_oracle("examples/ooo.asm");

    check_tournament("examples/nested_loops.asm");
    check_tournament("examples/bubble_sort.asm");
    check_tournament("examples/gcd.asm");
    check_static_fallback();
    /* on correlated code the tournament chooser must follow the gshare half */
    {
        PipeStats b2 = run_bp("examples/nested_loops.asm", BP_BIMODAL2);
        PipeStats t  = run_bp("examples/nested_loops.asm", BP_TOURNAMENT);
        CHECK(t.branch_mispredicts < b2.branch_mispredicts / 2);
    }

    check_cache_unit();
    check_cache_pipeline("examples/bubble_sort.asm");
    check_cache_pipeline("examples/fibonacci.asm");
    check_l2();

    if (failures == 0) {
        printf("all pipeline tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
