#include "device.h"

#include <stdio.h>

int mmio_is_device(uint32_t addr) {
    return addr >= MMIO_BASE && addr < MMIO_END;
}

uint32_t mmio_load(uint32_t addr) {
    if (addr == MMIO_IN && !cpu_silent) {
        unsigned long v = 0;
        if (scanf("%lu", &v) != 1) v = 0;
        return (uint32_t)v;
    }
    return 0;
}

void mmio_store(CPU *cpu, uint32_t addr, uint32_t value) {
    char b[16];
    switch (addr) {
        case MMIO_OUT:  if (!cpu_silent) { snprintf(b, sizeof b, "%u\n", value); cpu_emit(b); } break;
        case MMIO_OUTC: if (!cpu_silent) { b[0] = (char)(value & 0xFF); b[1] = '\0'; cpu_emit(b); } break;
        case MMIO_HALT: cpu->halted = 1; break;   /* control effect: never suppressed */
        default: break;
    }
}
