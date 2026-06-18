#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include "memory.h"

#define NUM_REGS 8

typedef struct {
    uint32_t regs[NUM_REGS];
    uint32_t pc;
    uint32_t sp;
    struct { uint8_t z, c, o, n; } flags;   /* zero, carry, overflow, negative */
    Memory  *mem;
    int      halted;
    uint64_t icount;
} CPU;

void cpu_init(CPU *cpu, Memory *mem);
void cpu_step(CPU *cpu);
void cpu_run(CPU *cpu, int trace);

#endif /* CPU_H */
