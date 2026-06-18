#include "assembler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum {
    F_NONE, F_RD_RS, F_RD, F_RD_IMM, F_RD_MEM, F_MEM_RS, F_IMM
} OpFormat;

typedef struct {
    const char *name;
    Opcode      op;
    OpFormat    fmt;
} MnemEntry;

static const MnemEntry MNEMONICS[] = {
    { "NOP", OP_NOP, F_NONE }, { "HALT", OP_HALT, F_NONE }, { "RET", OP_RET, F_NONE },
    { "ADD", OP_ADD, F_RD_RS }, { "SUB", OP_SUB, F_RD_RS }, { "MUL", OP_MUL, F_RD_RS },
    { "DIV", OP_DIV, F_RD_RS }, { "AND", OP_AND, F_RD_RS }, { "OR",  OP_OR,  F_RD_RS },
    { "XOR", OP_XOR, F_RD_RS }, { "SHL", OP_SHL, F_RD_RS }, { "SHR", OP_SHR, F_RD_RS },
    { "MOV", OP_MOV, F_RD_RS },
    { "NOT", OP_NOT, F_RD }, { "PUSH", OP_PUSH, F_RD }, { "POP", OP_POP, F_RD },
    { "IN",  OP_IN,  F_RD }, { "OUT", OP_OUT, F_RD },
    { "LOADI", OP_LOADI, F_RD_IMM },
    { "LOAD",  OP_LOAD,  F_RD_MEM },
    { "STORE", OP_STORE, F_MEM_RS },
    { "JMP", OP_JMP, F_IMM }, { "JZ", OP_JZ, F_IMM }, { "JNZ", OP_JNZ, F_IMM },
    { "JG",  OP_JG,  F_IMM }, { "JL", OP_JL, F_IMM }, { "CALL", OP_CALL, F_IMM },
};

static const MnemEntry *find_mnem(const char *name) {
    for (size_t i = 0; i < sizeof MNEMONICS / sizeof MNEMONICS[0]; i++) {
        if (strcasecmp(name, MNEMONICS[i].name) == 0) return &MNEMONICS[i];
    }
    return NULL;
}

typedef struct {
    Stmt   *items;
    size_t  count;
    size_t  cap;
} StmtVec;

static void push(StmtVec *v, Stmt s) {
    if (v->count == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 64;
        v->items = realloc(v->items, v->cap * sizeof(Stmt));
    }
    v->items[v->count++] = s;
}

typedef struct {
    const Token *t;
    size_t       n;
    size_t       i;
    int          error;
} P;

static const Token *peek(P *p)  { return &p->t[p->i]; }
static const Token *next(P *p)  { return &p->t[p->i++]; }

static int expect(P *p, TokType tt, const char *what) {
    if (peek(p)->type != tt) {
        fprintf(stderr, "parse error line %d: expected %s\n", peek(p)->line, what);
        p->error = 1;
        return 0;
    }
    p->i++;
    return 1;
}

/* Parse a signed integer operand (optional leading '-'). */
static int parse_signed(P *p, int32_t *out) {
    int neg = 0;
    if (peek(p)->type == T_MINUS) { neg = 1; p->i++; }
    if (peek(p)->type != T_NUM) {
        fprintf(stderr, "parse error line %d: expected number\n", peek(p)->line);
        p->error = 1;
        return 0;
    }
    long v = next(p)->num;
    *out = (int32_t)(neg ? -v : v);
    return 1;
}

/* Parse an immediate that may be a number or a label reference. */
static void parse_imm_or_label(P *p, Stmt *s) {
    if (peek(p)->type == T_IDENT) {
        strncpy(s->imm_label, next(p)->text, sizeof s->imm_label - 1);
    } else {
        parse_signed(p, &s->imm);
    }
}

/* Parse "[Rbase]" or "[Rbase+off]" / "[Rbase-off]" into base reg and offset. */
static int parse_mem(P *p, uint8_t *base, int32_t *off) {
    if (!expect(p, T_LBRACK, "'['")) return 0;
    if (peek(p)->type != T_REG) {
        fprintf(stderr, "parse error line %d: expected register in []\n", peek(p)->line);
        p->error = 1;
        return 0;
    }
    *base = (uint8_t)next(p)->reg;
    *off = 0;
    if (peek(p)->type == T_PLUS || peek(p)->type == T_MINUS) {
        int neg = (next(p)->type == T_MINUS);
        if (peek(p)->type != T_NUM) {
            fprintf(stderr, "parse error line %d: expected offset\n", peek(p)->line);
            p->error = 1;
            return 0;
        }
        long v = next(p)->num;
        *off = (int32_t)(neg ? -v : v);
    }
    return expect(p, T_RBRACK, "']'");
}

static uint8_t expect_reg(P *p) {
    if (peek(p)->type != T_REG) {
        fprintf(stderr, "parse error line %d: expected register\n", peek(p)->line);
        p->error = 1;
        return 0;
    }
    return (uint8_t)next(p)->reg;
}

static void parse_instr(P *p, StmtVec *out, const MnemEntry *m, int line) {
    Stmt s = { .type = ST_INSTR, .line = line, .op = m->op };

    switch (m->fmt) {
        case F_NONE:
            break;
        case F_RD_RS:
            s.rdest = expect_reg(p);
            expect(p, T_COMMA, "','");
            s.rsrc = expect_reg(p);
            break;
        case F_RD:
            s.rdest = expect_reg(p);
            break;
        case F_RD_IMM:
            s.rdest = expect_reg(p);
            expect(p, T_COMMA, "','");
            parse_imm_or_label(p, &s);
            break;
        case F_RD_MEM:
            s.rdest = expect_reg(p);
            expect(p, T_COMMA, "','");
            parse_mem(p, &s.rsrc, &s.imm);
            break;
        case F_MEM_RS:
            parse_mem(p, &s.rdest, &s.imm);
            expect(p, T_COMMA, "','");
            s.rsrc = expect_reg(p);
            break;
        case F_IMM:
            parse_imm_or_label(p, &s);
            break;
    }

    if (!p->error) push(out, s);
}

Stmt *parse(const Token *toks, size_t ntok, size_t *out_count) {
    P p = { .t = toks, .n = ntok, .i = 0, .error = 0 };
    StmtVec out = {0};

    while (peek(&p)->type != T_EOF && !p.error) {
        const Token *tk = peek(&p);

        if (tk->type == T_NEWLINE) { p.i++; continue; }

        if (tk->type == T_DIRECTIVE) {
            int line = tk->line;
            p.i++;
            if (strcasecmp(tk->text, "org") == 0) {
                int32_t v;
                if (parse_signed(&p, &v)) {
                    push(&out, (Stmt){ .type = ST_ORG, .line = line, .value = (uint32_t)v });
                }
            } else if (strcasecmp(tk->text, "word") == 0) {
                Stmt s = { .type = ST_WORD, .line = line };
                if (peek(&p)->type == T_IDENT) {
                    strncpy(s.word_label, next(&p)->text, sizeof s.word_label - 1);
                } else {
                    int32_t v; parse_signed(&p, &v); s.value = (uint32_t)v;
                }
                push(&out, s);
            } else {
                fprintf(stderr, "parse error line %d: unknown directive .%s\n", line, tk->text);
                p.error = 1;
            }
            continue;
        }

        if (tk->type == T_IDENT) {
            /* label definition "name:" or an instruction mnemonic */
            if (p.t[p.i + 1].type == T_COLON) {
                Stmt s = { .type = ST_LABEL, .line = tk->line };
                strncpy(s.name, tk->text, sizeof s.name - 1);
                push(&out, s);
                p.i += 2;
                continue;
            }
            const MnemEntry *m = find_mnem(tk->text);
            if (!m) {
                fprintf(stderr, "parse error line %d: unknown mnemonic '%s'\n",
                        tk->line, tk->text);
                p.error = 1;
                continue;
            }
            int line = tk->line;
            p.i++;
            parse_instr(&p, &out, m, line);
            continue;
        }

        fprintf(stderr, "parse error line %d: unexpected token\n", tk->line);
        p.error = 1;
    }

    if (p.error) { free(out.items); return NULL; }
    *out_count = out.count;
    return out.items;
}
