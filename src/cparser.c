#include "compiler.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---- arena (all AST allocations; freed together) ---- */
static void **arena = NULL;
static size_t arena_n = 0, arena_cap = 0;

static void *anode(size_t sz) {
    void *p = calloc(1, sz);
    if (arena_n == arena_cap) {
        arena_cap = arena_cap ? arena_cap * 2 : 256;
        arena = realloc(arena, arena_cap * sizeof(void *));
    }
    arena[arena_n++] = p;
    return p;
}
void arena_free(void) {
    for (size_t i = 0; i < arena_n; i++) free(arena[i]);
    free(arena); arena = NULL; arena_n = arena_cap = 0;
}

static Node *node(NodeType t, int line) { Node *n = anode(sizeof(Node)); n->type = t; n->line = line; return n; }
static void add_kid(Node *p, Node *k) {
    Node **arr = anode((size_t)(p->nkids + 1) * sizeof(Node *));
    for (int i = 0; i < p->nkids; i++) arr[i] = p->kids[i];
    arr[p->nkids++] = k; p->kids = arr;
}

/* ---- parser state ---- */
typedef struct {
    const Token *t; size_t n, i;
    char *err; size_t errcap;
    jmp_buf jb;
} P;

static const Token *peek(P *p) { return &p->t[p->i]; }
static const Token *advance(P *p) { return &p->t[p->i++]; }
static int at(P *p, TokType tt) { return peek(p)->type == tt; }
static int accept(P *p, TokType tt) { if (at(p, tt)) { p->i++; return 1; } return 0; }

static void perr(P *p, const char *fmt, ...) {
    char m[120];
    va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    snprintf(p->err, p->errcap, "line %d: %s", peek(p)->line, m);
    longjmp(p->jb, 1);
}
static void expect(P *p, TokType tt, const char *what) {
    if (!accept(p, tt)) perr(p, "expected %s", what);
}

static Node *parse_expr(P *p);
static Node *parse_stmt(P *p);

/* precedence for binary operators (higher binds tighter); 0 = not binary */
static int binprec(TokType t) {
    switch (t) {
        case T_OROR: return 1; case T_ANDAND: return 2; case T_PIPE: return 3;
        case T_CARET: return 4; case T_AMP: return 5;
        case T_EQ: case T_NE: return 6;
        case T_LT: case T_LE: case T_GT: case T_GE: return 7;
        case T_SHL: case T_SHR: return 8;
        case T_PLUS: case T_MINUS: return 9;
        case T_STAR: case T_SLASH: case T_PERCENT: return 10;
        default: return 0;
    }
}

static Node *parse_primary(P *p) {
    const Token *t = peek(p);
    if (accept(p, T_INT_LIT)) { Node *n = node(N_INT, t->line); n->ival = t->ival; return n; }
    if (accept(p, T_IDENT))   { Node *n = node(N_VAR, t->line); strcpy(n->name, t->text); return n; }
    if (accept(p, T_LPAREN))  { Node *n = parse_expr(p); expect(p, T_RPAREN, "')'"); return n; }
    perr(p, "expected expression");
    return NULL;
}

static Node *parse_postfix(P *p) {
    Node *n = parse_primary(p);
    for (;;) {
        if (at(p, T_LPAREN)) {
            if (n->type != N_VAR) perr(p, "call of non-function");
            Node *c = node(N_CALL, n->line); strcpy(c->name, n->name);
            advance(p);
            if (!at(p, T_RPAREN)) {
                do { add_kid(c, parse_expr(p)); } while (accept(p, T_COMMA));
            }
            expect(p, T_RPAREN, "')'");
            n = c;
        } else if (accept(p, T_LBRACK)) {
            Node *ix = node(N_INDEX, n->line); ix->a = n; ix->b = parse_expr(p);
            expect(p, T_RBRACK, "']'");
            n = ix;
        } else break;
    }
    return n;
}

static Node *parse_unary(P *p) {
    const Token *t = peek(p);
    if (at(p, T_MINUS) || at(p, T_BANG) || at(p, T_TILDE) || at(p, T_STAR) || at(p, T_AMP)) {
        advance(p);
        Node *n = node(N_UNARY, t->line); n->op = t->type; n->a = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

static Node *parse_bin(P *p, int minprec) {
    Node *left = parse_unary(p);
    for (;;) {
        int prec = binprec(peek(p)->type);
        if (prec == 0 || prec < minprec) break;
        const Token *op = advance(p);
        Node *right = parse_bin(p, prec + 1);
        Node *b = node(N_BINARY, op->line); b->op = op->type; b->a = left; b->b = right;
        left = b;
    }
    return left;
}

static Node *parse_expr(P *p) {
    Node *left = parse_bin(p, 1);
    if (at(p, T_ASSIGN)) {
        const Token *t = advance(p);
        Node *n = node(N_ASSIGN, t->line); n->a = left; n->b = parse_expr(p);
        return n;
    }
    return left;
}

/* type := ('int'|'void') '*'?   returns is_void; sets *is_ptr */
static int parse_type(P *p, int *is_ptr) {
    int is_void = 0;
    if (accept(p, T_KW_INT)) is_void = 0;
    else if (accept(p, T_KW_VOID)) is_void = 1;
    else perr(p, "expected type");
    *is_ptr = accept(p, T_STAR);
    return is_void;
}

/* local 'int' declaration statement */
static Node *parse_local_decl(P *p) {
    int is_ptr; int is_void = parse_type(p, &is_ptr);
    if (is_void) perr(p, "'void' variable");
    const Token *name = peek(p); expect(p, T_IDENT, "name");
    Node *d = node(N_VARDECL, name->line); strcpy(d->name, name->text); d->is_ptr = is_ptr;
    if (accept(p, T_LBRACK)) {
        const Token *sz = peek(p); expect(p, T_INT_LIT, "array size");
        d->is_array = 1; d->arr_size = sz->ival;
        expect(p, T_RBRACK, "']'");
    } else if (accept(p, T_ASSIGN)) {
        d->a = parse_expr(p);
    }
    expect(p, T_SEMI, "';'");
    return d;
}

static Node *parse_block(P *p) {
    const Token *t = peek(p); expect(p, T_LBRACE, "'{'");
    Node *b = node(N_BLOCK, t->line);
    while (!at(p, T_RBRACE) && !at(p, T_EOF)) add_kid(b, parse_stmt(p));
    expect(p, T_RBRACE, "'}'");
    return b;
}

static Node *parse_stmt(P *p) {
    const Token *t = peek(p);
    if (at(p, T_LBRACE)) return parse_block(p);
    if (at(p, T_KW_INT)) return parse_local_decl(p);
    if (accept(p, T_KW_IF)) {
        Node *n = node(N_IF, t->line);
        expect(p, T_LPAREN, "'('"); n->a = parse_expr(p); expect(p, T_RPAREN, "')'");
        n->b = parse_stmt(p);
        if (accept(p, T_KW_ELSE)) n->c = parse_stmt(p);
        return n;
    }
    if (accept(p, T_KW_WHILE)) {
        Node *n = node(N_WHILE, t->line);
        expect(p, T_LPAREN, "'('"); n->a = parse_expr(p); expect(p, T_RPAREN, "')'");
        n->b = parse_stmt(p);
        return n;
    }
    if (accept(p, T_KW_FOR)) {
        Node *n = node(N_FOR, t->line);
        expect(p, T_LPAREN, "'('");
        if (at(p, T_KW_INT)) n->a = parse_local_decl(p);
        else if (accept(p, T_SEMI)) n->a = NULL;
        else { Node *e = node(N_EXPRSTMT, t->line); e->a = parse_expr(p); expect(p, T_SEMI, "';'"); n->a = e; }
        if (!at(p, T_SEMI)) n->b = parse_expr(p);
        expect(p, T_SEMI, "';'");
        if (!at(p, T_RPAREN)) n->c = parse_expr(p);
        expect(p, T_RPAREN, "')'");
        n->d = parse_stmt(p);
        return n;
    }
    if (accept(p, T_KW_RETURN)) {
        Node *n = node(N_RETURN, t->line);
        if (!at(p, T_SEMI)) n->a = parse_expr(p);
        expect(p, T_SEMI, "';'");
        return n;
    }
    Node *e = node(N_EXPRSTMT, t->line); e->a = parse_expr(p); expect(p, T_SEMI, "';'");
    return e;
}

static Node *parse_toplevel(P *p) {
    const Token *startTok = peek(p);
    int is_ptr; int is_void = parse_type(p, &is_ptr);
    const Token *name = peek(p); expect(p, T_IDENT, "name");

    if (accept(p, T_LPAREN)) {           /* function */
        Node *f = node(N_FUNC, startTok->line); strcpy(f->name, name->text); f->ret_void = is_void;
        if (!at(p, T_RPAREN) && !(at(p, T_KW_VOID) && p->t[p->i + 1].type == T_RPAREN)) {
            do {
                int pp; parse_type(p, &pp);
                const Token *pn = peek(p); expect(p, T_IDENT, "param name");
                Node *par = node(N_PARAM, pn->line); strcpy(par->name, pn->text); par->is_ptr = pp;
                add_kid(f, par);
            } while (accept(p, T_COMMA));
        } else if (at(p, T_KW_VOID)) advance(p);   /* (void) */
        expect(p, T_RPAREN, "')'");
        f->a = parse_block(p);
        return f;
    }

    /* global variable */
    if (is_void) perr(p, "'void' variable");
    Node *d = node(N_VARDECL, name->line); strcpy(d->name, name->text); d->is_ptr = is_ptr;
    if (accept(p, T_LBRACK)) {
        const Token *sz = peek(p); expect(p, T_INT_LIT, "array size");
        d->is_array = 1; d->arr_size = sz->ival;
        expect(p, T_RBRACK, "']'");
    } else if (accept(p, T_ASSIGN)) {
        d->a = parse_expr(p);
    }
    expect(p, T_SEMI, "';'");
    return d;
}

Node *parse(const Token *toks, size_t n, char *err, size_t errcap) {
    P p = { .t = toks, .n = n, .i = 0, .err = err, .errcap = errcap };
    if (setjmp(p.jb)) return NULL;
    Node *prog = node(N_PROGRAM, 1);
    while (!at(&p, T_EOF)) add_kid(prog, parse_toplevel(&p));
    return prog;
}

Node *parse_source(const char *src, char *err, size_t errcap) {
    size_t n = 0;
    Token *toks = lex(src, &n, err, errcap);
    if (!toks) return NULL;
    Node *root = parse(toks, n, err, errcap);
    free(toks);
    return root;
}

/* ---- pretty-printer ---- */
const char *tok_name(int t) {
    switch (t) {
        case T_PLUS: return "+"; case T_MINUS: return "-"; case T_STAR: return "*";
        case T_SLASH: return "/"; case T_PERCENT: return "%"; case T_AMP: return "&";
        case T_PIPE: return "|"; case T_CARET: return "^"; case T_TILDE: return "~";
        case T_BANG: return "!"; case T_SHL: return "<<"; case T_SHR: return ">>";
        case T_EQ: return "=="; case T_NE: return "!="; case T_LT: return "<";
        case T_LE: return "<="; case T_GT: return ">"; case T_GE: return ">=";
        case T_ANDAND: return "&&"; case T_OROR: return "||";
        default: return "?";
    }
}

static void pr(const Node *n, FILE *o, int d) {
    if (!n) return;
    for (int i = 0; i < d; i++) fputs("  ", o);
    switch (n->type) {
        case N_PROGRAM: fputs("program\n", o); for (int i = 0; i < n->nkids; i++) pr(n->kids[i], o, d + 1); break;
        case N_FUNC:
            fprintf(o, "func %s -> %s\n", n->name, n->ret_void ? "void" : "int");
            for (int i = 0; i < n->nkids; i++) pr(n->kids[i], o, d + 1);
            pr(n->a, o, d + 1); break;
        case N_PARAM: fprintf(o, "param %s%s\n", n->is_ptr ? "*" : "", n->name); break;
        case N_VARDECL:
            fprintf(o, "vardecl %s%s", n->is_ptr ? "*" : "", n->name);
            if (n->is_array) fprintf(o, "[%ld]", n->arr_size);
            fputs("\n", o);
            pr(n->a, o, d + 1); break;
        case N_BLOCK: fputs("block\n", o); for (int i = 0; i < n->nkids; i++) pr(n->kids[i], o, d + 1); break;
        case N_IF: fputs("if\n", o); pr(n->a, o, d + 1); pr(n->b, o, d + 1); if (n->c) { for (int i = 0; i < d; i++) fputs("  ", o); fputs("else\n", o); pr(n->c, o, d + 1); } break;
        case N_WHILE: fputs("while\n", o); pr(n->a, o, d + 1); pr(n->b, o, d + 1); break;
        case N_FOR: fputs("for\n", o); pr(n->a, o, d + 1); pr(n->b, o, d + 1); pr(n->c, o, d + 1); pr(n->d, o, d + 1); break;
        case N_RETURN: fputs("return\n", o); pr(n->a, o, d + 1); break;
        case N_EXPRSTMT: fputs("expr\n", o); pr(n->a, o, d + 1); break;
        case N_INT: fprintf(o, "int %ld\n", n->ival); break;
        case N_VAR: fprintf(o, "var %s\n", n->name); break;
        case N_UNARY: fprintf(o, "unary %s\n", tok_name(n->op)); pr(n->a, o, d + 1); break;
        case N_BINARY: fprintf(o, "binary %s\n", tok_name(n->op)); pr(n->a, o, d + 1); pr(n->b, o, d + 1); break;
        case N_ASSIGN: fputs("assign =\n", o); pr(n->a, o, d + 1); pr(n->b, o, d + 1); break;
        case N_CALL: fprintf(o, "call %s\n", n->name); for (int i = 0; i < n->nkids; i++) pr(n->kids[i], o, d + 1); break;
        case N_INDEX: fputs("index\n", o); pr(n->a, o, d + 1); pr(n->b, o, d + 1); break;
    }
}
void ast_print(const Node *root, FILE *out) { pr(root, out, 0); }
