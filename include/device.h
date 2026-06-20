#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include "cpu.h"

/* Memory-mapped device registers. LOAD/STORE to these addresses hit devices
 * instead of RAM. */
#define MMIO_BASE  0xE000u
#define MMIO_OUT   0xE000u   /* store: print value as a decimal integer */
#define MMIO_OUTC  0xE004u   /* store: print low byte as an ASCII character */
#define MMIO_IN    0xE008u   /* load:  read a decimal integer from stdin */
#define MMIO_HALT  0xE00Cu   /* store: halt the CPU */
#define MMIO_END   0xE010u

int      mmio_is_device(uint32_t addr);
uint32_t mmio_load(uint32_t addr);
void     mmio_store(CPU *cpu, uint32_t addr, uint32_t value);

#endif /* DEVICE_H */
