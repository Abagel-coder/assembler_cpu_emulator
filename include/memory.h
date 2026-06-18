/* memory.h — flat 64KB byte-addressable memory with word helpers.
 *
 * Words are stored little-endian. All addresses are byte addresses.
 */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define MEM_SIZE (1 << 16)   /* 64KB */

typedef struct {
    uint8_t bytes[MEM_SIZE];
} Memory;

void     mem_init(Memory *m);
uint8_t  mem_read8(Memory *m, uint32_t addr);
void     mem_write8(Memory *m, uint32_t addr, uint8_t value);
uint32_t mem_read32(Memory *m, uint32_t addr);
void     mem_write32(Memory *m, uint32_t addr, uint32_t value);

#endif /* MEMORY_H */
