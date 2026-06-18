#include "assembler.h"
#include "cpu.h"
#include "memory.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                                 \
    }                                                               \
} while (0)

static int build_and_run(const char *asm_path, CPU *cpu, Memory *mem) {
    const char *bin = "/tmp/integ.bin";
    if (assemble(asm_path, bin) != 0) return -1;

    mem_init(mem);
    FILE *f = fopen(bin, "rb");
    if (!f) return -1;
    fread(mem->bytes, 1, MEM_SIZE, f);
    fclose(f);

    cpu_init(cpu, mem);
    cpu_run(cpu, 0);
    return 0;
}

static void test_factorial(void) {
    CPU cpu; Memory mem;
    CHECK(build_and_run("examples/factorial.asm", &cpu, &mem) == 0);
    CHECK(cpu.regs[0] == 120);
}

static void test_fibonacci(void) {
    CPU cpu; Memory mem;
    CHECK(build_and_run("examples/fibonacci.asm", &cpu, &mem) == 0);
    CHECK(cpu.regs[0] == 55);
}

static void test_bubble_sort(void) {
    CPU cpu; Memory mem;
    CHECK(build_and_run("examples/bubble_sort.asm", &cpu, &mem) == 0);
    uint32_t expected[5] = { 1, 2, 3, 4, 5 };
    for (int i = 0; i < 5; i++) {
        CHECK(mem_read32(&mem, 256 + i * 4) == expected[i]);
    }
}

int main(void) {
    test_factorial();
    test_fibonacci();
    test_bubble_sort();

    if (failures == 0) {
        printf("all integration tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
