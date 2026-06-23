/* Code generator (through M4): functions, parameters, return values, recursion.
 *
 * Registers: R0 accumulator/return, R1 op-temp, R2/R3 scratch, R6 frame pointer,
 * R7 software stack pointer. Expression temporaries use the hardware PUSH/POP
 * stack; CALL/RET use the hardware stack for return addresses.
 *
 * Per-call frame on the R7 stack (grows down):
 *     [R6 + 4 + 4i]   parameter i        (pushed by caller)
 *     [R6 + 0]        saved caller FP
 *     [R6 - 4(j+1)]   local j            (allocated by callee prologue)
 * No SP-arithmetic instruction is needed — R6/R7 are ordinary registers.
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
static int    g_label;
static char   cur_epi[80];

static struct { char name[64]; int off; } sym[256];   /* current function scope */
static int nsym;
static struct { char name[64]; int nparams; } funcs[128];
static int nfuncs;

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
static int newlabel(void) { return g_label++; }
static void emit_label(int id) { fprintf(O, "_L%d:\n", id); }
static void emitj(const char *mn, int id) { fprintf(O, "        %s _L%d\n", mn, id); }

static int sym_find(const char *name, int *off) {
    for (int i = 0; i < nsym; i++) if (!strcmp(sym[i].name, name)) { *off = sym[i].off; return 1; }
    return 0;
}
static void sym_add(const char *name, int off) {
    strncpy(sym[nsym].name, name, sizeof sym[0].name - 1);
    sym[nsym].off = off; nsym++;
}
static int func_find(const char *name, int *nparams) {
    for (int i = 0; i < nfuncs; i++) if (!strcmp(funcs[i].name, name)) { *nparams = funcs[i].nparams; return 1; }
    return 0;
}

/* assign frame offsets to locals (negative, below FP) */
static void collect(const Node *n, int *li) {
    if (!n) return;
    switch (n->type) {
        case N_BLOCK: for (int i = 0; i < n->nkids; i++) collect(n->kids[i], li); break;
        case N_VARDECL: { int o; if (!sym_find(n->name, &o)) { sym_add(n->name, -4 * (*li + 1)); (*li)++; } } break;
        case N_IF: collect(n->b, li); collect(n->c, li); break;
        case N_WHILE: collect(n->b, li); break;
        case N_FOR: collect(n->a, li); collect(n->d, li); break;
        default: break;
    }
}

static int gen_expr(const Node *n);

static int gen_cmp(const Node *n) {
    if (gen_expr(n->a)) return 1;
    emit("PUSH R0");
    if (gen_expr(n->b)) return 1;
    emit("POP R1");
    emit("SUB R1, R0");
    emit("LOADI R0, 0");
    int lset = newlabel(), ldone = newlabel();
    switch (n->op) {
        case T_EQ: emitj("JZ", lset); break;
        case T_NE: emitj("JNZ", lset); break;
        case T_LT: emitj("JL", lset); break;
        case T_GT: emitj("JG", lset); break;
        case T_LE: emitj("JL", lset); emitj("JZ", lset); break;
        case T_GE: emitj("JG", lset); emitj("JZ", lset); break;
        default: return fail(n->line, "bad comparison");
    }
    emitj("JMP", ldone);
    emit_label(lset);
    emit("LOADI R0, 1");
    emit_label(ldone);
    return 0;
}

static int gen_and(const Node *n) {
    int lfalse = newlabel(), lend = newlabel();
    if (gen_expr(n->a)) return 1;
    emit("AND R0, R0"); emitj("JZ", lfalse);
    if (gen_expr(n->b)) return 1;
    emit("AND R0, R0"); emitj("JZ", lfalse);
    emit("LOADI R0, 1"); emitj("JMP", lend);
    emit_label(lfalse); emit("LOADI R0, 0");
    emit_label(lend);
    return 0;
}
static int gen_or(const Node *n) {
    int ltrue = newlabel(), lend = newlabel();
    if (gen_expr(n->a)) return 1;
    emit("AND R0, R0"); emitj("JNZ", ltrue);
    if (gen_expr(n->b)) return 1;
    emit("AND R0, R0"); emitj("JNZ", ltrue);
    emit("LOADI R0, 0"); emitj("JMP", lend);
    emit_label(ltrue); emit("LOADI R0, 1");
    emit_label(lend);
    return 0;
}

static int gen_binary(const Node *n) {
    if (n->op == T_ANDAND) return gen_and(n);
    if (n->op == T_OROR) return gen_or(n);
    if (n->op == T_EQ || n->op == T_NE || n->op == T_LT || n->op == T_LE ||
        n->op == T_GT || n->op == T_GE) return gen_cmp(n);

    if (gen_expr(n->a)) return 1;
    emit("PUSH R0");
    if (gen_expr(n->b)) return 1;
    emit("POP R1");                 /* R1 = left, R0 = right */
    switch (n->op) {
        case T_PLUS:  emit("ADD R1, R0"); emit("MOV R0, R1"); break;
        case T_MINUS: emit("SUB R1, R0"); emit("MOV R0, R1"); break;
        case T_STAR:  emit("MUL R1, R0"); emit("MOV R0, R1"); break;
        case T_SLASH: emit("DIV R1, R0"); emit("MOV R0, R1"); break;
        case T_PERCENT:
            emit("MOV R2, R1"); emit("MOV R3, R0");
            emit("DIV R2, R3"); emit("MUL R2, R3");
            emit("SUB R1, R2"); emit("MOV R0, R1"); break;
        case T_AMP:   emit("AND R1, R0"); emit("MOV R0, R1"); break;
        case T_PIPE:  emit("OR R1, R0");  emit("MOV R0, R1"); break;
        case T_CARET: emit("XOR R1, R0"); emit("MOV R0, R1"); break;
        case T_SHL:   emit("SHL R1, R0"); emit("MOV R0, R1"); break;
        case T_SHR:   emit("SHR R1, R0"); emit("MOV R0, R1"); break;
        default: return fail(n->line, "unsupported binary operator");
    }
    return 0;
}

static int gen_call(const Node *n) {
    if (!strcmp(n->name, "out")) {
        if (n->nkids != 1) return fail(n->line, "out() takes 1 argument");
        if (gen_expr(n->kids[0])) return 1;
        emit("OUT R0");
        return 0;
    }
    if (!strcmp(n->name, "in")) { emit("IN R0"); return 0; }

    int np;
    if (!func_find(n->name, &np)) return fail(n->line, "undefined function '%s'", n->name);
    if (n->nkids != np) return fail(n->line, "'%s' expects %d argument(s)", n->name, np);

    for (int i = n->nkids - 1; i >= 0; i--) {   /* push args, arg0 ends lowest */
        if (gen_expr(n->kids[i])) return 1;
        emit("LOADI R1, 4"); emit("SUB R7, R1"); emit("STORE [R7+0], R0");
    }
    emit("CALL %s", n->name);
    if (n->nkids) { emit("LOADI R1, %d", 4 * n->nkids); emit("ADD R7, R1"); }
    return 0;                                   /* result already in R0 */
}

static int gen_expr(const Node *n) {
    switch (n->type) {
        case N_INT:
            if (n->ival < IMM_MIN || n->ival > IMM_MAX)
                return fail(n->line, "integer literal %ld too large", n->ival);
            emit("LOADI R0, %ld", n->ival);
            return 0;
        case N_VAR: {
            int off; if (!sym_find(n->name, &off)) return fail(n->line, "undefined variable '%s'", n->name);
            emit("LOAD R0, [R6%+d]", off);
            return 0;
        }
        case N_ASSIGN: {
            if (n->a->type != N_VAR) return fail(n->line, "unsupported assignment target");
            int off; if (!sym_find(n->a->name, &off)) return fail(n->a->line, "undefined variable '%s'", n->a->name);
            if (gen_expr(n->b)) return 1;
            emit("STORE [R6%+d], R0", off);
            return 0;
        }
        case N_UNARY:
            if (gen_expr(n->a)) return 1;
            if (n->op == T_MINUS) { emit("LOADI R1, 0"); emit("SUB R1, R0"); emit("MOV R0, R1"); }
            else if (n->op == T_TILDE) emit("NOT R0");
            else if (n->op == T_BANG) {
                int lend = newlabel();
                emit("AND R0, R0"); emit("LOADI R0, 0"); emitj("JNZ", lend);
                emit("LOADI R0, 1"); emit_label(lend);
            }
            else return fail(n->line, "unary '%s' needs M5", tok_name(n->op));
            return 0;
        case N_BINARY: return gen_binary(n);
        case N_CALL: return gen_call(n);
        case N_INDEX: return fail(n->line, "arrays/pointers need M5");
        default: return fail(n->line, "unsupported expression");
    }
}

static int gen_stmt(const Node *n);

static int gen_cond_false(const Node *cond, int label) {
    if (gen_expr(cond)) return 1;
    emit("AND R0, R0");
    emitj("JZ", label);
    return 0;
}

static int gen_stmt(const Node *n) {
    switch (n->type) {
        case N_BLOCK: for (int i = 0; i < n->nkids; i++) if (gen_stmt(n->kids[i])) return 1; return 0;
        case N_VARDECL: {
            if (n->is_array || n->is_ptr) return fail(n->line, "arrays/pointers need M5");
            if (n->a) { int off; sym_find(n->name, &off); if (gen_expr(n->a)) return 1; emit("STORE [R6%+d], R0", off); }
            return 0;
        }
        case N_EXPRSTMT: return gen_expr(n->a);
        case N_RETURN:
            if (n->a && gen_expr(n->a)) return 1;
            emit("JMP %s", cur_epi);
            return 0;
        case N_IF: {
            if (n->c) {
                int lelse = newlabel(), lend = newlabel();
                if (gen_cond_false(n->a, lelse)) return 1;
                if (gen_stmt(n->b)) return 1;
                emitj("JMP", lend);
                emit_label(lelse);
                if (gen_stmt(n->c)) return 1;
                emit_label(lend);
            } else {
                int lend = newlabel();
                if (gen_cond_false(n->a, lend)) return 1;
                if (gen_stmt(n->b)) return 1;
                emit_label(lend);
            }
            return 0;
        }
        case N_WHILE: {
            int ltop = newlabel(), lend = newlabel();
            emit_label(ltop);
            if (gen_cond_false(n->a, lend)) return 1;
            if (gen_stmt(n->b)) return 1;
            emitj("JMP", ltop);
            emit_label(lend);
            return 0;
        }
        case N_FOR: {
            if (n->a && gen_stmt(n->a)) return 1;
            int ltop = newlabel(), lend = newlabel();
            emit_label(ltop);
            if (n->b && gen_cond_false(n->b, lend)) return 1;
            if (gen_stmt(n->d)) return 1;
            if (n->c && gen_expr(n->c)) return 1;
            emitj("JMP", ltop);
            emit_label(lend);
            return 0;
        }
        default: return fail(n->line, "unsupported statement");
    }
}

static int gen_function(const Node *fn) {
    nsym = 0;
    int np = 0;
    for (int i = 0; i < fn->nkids; i++)
        if (fn->kids[i]->type == N_PARAM) { sym_add(fn->kids[i]->name, 4 + 4 * np); np++; }
    int nlocal = 0;
    collect(fn->a, &nlocal);

    fprintf(O, "%s:\n", fn->name);
    emit("LOADI R1, 4"); emit("SUB R7, R1");      /* make room for saved FP */
    emit("STORE [R7+0], R6"); emit("MOV R6, R7"); /* save & set frame pointer */
    if (nlocal) { emit("LOADI R1, %d", 4 * nlocal); emit("SUB R7, R1"); }

    snprintf(cur_epi, sizeof cur_epi, "%s_epi", fn->name);
    if (gen_stmt(fn->a)) return 1;

    fprintf(O, "%s:\n", cur_epi);
    emit("MOV R7, R6");                            /* discard locals */
    emit("LOAD R6, [R7+0]");                       /* restore caller FP */
    emit("LOADI R1, 4"); emit("ADD R7, R1");       /* pop saved-FP slot */
    emit("RET");
    return 0;
}

int codegen(const Node *root, FILE *out, char *err, size_t errcap) {
    O = out; ERR = err; ERRCAP = errcap; g_label = 0; nfuncs = 0;

    const Node *main_fn = NULL;
    for (int i = 0; i < root->nkids; i++) {
        const Node *d = root->kids[i];
        if (d->type == N_VARDECL) return fail(d->line, "globals need M5");
        if (d->type == N_FUNC) {
            int p = 0;
            for (int k = 0; k < d->nkids; k++) if (d->kids[k]->type == N_PARAM) p++;
            strncpy(funcs[nfuncs].name, d->name, sizeof funcs[0].name - 1);
            funcs[nfuncs].nparams = p; nfuncs++;
            if (!strcmp(d->name, "main")) main_fn = d;
        }
    }
    if (!main_fn) { snprintf(err, errcap, "no main() function"); return 1; }

    fprintf(out, "; generated by mcc\n");
    emit("LOADI R7, %d", STACK_BASE);
    emit("CALL main");
    emit("HALT");
    for (int i = 0; i < root->nkids; i++)
        if (root->kids[i]->type == N_FUNC && gen_function(root->kids[i])) return 1;
    return 0;
}
