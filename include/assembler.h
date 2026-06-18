#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <stddef.h>
#include <stdint.h>
#include "isa.h"

typedef enum {
    T_EOF, T_NEWLINE, T_IDENT, T_REG, T_NUM,
    T_COMMA, T_LBRACK, T_RBRACK, T_PLUS, T_MINUS, T_COLON, T_DIRECTIVE
} TokType;

typedef struct {
    TokType type;
    char    text[64];
    long    num;
    int     reg;
    int     line;
} Token;

typedef enum { ST_INSTR, ST_LABEL, ST_ORG, ST_WORD } StmtType;

typedef struct {
    StmtType type;
    int      line;

    char     name[64];       /* ST_LABEL */

    Opcode   op;             /* ST_INSTR */
    uint8_t  rdest, rsrc;
    int32_t  imm;
    char     imm_label[64];  /* set when the immediate is a label */

    uint32_t value;          /* ST_ORG / ST_WORD */
    char     word_label[64];

    uint32_t address;        /* assigned in pass 1 */
} Stmt;

typedef struct {
    char     name[64];
    uint32_t address;
} Symbol;

typedef struct {
    Symbol  *symbols;
    size_t   count;
    size_t   cap;
} SymbolTable;

Token *lex(const char *src, size_t *out_count);
Stmt  *parse(const Token *toks, size_t ntok, size_t *out_count);
int    assemble(const char *src_path, const char *out_path);

#endif /* ASSEMBLER_H */
