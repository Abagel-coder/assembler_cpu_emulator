/* memory.c — memory model implementation. */
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bounds check that aborts on out-of-range access. Keeping it loud during
 * bring-up makes hand-encoded test bugs obvious instead of silently wrapping. */
static void check_bounds(uint32_t addr, uint32_t span) {
    if (addr + span > MEM_SIZE) {
        fprintf(stderr, "memory access out of bounds: addr=0x%x span=%u\n",
                addr, span);
        exit(1);
    }
}

void mem_init(Memory *m) {
    memset(m->bytes, 0, MEM_SIZE);
}

uint8_t mem_read8(Memory *m, uint32_t addr) {
    check_bounds(addr, 1);
    return m->bytes[addr];
}

void mem_write8(Memory *m, uint32_t addr, uint8_t value) {
    check_bounds(addr, 1);
    m->bytes[addr] = value;
}

/* Little-endian 32-bit read. */
uint32_t mem_read32(Memory *m, uint32_t addr) {
    check_bounds(addr, 4);
    return (uint32_t)m->bytes[addr]
         | ((uint32_t)m->bytes[addr + 1] << 8)
         | ((uint32_t)m->bytes[addr + 2] << 16)
         | ((uint32_t)m->bytes[addr + 3] << 24);
}

/* Little-endian 32-bit write. */
void mem_write32(Memory *m, uint32_t addr, uint32_t value) {
    check_bounds(addr, 4);
    m->bytes[addr]     = (uint8_t)(value & 0xFF);
    m->bytes[addr + 1] = (uint8_t)((value >> 8) & 0xFF);
    m->bytes[addr + 2] = (uint8_t)((value >> 16) & 0xFF);
    m->bytes[addr + 3] = (uint8_t)((value >> 24) & 0xFF);
}
