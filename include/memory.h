/* Flat 64KB byte-addressable memory; words are little-endian. */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define MEM_SIZE (1 << 16)

typedef struct {
    uint8_t bytes[MEM_SIZE];
} Memory;

void     mem_init(Memory *m);
uint8_t  mem_read8(Memory *m, uint32_t addr);
void     mem_write8(Memory *m, uint32_t addr, uint8_t value);
uint32_t mem_read32(Memory *m, uint32_t addr);
void     mem_write32(Memory *m, uint32_t addr, uint32_t value);

#endif /* MEMORY_H */
