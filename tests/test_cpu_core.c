/* test_cpu_core.c — exercise the Milestone 1 CPU core with hand-encoded
 * instructions, asserting on final register and flag state. */
#include "cpu.h"
#include "isa.h"

#include <assert.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                                 \
    }                                                               \
} while (0)

/* Load a hand-encoded program at address 0 and run it to HALT. */
static void load_and_run(CPU *cpu, Memory *mem, const uint32_t *prog, size_t n) {
    mem_init(mem);
    cpu_init(cpu, mem);
    for (size_t i = 0; i < n; i++) {
        mem_write32(mem, (uint32_t)(i * 4), prog[i]);
    }
    cpu_run(cpu, 0);
}

static void test_arithmetic(void) {
    CPU cpu; Memory mem;
    /* R0 = 20; R1 = 22; R0 = R0 + R1; HALT  -> R0 == 42 */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 20),
        ENCODE_INSTR(OP_LOADI, 1, 0, 22),
        ENCODE_INSTR(OP_ADD,   0, 1, 0),
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 42);
    CHECK(cpu.halted == 1);
    CHECK(cpu.flags.z == 0);
}

static void test_sub_to_zero_sets_zflag(void) {
    CPU cpu; Memory mem;
    /* R0 = 7; R1 = 7; R0 = R0 - R1 -> 0, Z flag set */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 7),
        ENCODE_INSTR(OP_LOADI, 1, 0, 7),
        ENCODE_INSTR(OP_SUB,   0, 1, 0),
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 0);
    CHECK(cpu.flags.z == 1);
}

static void test_logic_and_shift(void) {
    CPU cpu; Memory mem;
    /* R0 = 6 (0b110); R1 = 3; R0 = R0 << R1 -> 48; R2 = 12; R0 = R0 & R2 -> 16 */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 6),
        ENCODE_INSTR(OP_LOADI, 1, 0, 3),
        ENCODE_INSTR(OP_SHL,   0, 1, 0),   /* 6 << 3 = 48 */
        ENCODE_INSTR(OP_LOADI, 2, 0, 16),
        ENCODE_INSTR(OP_AND,   0, 2, 0),   /* 48 & 16 = 16 */
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 16);
}

static void test_mul_div(void) {
    CPU cpu; Memory mem;
    /* R0 = 6; R1 = 7; R0 = R0 * R1 -> 42; R2 = 2; R0 = R0 / R2 -> 21 */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 6),
        ENCODE_INSTR(OP_LOADI, 1, 0, 7),
        ENCODE_INSTR(OP_MUL,   0, 1, 0),
        ENCODE_INSTR(OP_LOADI, 2, 0, 2),
        ENCODE_INSTR(OP_DIV,   0, 2, 0),
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 21);
}

static void test_div_by_zero_halts(void) {
    CPU cpu; Memory mem;
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 10),
        ENCODE_INSTR(OP_LOADI, 1, 0, 0),
        ENCODE_INSTR(OP_DIV,   0, 1, 0),   /* div by zero -> halt */
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.halted == 1);
}

static void test_jmp(void) {
    CPU cpu; Memory mem;
    /* JMP over a poison LOADI that would set R0=99; correct R0 stays 0. */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_JMP,   0, 0, 8),   /* skip to address 8 */
        ENCODE_INSTR(OP_LOADI, 0, 0, 99),  /* address 4 — should be skipped */
        ENCODE_INSTR(OP_HALT,  0, 0, 0),   /* address 8 */
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 0);
}

static void test_negative_immediate(void) {
    CPU cpu; Memory mem;
    /* LOADI R0, -1 -> 0xFFFFFFFF via 20-bit sign extension. */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, -1),
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 0xFFFFFFFFu);
    CHECK(cpu.flags.n == 0);  /* LOADI does not touch flags */
}

static void test_mov_and_memory(void) {
    CPU cpu; Memory mem;
    /* R0 = 1234; STORE [R1+0] = R0; LOAD R2 = [R1+0]; MOV R3 = R2 */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 1234),
        ENCODE_INSTR(OP_LOADI, 1, 0, 4096),
        ENCODE_INSTR(OP_STORE, 1, 0, 0),
        ENCODE_INSTR(OP_LOAD,  2, 1, 0),
        ENCODE_INSTR(OP_MOV,   3, 2, 0),
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[2] == 1234);
    CHECK(cpu.regs[3] == 1234);
}

static void test_push_pop(void) {
    CPU cpu; Memory mem;
    /* push 11 then 22, pop into R1 (22) then R2 (11) */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 11),
        ENCODE_INSTR(OP_PUSH,  0, 0, 0),
        ENCODE_INSTR(OP_LOADI, 0, 0, 22),
        ENCODE_INSTR(OP_PUSH,  0, 0, 0),
        ENCODE_INSTR(OP_POP,   1, 0, 0),
        ENCODE_INSTR(OP_POP,   2, 0, 0),
        ENCODE_INSTR(OP_HALT,  0, 0, 0),
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[1] == 22);
    CHECK(cpu.regs[2] == 11);
    CHECK(cpu.sp == MEM_SIZE);
}

static void test_call_ret(void) {
    CPU cpu; Memory mem;
    /* main: CALL sub; HALT.  sub: R0=7; RET. */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_CALL,  0, 0, 12),  /* addr 0 -> call addr 12 */
        ENCODE_INSTR(OP_LOADI, 1, 0, 5),   /* addr 4 (after return) */
        ENCODE_INSTR(OP_HALT,  0, 0, 0),   /* addr 8 */
        ENCODE_INSTR(OP_LOADI, 0, 0, 7),   /* addr 12: sub */
        ENCODE_INSTR(OP_RET,   0, 0, 0),   /* addr 16 */
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 7);
    CHECK(cpu.regs[1] == 5);
    CHECK(cpu.sp == MEM_SIZE);
}

static void test_conditional_jumps(void) {
    CPU cpu; Memory mem;
    /* R0=3; loop: R0=R0-1 (R1=1 decrement); JNZ loop; -> R0 hits 0 */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 3),   /* addr 0 */
        ENCODE_INSTR(OP_LOADI, 1, 0, 1),   /* addr 4 */
        ENCODE_INSTR(OP_SUB,   0, 1, 0),   /* addr 8: loop body */
        ENCODE_INSTR(OP_JNZ,   0, 0, 8),   /* addr 12: back to loop */
        ENCODE_INSTR(OP_HALT,  0, 0, 0),   /* addr 16 */
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[0] == 0);
    CHECK(cpu.flags.z == 1);
}

static void test_signed_jg_jl(void) {
    CPU cpu; Memory mem;
    /* R0=5, R1=9; R0=R0-R1 -> negative; JL taken sets R2=1 */
    uint32_t prog[] = {
        ENCODE_INSTR(OP_LOADI, 0, 0, 5),
        ENCODE_INSTR(OP_LOADI, 1, 0, 9),
        ENCODE_INSTR(OP_SUB,   0, 1, 0),
        ENCODE_INSTR(OP_JL,    0, 0, 20),  /* addr 12 -> jump to addr 20 */
        ENCODE_INSTR(OP_HALT,  0, 0, 0),   /* addr 16 (skipped) */
        ENCODE_INSTR(OP_LOADI, 2, 0, 1),   /* addr 20 */
        ENCODE_INSTR(OP_HALT,  0, 0, 0),   /* addr 24 */
    };
    load_and_run(&cpu, &mem, prog, sizeof prog / sizeof prog[0]);
    CHECK(cpu.regs[2] == 1);
}

int main(void) {
    test_arithmetic();
    test_sub_to_zero_sets_zflag();
    test_logic_and_shift();
    test_mul_div();
    test_div_by_zero_halts();
    test_jmp();
    test_negative_immediate();
    test_mov_and_memory();
    test_push_pop();
    test_call_ret();
    test_conditional_jumps();
    test_signed_jg_jl();

    if (failures == 0) {
        printf("all CPU core tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
