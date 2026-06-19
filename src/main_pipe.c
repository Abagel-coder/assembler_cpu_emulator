#include "pipe_sim.h"
#include "bpred.h"
#include "cache.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Memory *clone(const Memory *src) {
    Memory *m = malloc(sizeof(Memory));
    *m = *src;
    return m;
}

static PipeStats run(const Memory *base, PipeConfig cfg) {
    Memory *m = clone(base);
    PipeStats s = pipe_run(m, cfg);
    free(m);
    return s;
}

static BpKind parse_pred(const char *s) {
    if (!strcmp(s, "none"))       return BP_NONE;
    if (!strcmp(s, "static"))     return BP_STATIC_NT;
    if (!strcmp(s, "1bit"))       return BP_BIMODAL1;
    if (!strcmp(s, "2bit"))       return BP_BIMODAL2;
    if (!strcmp(s, "gshare"))     return BP_GSHARE;
    if (!strcmp(s, "tournament")) return BP_TOURNAMENT;
    return BP_BIMODAL2;
}

static ReplPolicy parse_repl(const char *s) {
    if (!strcmp(s, "fifo"))   return REPL_FIFO;
    if (!strcmp(s, "random")) return REPL_RANDOM;
    return REPL_LRU;
}

/* "size:assoc:block" -> CacheConfig (enabled). */
static CacheConfig parse_cache(const char *s, ReplPolicy repl, int miss) {
    CacheConfig c = { .enabled = 1, .repl = repl, .hit_latency = 1, .miss_penalty = miss };
    sscanf(s, "%d:%d:%d", &c.size_bytes, &c.assoc, &c.block_bytes);
    return c;
}

static double pct(uint64_t num, uint64_t den) {
    return den ? 100.0 * (double)num / (double)den : 0.0;
}

static void full_report(const Memory *base, const char *path) {
    PipeStats nofwd = run(base, (PipeConfig){ .forwarding = 0 });
    PipeStats fwd   = run(base, (PipeConfig){ .forwarding = 1 });
    long n = (long)fwd.instructions;

    printf("=== pipeline analysis: %s ===\n", path);
    printf("dynamic instructions : %ld\n", n);
    printf("ideal cycles (CPI=1) : %ld  (CPI %.3f)\n\n", n + 4, (double)(n + 4) / n);
    printf("no forwarding        : %llu cycles  CPI %.3f  (data %llu, ctrl %llu)\n",
           (unsigned long long)nofwd.cycles, (double)nofwd.cycles / n,
           (unsigned long long)nofwd.data_stalls, (unsigned long long)nofwd.control_stalls);
    printf("with forwarding      : %llu cycles  CPI %.3f  (data %llu, ctrl %llu)\n",
           (unsigned long long)fwd.cycles, (double)fwd.cycles / n,
           (unsigned long long)fwd.data_stalls, (unsigned long long)fwd.control_stalls);
    printf("forwarding speedup   : %.2fx\n\n", (double)nofwd.cycles / fwd.cycles);

    printf("=== branch prediction (forwarding on) ===\n");
    printf("%-13s %8s %8s %8s %7s %9s %6s\n",
           "predictor", "cond", "mispred", "acc%", "MPKI", "cycles", "CPI");
    BpKind kinds[] = { BP_STATIC_NT, BP_BIMODAL1, BP_BIMODAL2, BP_GSHARE, BP_TOURNAMENT };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        PipeStats s = run(base, (PipeConfig){ .forwarding = 1, .bp = kinds[i] });
        printf("%-13s %8llu %8llu %7.1f%% %7.2f %9llu %6.3f\n",
               bp_name(kinds[i]),
               (unsigned long long)s.cond_branches,
               (unsigned long long)s.branch_mispredicts,
               pct(s.cond_branches - s.branch_mispredicts, s.cond_branches),
               1000.0 * (double)s.branch_mispredicts / n,
               (unsigned long long)s.cycles, (double)s.cycles / n);
    }

    CacheConfig l1 = { .enabled = 1, .size_bytes = 1024, .block_bytes = 32, .assoc = 4,
                       .repl = REPL_LRU, .hit_latency = 1, .miss_penalty = 10 };
    PipeStats cs = run(base, (PipeConfig){
        .forwarding = 1, .bp = BP_TOURNAMENT, .icache = l1, .dcache = l1 });
    printf("\n=== caches (L1 1KB, 32B blocks, 4-way LRU, miss=10) ===\n");
    printf("%-8s %9s %8s %8s %7s\n", "cache", "accesses", "misses", "miss%", "AMAT");
    printf("%-8s %9llu %8llu %7.2f%% %6.2f\n", "L1-I",
           (unsigned long long)cs.icache_accesses, (unsigned long long)cs.icache_misses,
           pct(cs.icache_misses, cs.icache_accesses), cs.icache_amat);
    printf("%-8s %9llu %8llu %7.2f%% %6.2f\n", "L1-D",
           (unsigned long long)cs.dcache_accesses, (unsigned long long)cs.dcache_misses,
           pct(cs.dcache_misses, cs.dcache_accesses), cs.dcache_amat);
    printf("cycles with caches   : %llu  CPI %.3f  (mem stalls %llu)\n",
           (unsigned long long)cs.cycles, (double)cs.cycles / n,
           (unsigned long long)cs.mem_stalls);
}

int main(int argc, char **argv) {
    const char *path = NULL;
    int csv = 0, forwarding = 1, ghist = 8, miss = 10;
    BpKind pred = BP_BIMODAL2;
    ReplPolicy repl = REPL_LRU;
    const char *l1i = NULL, *l1d = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--csv")) csv = 1;
        else if (!strcmp(argv[i], "--no-forward")) forwarding = 0;
        else if (!strcmp(argv[i], "--predictor") && i + 1 < argc) pred = parse_pred(argv[++i]);
        else if (!strcmp(argv[i], "--ghist-bits") && i + 1 < argc) ghist = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--miss-penalty") && i + 1 < argc) miss = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--repl") && i + 1 < argc) repl = parse_repl(argv[++i]);
        else if (!strcmp(argv[i], "--l1i") && i + 1 < argc) l1i = argv[++i];
        else if (!strcmp(argv[i], "--l1d") && i + 1 < argc) l1d = argv[++i];
        else path = argv[i];
    }

    if (!path) {
        fprintf(stderr, "usage: %s [--csv] [--predictor N] [--ghist-bits N] "
                        "[--no-forward] [--l1i S:A:B] [--l1d S:A:B] "
                        "[--repl R] [--miss-penalty N] program.bin\n", argv[0]);
        return 2;
    }

    Memory base;
    mem_init(&base);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fread(base.bytes, 1, MEM_SIZE, f);
    fclose(f);

    if (!csv) {
        full_report(&base, path);
        return 0;
    }

    PipeConfig cfg = { .forwarding = forwarding, .bp = pred, .bp_ghist_bits = ghist };
    if (l1i) cfg.icache = parse_cache(l1i, repl, miss);
    if (l1d) cfg.dcache = parse_cache(l1d, repl, miss);
    PipeStats s = run(&base, cfg);
    long n = (long)s.instructions;

    /* predictor,ghist,l1d_size,l1d_assoc,l1d_block,instructions,cond,mispred,
       acc,mpki,l1i_miss%,l1d_miss%,cycles,cpi */
    printf("%s,%d,%d,%d,%d,%ld,%llu,%llu,%.2f,%.2f,%.2f,%.2f,%llu,%.3f\n",
           bp_name(pred), ghist,
           cfg.dcache.size_bytes, cfg.dcache.assoc, cfg.dcache.block_bytes,
           n,
           (unsigned long long)s.cond_branches,
           (unsigned long long)s.branch_mispredicts,
           pct(s.cond_branches - s.branch_mispredicts, s.cond_branches),
           1000.0 * (double)s.branch_mispredicts / n,
           pct(s.icache_misses, s.icache_accesses),
           pct(s.dcache_misses, s.dcache_accesses),
           (unsigned long long)s.cycles, (double)s.cycles / n);
    return 0;
}
