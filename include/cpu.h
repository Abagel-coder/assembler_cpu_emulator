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

/* When nonzero, IN/OUT perform no real I/O (used by the timing simulator,
 * which executes the program only to capture its instruction stream). */
extern int cpu_silent;

/* Program output (OUT / MMIO console) goes through cpu_emit: to stdout normally,
 * or into a capture buffer when cpu_capture is set (used by the TUI debugger). */
extern int  cpu_capture;
void        cpu_emit(const char *s);
const char *cpu_capture_buf(void);
void        cpu_capture_reset(void);

void cpu_init(CPU *cpu, Memory *mem);
void cpu_step(CPU *cpu);
void cpu_run(CPU *cpu, int trace);

#endif /* CPU_H */
