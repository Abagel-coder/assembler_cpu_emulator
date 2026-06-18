/* cpu.c — fetch-decode-execute core. */
#include "cpu.h"
#include "isa.h"

#include <stdio.h>

void cpu_init(CPU *cpu, Memory *mem) {
    for (int i = 0; i < NUM_REGS; i++) {
        cpu->regs[i] = 0;
    }
    cpu->pc = 0;
    cpu->sp = MEM_SIZE;   /* stack grows downward from the top of memory */
    cpu->flags.z = cpu->flags.c = cpu->flags.o = cpu->flags.n = 0;
    cpu->mem = mem;
    cpu->halted = 0;
}

/* Set zero/negative flags from a result word. Carry/overflow are set by the
 * individual arithmetic ops that can produce them. */
static void set_zn(CPU *cpu, uint32_t result) {
    cpu->flags.z = (result == 0);
    cpu->flags.n = (result >> 31) & 1;
}

static void exec_add(CPU *cpu, Instruction ins) {
    uint32_t a = cpu->regs[ins.rdest];
    uint32_t b = cpu->regs[ins.rsrc];
    uint64_t wide = (uint64_t)a + (uint64_t)b;
    uint32_t r = (uint32_t)wide;
    cpu->regs[ins.rdest] = r;
    cpu->flags.c = (wide >> 32) & 1;
    /* signed overflow: operands same sign, result differs */
    cpu->flags.o = (~(a ^ b) & (a ^ r)) >> 31;
    set_zn(cpu, r);
}

static void exec_sub(CPU *cpu, Instruction ins) {
    uint32_t a = cpu->regs[ins.rdest];
    uint32_t b = cpu->regs[ins.rsrc];
    uint32_t r = a - b;
    cpu->regs[ins.rdest] = r;
    cpu->flags.c = (a < b);                       /* borrow */
    cpu->flags.o = ((a ^ b) & (a ^ r)) >> 31;     /* signed overflow */
    set_zn(cpu, r);
}

void cpu_step(CPU *cpu) {
    uint32_t raw = mem_read32(cpu->mem, cpu->pc);
    Instruction ins = {
        .opcode = DECODE_OPCODE(raw),
        .rdest  = DECODE_RDEST(raw),
        .rsrc   = DECODE_RSRC(raw),
        .imm    = DECODE_IMM(raw)
    };
    cpu->pc += 4;

    switch (ins.opcode) {
        case OP_NOP:
            break;
        case OP_HALT:
            cpu->halted = 1;
            break;

        case OP_ADD:
            exec_add(cpu, ins);
            break;
        case OP_SUB:
            exec_sub(cpu, ins);
            break;
        case OP_MUL: {
            uint32_t r = cpu->regs[ins.rdest] * cpu->regs[ins.rsrc];
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }
        case OP_DIV: {
            uint32_t divisor = cpu->regs[ins.rsrc];
            if (divisor == 0) {
                fprintf(stderr, "division by zero at PC=0x%x\n", cpu->pc - 4);
                cpu->halted = 1;
                break;
            }
            uint32_t r = cpu->regs[ins.rdest] / divisor;
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }
        case OP_AND: {
            uint32_t r = cpu->regs[ins.rdest] & cpu->regs[ins.rsrc];
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }
        case OP_OR: {
            uint32_t r = cpu->regs[ins.rdest] | cpu->regs[ins.rsrc];
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }
        case OP_XOR: {
            uint32_t r = cpu->regs[ins.rdest] ^ cpu->regs[ins.rsrc];
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }
        case OP_NOT: {
            uint32_t r = ~cpu->regs[ins.rdest];
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }
        case OP_SHL: {
            uint32_t r = cpu->regs[ins.rdest] << cpu->regs[ins.rsrc];
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }
        case OP_SHR: {
            uint32_t r = cpu->regs[ins.rdest] >> cpu->regs[ins.rsrc];
            cpu->regs[ins.rdest] = r;
            set_zn(cpu, r);
            break;
        }

        case OP_MOV:
            cpu->regs[ins.rdest] = cpu->regs[ins.rsrc];
            break;
        case OP_LOAD:
            cpu->regs[ins.rdest] =
                mem_read32(cpu->mem, cpu->regs[ins.rsrc] + (uint32_t)ins.imm);
            break;
        case OP_STORE:
            mem_write32(cpu->mem, cpu->regs[ins.rdest] + (uint32_t)ins.imm,
                        cpu->regs[ins.rsrc]);
            break;
        case OP_LOADI:
            cpu->regs[ins.rdest] = (uint32_t)ins.imm;
            break;
        case OP_PUSH:
            cpu->sp -= 4;
            mem_write32(cpu->mem, cpu->sp, cpu->regs[ins.rdest]);
            break;
        case OP_POP:
            cpu->regs[ins.rdest] = mem_read32(cpu->mem, cpu->sp);
            cpu->sp += 4;
            break;

        case OP_JMP:
            cpu->pc = (uint32_t)ins.imm;
            break;
        case OP_JZ:
            if (cpu->flags.z) cpu->pc = (uint32_t)ins.imm;
            break;
        case OP_JNZ:
            if (!cpu->flags.z) cpu->pc = (uint32_t)ins.imm;
            break;
        case OP_JG:
            if (!cpu->flags.z && cpu->flags.n == cpu->flags.o)
                cpu->pc = (uint32_t)ins.imm;
            break;
        case OP_JL:
            if (cpu->flags.n != cpu->flags.o)
                cpu->pc = (uint32_t)ins.imm;
            break;
        case OP_CALL:
            cpu->sp -= 4;
            mem_write32(cpu->mem, cpu->sp, cpu->pc);
            cpu->pc = (uint32_t)ins.imm;
            break;
        case OP_RET:
            cpu->pc = mem_read32(cpu->mem, cpu->sp);
            cpu->sp += 4;
            break;

        case OP_IN: {
            unsigned long v = 0;
            if (scanf("%lu", &v) != 1) v = 0;
            cpu->regs[ins.rdest] = (uint32_t)v;
            break;
        }
        case OP_OUT:
            printf("%u\n", cpu->regs[ins.rdest]);
            break;

        default:
            fprintf(stderr, "Illegal/unimplemented opcode 0x%x at PC=0x%x\n",
                    ins.opcode, cpu->pc - 4);
            cpu->halted = 1;
    }
}

static void trace_line(const CPU *cpu) {
    uint32_t raw = mem_read32(cpu->mem, cpu->pc);
    Opcode op = DECODE_OPCODE(raw);
    fprintf(stderr, "PC=%04x  %-5s rd=%u rs=%u imm=%-6d | ",
            cpu->pc, opcode_name(op), DECODE_RDEST(raw), DECODE_RSRC(raw),
            DECODE_IMM(raw));
    for (int i = 0; i < NUM_REGS; i++) {
        fprintf(stderr, "R%d=%u ", i, cpu->regs[i]);
    }
    fprintf(stderr, "SP=%u Z%d C%d O%d N%d\n",
            cpu->sp, cpu->flags.z, cpu->flags.c, cpu->flags.o, cpu->flags.n);
}

void cpu_run(CPU *cpu, int trace) {
    while (!cpu->halted) {
        if (trace) trace_line(cpu);
        cpu_step(cpu);
    }
}
