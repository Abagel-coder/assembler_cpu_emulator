/* Code generator (M2): a single straight-line main() -> assembly.
 *
 * Conventions:
 *   R0  expression accumulator / result
 *   R1  binary-op temporary   R2/R3  scratch (modulo)
 *   R7  software-stack base; locals live at [R7 + offset]
 *   hardware PUSH/POP  expression temporaries
 * Locals are statically framed at program start (no calls yet); the same R7
 * scheme extends to real call frames in M4 without changing the ISA.
 */
#include "compiler.h"

#include <stdarg.h>
#include <string.h>

#define STACK_BASE 0x8000
#define IMM_MIN (-(1L << 19))
#define IMM_MAX ((1L << 19) - 1)

static FILE  *O;
static char  *ERR;
static size_t ERRCAP;

static struct { char name[64]; int off; } locals[256];
static int nlocals;
static int framesize;

static int fail(int line, const char *fmt, ...) {
    char m[120];
    va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    snprintf(ERR, ERRCAP, "line %d: %s", line, m);
    return 1;
}
static void emit(const char *fmt, ...) {
    fputs("        ", O);
    va_list ap; va_start(ap, fmt); vfprintf(O, fmt, ap); va_end(ap);
    fputc('\n', O);
}

static int local_off(const char *name) {
    for (int i = 0; i < nlocals; i++) if (!strcmp(locals[i].name, name)) return locals[i].off;
    return -1;
}
static void add_local(const char *name) {
    if (local_off(name) >= 0) return;
    strncpy(locals[nlocals].name, name, sizeof locals[0].name - 1);
    locals[nlocals].off = nlocals * 4;
    nlocals++;
}
static void collect(const Node *n) {
    if (!n) return;
    switch (n->type) {
        case N_BLOCK: for (int i = 0; i < n->nkids; i++) collect(n->kids[i]); break;
        case N_VARDECL: add_local(n->name); break;
        case N_IF: collect(n->b); collect(n->c); break;
        case N_WHILE: collect(n->b); break;
        case N_FOR: collect(n->a); collect(n->d); break;
        default: break;
    }
}

static int is_cmp_or_logic(int op) {
    return op == T_EQ || op == T_NE || op == T_LT || op == T_LE || op == T_GT ||
           op == T_GE || op == T_ANDAND || op == T_OROR;
}

static int gen_expr(const Node *n);

static int gen_binary(const Node *n) {
    if (is_cmp_or_logic(n->op))
        return fail(n->line, "comparison/logical operators need M3");
    if (gen_expr(n->a)) return 1;
    emit("PUSH R0");
    if (gen_expr(n->b)) return 1;
    emit("POP R1");                 /* R1 = left, R0 = right */
    switch (n->op) {
        case T_PLUS:    emit("ADD R1, R0"); emit("MOV R0, R1"); break;
        case T_MINUS:   emit("SUB R1, R0"); emit("MOV R0, R1"); break;
        case T_STAR:    emit("MUL R1, R0"); emit("MOV R0, R1"); break;
        case T_SLASH:   emit("DIV R1, R0"); emit("MOV R0, R1"); break;
        case T_PERCENT:                     /* a - (a/b)*b */
            emit("MOV R2, R1"); emit("MOV R3, R0");
            emit("DIV R2, R3"); emit("MUL R2, R3");
            emit("SUB R1, R2"); emit("MOV R0, R1"); break;
        case T_AMP:     emit("AND R1, R0"); emit("MOV R0, R1"); break;
        case T_PIPE:    emit("OR R1, R0");  emit("MOV R0, R1"); break;
        case T_CARET:   emit("XOR R1, R0"); emit("MOV R0, R1"); break;
        case T_SHL:     emit("SHL R1, R0"); emit("MOV R0, R1"); break;
        case T_SHR:     emit("SHR R1, R0"); emit("MOV R0, R1"); break;
        default: return fail(n->line, "unsupported binary operator");
    }
    return 0;
}

static int gen_expr(const Node *n) {
    switch (n->type) {
        case N_INT:
            if (n->ival < IMM_MIN || n->ival > IMM_MAX)
                return fail(n->line, "integer literal %ld too large (M2)", n->ival);
            emit("LOADI R0, %ld", n->ival);
            return 0;
        case N_VAR: {
            int off = local_off(n->name);
            if (off < 0) return fail(n->line, "undefined variable '%s'", n->name);
            emit("LOAD R0, [R7+%d]", off);
            return 0;
        }
        case N_ASSIGN: {
            if (n->a->type != N_VAR) return fail(n->line, "unsupported assignment target (M2)");
            int off = local_off(n->a->name);
            if (off < 0) return fail(n->a->line, "undefined variable '%s'", n->a->name);
            if (gen_expr(n->b)) return 1;
            emit("STORE [R7+%d], R0", off);
            return 0;
        }
        case N_UNARY:
            if (gen_expr(n->a)) return 1;
            if (n->op == T_MINUS) { emit("LOADI R1, 0"); emit("SUB R1, R0"); emit("MOV R0, R1"); }
            else if (n->op == T_TILDE) emit("NOT R0");
            else return fail(n->line, "unary '%s' needs a later milestone", tok_name(n->op));
            return 0;
        case N_BINARY: return gen_binary(n);
        case N_CALL:
            if (!strcmp(n->name, "out")) {
                if (n->nkids != 1) return fail(n->line, "out() takes 1 argument");
                if (gen_expr(n->kids[0])) return 1;
                emit("OUT R0");
            } else if (!strcmp(n->name, "in")) {
                emit("IN R0");
            } else {
                return fail(n->line, "calls to user functions need M4");
            }
            return 0;
        case N_INDEX: return fail(n->line, "arrays/pointers need M5");
        default: return fail(n->line, "unsupported expression");
    }
}

static int gen_stmt(const Node *n) {
    switch (n->type) {
        case N_BLOCK: for (int i = 0; i < n->nkids; i++) if (gen_stmt(n->kids[i])) return 1; return 0;
        case N_VARDECL:
            if (n->is_array || n->is_ptr) return fail(n->line, "arrays/pointers need M5");
            if (n->a) { if (gen_expr(n->a)) return 1; emit("STORE [R7+%d], R0", local_off(n->name)); }
            return 0;
        case N_EXPRSTMT: return gen_expr(n->a);
        case N_RETURN:
            if (n->a && gen_expr(n->a)) return 1;
            emit("HALT");
            return 0;
        case N_IF: case N_WHILE: case N_FOR:
            return fail(n->line, "control flow needs M3");
        default: return fail(n->line, "unsupported statement");
    }
}

int codegen(const Node *root, FILE *out, char *err, size_t errcap) {
    O = out; ERR = err; ERRCAP = errcap;
    nlocals = 0;

    const Node *main_fn = NULL;
    int funcs = 0;
    for (int i = 0; i < root->nkids; i++) {
        const Node *d = root->kids[i];
        if (d->type == N_VARDECL) return fail(d->line, "globals need M5");
        if (d->type == N_FUNC) {
            funcs++;
            if (!strcmp(d->name, "main")) main_fn = d;
        }
    }
    if (!main_fn) { snprintf(err, errcap, "no main() function"); return 1; }
    if (funcs > 1) return fail(main_fn->line, "only main() is supported until M4");

    collect(main_fn->a);
    framesize = nlocals * 4;

    fprintf(out, "; generated by mcc (M2)\n");
    emit("LOADI R7, %d", STACK_BASE);
    if (framesize) { emit("LOADI R1, %d", framesize); emit("SUB R7, R1"); }
    if (gen_stmt(main_fn->a)) return 1;
    emit("HALT");
    return 0;
}
