#ifndef PIPE_SIM_H
#define PIPE_SIM_H

#include <stdint.h>
#include "memory.h"
#include "cpu.h"

typedef struct {
    int forwarding;
} PipeConfig;

typedef struct {
    uint64_t instructions;
    uint64_t cycles;
    uint64_t data_stalls;
    uint64_t control_stalls;

    /* final architectural state, for oracle comparison */
    uint32_t regs[NUM_REGS];
    uint32_t pc, sp;
    uint8_t  fz, fc, fo, fn;
} PipeStats;

PipeStats pipe_run(Memory *mem, PipeConfig cfg);

#endif /* PIPE_SIM_H */
