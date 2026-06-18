#include "assembler.h"
#include "cpu.h"
#include "memory.h"
#include "pipe_sim.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                                 \
    }                                                               \
} while (0)

static int load_bin(Memory *mem, const char *bin) {
    mem_init(mem);
    FILE *f = fopen(bin, "rb");
    if (!f) return -1;
    fread(mem->bytes, 1, MEM_SIZE, f);
    fclose(f);
    return 0;
}

/* The structural sim's final architectural state must match a plain cpu.c run. */
static void check_oracle(const char *asmf) {
    const char *bin = "/tmp/pipe_test.bin";
    CHECK(assemble(asmf, bin) == 0);

    Memory mref, mpipe;
    CHECK(load_bin(&mref, bin) == 0);
    CHECK(load_bin(&mpipe, bin) == 0);

    CPU ref;
    cpu_init(&ref, &mref);
    cpu_run(&ref, 0);

    PipeStats st = pipe_run(&mpipe, (PipeConfig){ .forwarding = 1 });

    for (int i = 0; i < NUM_REGS; i++) CHECK(st.regs[i] == ref.regs[i]);
    CHECK(st.pc == ref.pc);
    CHECK(st.sp == ref.sp);
    CHECK(st.instructions == ref.icount);
    CHECK(memcmp(mref.bytes, mpipe.bytes, MEM_SIZE) == 0);
}

static void run_counts(const char *asmf, uint64_t *instrs,
                       uint64_t *cyc_nofwd, uint64_t *cyc_fwd) {
    const char *bin = "/tmp/pipe_test.bin";
    assemble(asmf, bin);
    Memory m1, m2;
    load_bin(&m1, bin);
    load_bin(&m2, bin);
    PipeStats a = pipe_run(&m1, (PipeConfig){ .forwarding = 0 });
    PipeStats b = pipe_run(&m2, (PipeConfig){ .forwarding = 1 });
    *instrs = b.instructions;
    *cyc_nofwd = a.cycles;
    *cyc_fwd = b.cycles;
}

/* Cycle counts locked against the validated analytical model. */
static void check_counts(const char *asmf, uint64_t exp_instr,
                         uint64_t exp_nofwd, uint64_t exp_fwd) {
    uint64_t instr, nofwd, fwd;
    run_counts(asmf, &instr, &nofwd, &fwd);
    CHECK(instr == exp_instr);
    CHECK(nofwd == exp_nofwd);
    CHECK(fwd == exp_fwd);
    CHECK(fwd <= nofwd);
    CHECK(fwd >= instr + 4);   /* pipeline fill overhead */
}

int main(void) {
    check_oracle("examples/factorial.asm");
    check_oracle("examples/fibonacci.asm");
    check_oracle("examples/bubble_sort.asm");

    check_counts("examples/factorial.asm", 20, 43, 32);
    check_counts("examples/fibonacci.asm", 66, 138, 88);
    check_counts("examples/bubble_sort.asm", 144, 299, 199);

    if (failures == 0) {
        printf("all pipeline tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
