#include "compiler.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Token *items; size_t n, cap; } Vec;

static void push(Vec *v, Token t) {
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 128;
        v->items = realloc(v->items, v->cap * sizeof(Token));
    }
    v->items[v->n++] = t;
}

static int keyword(const char *s) {
    if (!strcmp(s, "int")) return T_KW_INT;
    if (!strcmp(s, "void")) return T_KW_VOID;
    if (!strcmp(s, "if")) return T_KW_IF;
    if (!strcmp(s, "else")) return T_KW_ELSE;
    if (!strcmp(s, "while")) return T_KW_WHILE;
    if (!strcmp(s, "for")) return T_KW_FOR;
    if (!strcmp(s, "return")) return T_KW_RETURN;
    return T_IDENT;
}

Token *lex(const char *src, size_t *out_n, char *err, size_t errcap) {
    Vec v = {0};
    int line = 1;
    const char *p = src;

    while (*p) {
        if (*p == '\n') { line++; p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') line++; p++; }
            if (*p) p += 2;
            continue;
        }

        Token t = { .line = line };

        if (isdigit((unsigned char)*p)) {
            char *e; t.type = T_INT_LIT; t.ival = strtol(p, &e, 0); p = e;
            push(&v, t); continue;
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            size_t i = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && i < sizeof t.text - 1) t.text[i++] = *p++;
            t.text[i] = '\0';
            t.type = keyword(t.text);
            push(&v, t); continue;
        }

        /* two-char operators */
        int two = 1;
        if (p[0] == '<' && p[1] == '<') t.type = T_SHL;
        else if (p[0] == '>' && p[1] == '>') t.type = T_SHR;
        else if (p[0] == '=' && p[1] == '=') t.type = T_EQ;
        else if (p[0] == '!' && p[1] == '=') t.type = T_NE;
        else if (p[0] == '<' && p[1] == '=') t.type = T_LE;
        else if (p[0] == '>' && p[1] == '=') t.type = T_GE;
        else if (p[0] == '&' && p[1] == '&') t.type = T_ANDAND;
        else if (p[0] == '|' && p[1] == '|') t.type = T_OROR;
        else two = 0;
        if (two) { p += 2; push(&v, t); continue; }

        switch (*p) {
            case '(': t.type = T_LPAREN; break; case ')': t.type = T_RPAREN; break;
            case '{': t.type = T_LBRACE; break; case '}': t.type = T_RBRACE; break;
            case '[': t.type = T_LBRACK; break; case ']': t.type = T_RBRACK; break;
            case ';': t.type = T_SEMI; break;   case ',': t.type = T_COMMA; break;
            case '=': t.type = T_ASSIGN; break;  case '+': t.type = T_PLUS; break;
            case '-': t.type = T_MINUS; break;   case '*': t.type = T_STAR; break;
            case '/': t.type = T_SLASH; break;   case '%': t.type = T_PERCENT; break;
            case '&': t.type = T_AMP; break;     case '|': t.type = T_PIPE; break;
            case '^': t.type = T_CARET; break;   case '~': t.type = T_TILDE; break;
            case '!': t.type = T_BANG; break;    case '<': t.type = T_LT; break;
            case '>': t.type = T_GT; break;
            default:
                snprintf(err, errcap, "line %d: unexpected character '%c'", line, *p);
                free(v.items);
                return NULL;
        }
        p++; push(&v, t);
    }

    push(&v, (Token){ .type = T_EOF, .line = line });
    *out_n = v.n;
    return v.items;
}
