/* 32-bit instruction layout:
 *  31     26 25  23 22  20 19                0
 * +--------+------+------+-------------------+
 * | opcode | rdest| rsrc | immediate / offset|
 * |   6    |  3   |  3   |        20         |
 * +--------+------+------+-------------------+
 */
#ifndef ISA_H
#define ISA_H

#include <stdint.h>

typedef enum {
    OP_NOP = 0x00, OP_HALT,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_AND, OP_OR,  OP_XOR, OP_NOT, OP_SHL, OP_SHR,
    OP_MOV, OP_LOAD, OP_STORE, OP_LOADI, OP_PUSH, OP_POP,
    OP_JMP, OP_JZ,  OP_JNZ, OP_JG,  OP_JL,  OP_CALL, OP_RET,
    OP_IN,  OP_OUT
} Opcode;

typedef struct {
    Opcode  opcode;
    uint8_t rdest;
    uint8_t rsrc;
    int32_t imm;
} Instruction;

#define OPCODE_MASK   0xFC000000u
#define RDEST_MASK    0x03800000u
#define RSRC_MASK     0x00700000u
#define IMM_MASK      0x000FFFFFu

#define IMM_BITS      20
#define IMM_SIGN_BIT  0x00080000u

static inline int32_t sign_extend20(uint32_t v) {
    v &= IMM_MASK;
    if (v & IMM_SIGN_BIT) {
        v |= ~IMM_MASK;
    }
    return (int32_t)v;
}

#define DECODE_OPCODE(w)  ((Opcode)(((w) & OPCODE_MASK) >> 26))
#define DECODE_RDEST(w)   ((uint8_t)(((w) & RDEST_MASK) >> 23))
#define DECODE_RSRC(w)    ((uint8_t)(((w) & RSRC_MASK)  >> 20))
#define DECODE_IMM(w)     sign_extend20((w) & IMM_MASK)

#define ENCODE_INSTR(op, rd, rs, imm) \
    (((uint32_t)(op) << 26) | ((uint32_t)(rd) << 23) | \
     ((uint32_t)(rs) << 20) | ((uint32_t)(imm) & IMM_MASK))

typedef enum {
    OPF_NONE, OPF_RD_RS, OPF_RD, OPF_RD_IMM, OPF_RD_MEM, OPF_MEM_RS, OPF_IMM
} OpFormat;

const char *opcode_name(Opcode op);
OpFormat    opcode_format(Opcode op);

#endif /* ISA_H */
