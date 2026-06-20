#ifndef PIPE_SIM_H
#define PIPE_SIM_H

#include <stdint.h>
#include "memory.h"
#include "cpu.h"
#include "bpred.h"
#include "cache.h"

typedef struct {
    int         forwarding;
    BpKind      bp;
    int         bp_idx_bits;
    int         bp_ghist_bits;
    CacheConfig icache;
    CacheConfig dcache;
    CacheConfig l2;       /* unified L2 behind both L1s; miss_penalty = memory latency */
} PipeConfig;

typedef struct {
    uint64_t instructions;
    uint64_t cycles;
    uint64_t data_stalls;
    uint64_t control_stalls;
    uint64_t mem_stalls;

    uint64_t cond_branches;
    uint64_t branch_mispredicts;   /* direction mispredicts on conditionals */

    uint64_t icache_accesses, icache_misses;
    uint64_t dcache_accesses, dcache_misses;
    uint64_t l2_accesses, l2_misses;
    double   icache_amat, dcache_amat;

    /* final architectural state, for oracle comparison */
    uint32_t regs[NUM_REGS];
    uint32_t pc, sp;
    uint8_t  fz, fc, fo, fn;
} PipeStats;

PipeStats pipe_run(Memory *mem, PipeConfig cfg);

/* In-order superscalar issue model (width lanes/cycle). width=1 reproduces the
 * scalar pipeline. Models data hazards, forwarding, and branch penalties; does
 * not model caches. */
PipeStats pipe_run_ss(Memory *mem, PipeConfig cfg, int width);

/* Tomasulo-style dynamic scheduling: in-order dispatch/commit, out-of-order
 * execute, register renaming (true deps only), a finite reorder buffer, and
 * multi-cycle functional-unit latencies. Set in_order=1 for an otherwise
 * identical in-order baseline (isolates the benefit of OoO execution). */
PipeStats pipe_run_ooo(Memory *mem, PipeConfig cfg, int width, int rob, int in_order);

#endif /* PIPE_SIM_H */
