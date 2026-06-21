#include "isa.h"

#include <stdio.h>

const char *opcode_name(Opcode op) {
    switch (op) {
        case OP_NOP:   return "NOP";
        case OP_HALT:  return "HALT";
        case OP_ADD:   return "ADD";
        case OP_SUB:   return "SUB";
        case OP_MUL:   return "MUL";
        case OP_DIV:   return "DIV";
        case OP_AND:   return "AND";
        case OP_OR:    return "OR";
        case OP_XOR:   return "XOR";
        case OP_NOT:   return "NOT";
        case OP_SHL:   return "SHL";
        case OP_SHR:   return "SHR";
        case OP_MOV:   return "MOV";
        case OP_LOAD:  return "LOAD";
        case OP_STORE: return "STORE";
        case OP_LOADI: return "LOADI";
        case OP_PUSH:  return "PUSH";
        case OP_POP:   return "POP";
        case OP_JMP:   return "JMP";
        case OP_JZ:    return "JZ";
        case OP_JNZ:   return "JNZ";
        case OP_JG:    return "JG";
        case OP_JL:    return "JL";
        case OP_CALL:  return "CALL";
        case OP_RET:   return "RET";
        case OP_IN:    return "IN";
        case OP_OUT:   return "OUT";
        default:       return "???";
    }
}

OpFormat opcode_format(Opcode op) {
    switch (op) {
        case OP_NOP: case OP_HALT: case OP_RET:
            return OPF_NONE;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_AND: case OP_OR:  case OP_XOR: case OP_SHL:
        case OP_SHR: case OP_MOV:
            return OPF_RD_RS;
        case OP_NOT: case OP_PUSH: case OP_POP: case OP_IN: case OP_OUT:
            return OPF_RD;
        case OP_LOADI:
            return OPF_RD_IMM;
        case OP_LOAD:
            return OPF_RD_MEM;
        case OP_STORE:
            return OPF_MEM_RS;
        case OP_JMP: case OP_JZ: case OP_JNZ:
        case OP_JG:  case OP_JL: case OP_CALL:
            return OPF_IMM;
        default:
            return OPF_NONE;
    }
}

void isa_disasm(char *buf, size_t n, uint32_t word) {
    Opcode  op = DECODE_OPCODE(word);
    uint8_t rd = DECODE_RDEST(word), rs = DECODE_RSRC(word);
    int32_t imm = DECODE_IMM(word);
    const char *nm = opcode_name(op);

    switch (opcode_format(op)) {
        case OPF_NONE:   snprintf(buf, n, "%s", nm); break;
        case OPF_RD_RS:  snprintf(buf, n, "%s R%u, R%u", nm, rd, rs); break;
        case OPF_RD:     snprintf(buf, n, "%s R%u", nm, rd); break;
        case OPF_RD_IMM: snprintf(buf, n, "%s R%u, %d", nm, rd, imm); break;
        case OPF_RD_MEM:
            if (imm < 0) snprintf(buf, n, "%s R%u, [R%u-%d]", nm, rd, rs, -imm);
            else         snprintf(buf, n, "%s R%u, [R%u+%d]", nm, rd, rs, imm);
            break;
        case OPF_MEM_RS:
            if (imm < 0) snprintf(buf, n, "%s [R%u-%d], R%u", nm, rd, -imm, rs);
            else         snprintf(buf, n, "%s [R%u+%d], R%u", nm, rd, imm, rs);
            break;
        case OPF_IMM:    snprintf(buf, n, "%s %d", nm, imm); break;
    }
}
