#include "assembler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Token  *items;
    size_t  count;
    size_t  cap;
} TokVec;

static void push(TokVec *v, Token t) {
    if (v->count == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 64;
        v->items = realloc(v->items, v->cap * sizeof(Token));
    }
    v->items[v->count++] = t;
}

static int is_reg(const char *s, int *out) {
    if ((s[0] == 'R' || s[0] == 'r') && isdigit((unsigned char)s[1]) && s[2] == '\0') {
        int n = s[1] - '0';
        if (n >= 0 && n < 8) { *out = n; return 1; }
    }
    return 0;
}

Token *lex(const char *src, size_t *out_count) {
    TokVec v = {0};
    int line = 1;
    const char *p = src;

    while (*p) {
        if (*p == '\n') {
            push(&v, (Token){ .type = T_NEWLINE, .line = line });
            line++; p++;
            continue;
        }
        if (*p == ';') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (isspace((unsigned char)*p)) { p++; continue; }

        if (*p == ',') { push(&v, (Token){ .type = T_COMMA,  .line = line }); p++; continue; }
        if (*p == '[') { push(&v, (Token){ .type = T_LBRACK, .line = line }); p++; continue; }
        if (*p == ']') { push(&v, (Token){ .type = T_RBRACK, .line = line }); p++; continue; }
        if (*p == '+') { push(&v, (Token){ .type = T_PLUS,   .line = line }); p++; continue; }
        if (*p == ':') { push(&v, (Token){ .type = T_COLON,  .line = line }); p++; continue; }

        if (*p == '-') {
            push(&v, (Token){ .type = T_MINUS, .line = line }); p++;
            continue;
        }

        if (isdigit((unsigned char)*p)) {
            char *end;
            long val = strtol(p, &end, 0);   /* base 0 handles 0x and decimal */
            push(&v, (Token){ .type = T_NUM, .num = val, .line = line });
            p = end;
            continue;
        }

        if (*p == '.') {
            Token t = { .type = T_DIRECTIVE, .line = line };
            size_t i = 0; p++;
            while (isalnum((unsigned char)*p) && i < sizeof t.text - 1) t.text[i++] = *p++;
            t.text[i] = '\0';
            push(&v, t);
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            Token t = { .line = line };
            size_t i = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && i < sizeof t.text - 1)
                t.text[i++] = *p++;
            t.text[i] = '\0';
            int reg;
            if (is_reg(t.text, &reg)) {
                t.type = T_REG; t.reg = reg;
            } else {
                t.type = T_IDENT;
            }
            push(&v, t);
            continue;
        }

        fprintf(stderr, "lex error: unexpected character '%c' on line %d\n", *p, line);
        free(v.items);
        return NULL;
    }

    push(&v, (Token){ .type = T_EOF, .line = line });
    *out_count = v.count;
    return v.items;
}
