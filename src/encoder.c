#include "assembler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMM_MIN (-(1 << (IMM_BITS - 1)))
#define IMM_MAX ((1 << (IMM_BITS - 1)) - 1)

static void sym_add(SymbolTable *t, const char *name, uint32_t addr) {
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 32;
        t->symbols = realloc(t->symbols, t->cap * sizeof(Symbol));
    }
    strncpy(t->symbols[t->count].name, name, sizeof t->symbols[t->count].name - 1);
    t->symbols[t->count].name[sizeof t->symbols[t->count].name - 1] = '\0';
    t->symbols[t->count].address = addr;
    t->count++;
}

static int sym_lookup(const SymbolTable *t, const char *name, uint32_t *out) {
    for (size_t i = 0; i < t->count; i++) {
        if (strcmp(t->symbols[i].name, name) == 0) { *out = t->symbols[i].address; return 1; }
    }
    return 0;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t rd = fread(buf, 1, (size_t)n, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

int assemble(const char *src_path, const char *out_path) {
    char *src = read_file(src_path);
    if (!src) return 1;

    size_t ntok = 0;
    Token *toks = lex(src, &ntok);
    free(src);
    if (!toks) return 1;

    size_t nst = 0;
    Stmt *stmts = parse(toks, ntok, &nst);
    free(toks);
    if (!stmts) return 1;

    /* Pass 1: assign addresses and collect labels. */
    SymbolTable syms = {0};
    uint32_t addr = 0, max_addr = 0;
    for (size_t i = 0; i < nst; i++) {
        Stmt *s = &stmts[i];
        switch (s->type) {
            case ST_ORG:
                addr = s->value;
                break;
            case ST_LABEL:
                sym_add(&syms, s->name, addr);
                break;
            case ST_INSTR:
            case ST_WORD:
                s->address = addr;
                addr += 4;
                if (addr > max_addr) max_addr = addr;
                break;
        }
    }

    /* Pass 2: resolve labels and encode. */
    uint8_t *out = calloc(max_addr ? max_addr : 4, 1);
    int rc = 0;
    for (size_t i = 0; i < nst && rc == 0; i++) {
        Stmt *s = &stmts[i];
        uint32_t word = 0;

        if (s->type == ST_INSTR) {
            int32_t imm = s->imm;
            if (s->imm_label[0]) {
                uint32_t target;
                if (!sym_lookup(&syms, s->imm_label, &target)) {
                    fprintf(stderr, "error line %d: undefined label '%s'\n",
                            s->line, s->imm_label);
                    rc = 1; break;
                }
                imm = (int32_t)target;
            }
            if (imm < IMM_MIN || imm > IMM_MAX) {
                fprintf(stderr, "error line %d: immediate %d out of 20-bit range\n",
                        s->line, imm);
                rc = 1; break;
            }
            word = ENCODE_INSTR(s->op, s->rdest, s->rsrc, imm);
        } else if (s->type == ST_WORD) {
            word = s->value;
            if (s->word_label[0]) {
                uint32_t target;
                if (!sym_lookup(&syms, s->word_label, &target)) {
                    fprintf(stderr, "error line %d: undefined label '%s'\n",
                            s->line, s->word_label);
                    rc = 1; break;
                }
                word = target;
            }
        } else {
            continue;
        }

        out[s->address]     = (uint8_t)(word & 0xFF);
        out[s->address + 1] = (uint8_t)((word >> 8) & 0xFF);
        out[s->address + 2] = (uint8_t)((word >> 16) & 0xFF);
        out[s->address + 3] = (uint8_t)((word >> 24) & 0xFF);
    }

    if (rc == 0) {
        FILE *f = fopen(out_path, "wb");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", out_path);
            rc = 1;
        } else {
            fwrite(out, 1, max_addr ? max_addr : 0, f);
            fclose(f);
        }
    }

    free(out);
    free(syms.symbols);
    free(stmts);
    return rc;
}
