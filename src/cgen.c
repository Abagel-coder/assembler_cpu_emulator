/* Code generator (through M6): functions/recursion, pointers/arrays/globals,
 * plus direct scalar access and a peephole pass over the emitted assembly.
 *
 * Registers: R0 accumulator/return, R1 op-temp, R2/R3 scratch, R6 frame pointer,
 * R7 software stack pointer. Expression temporaries use the hardware PUSH/POP
 * stack; CALL/RET use the hardware stack for return addresses.
 *
 * Per-call frame on the R7 stack (grows down):
 *     [R6 + 4 + 4i]   parameter i        (pushed by caller)
 *     [R6 + 0]        saved caller FP
 *     [R6 + off]      local (off < 0); arrays occupy contiguous words
 * Globals are emitted as .word data after the code and addressed by label.
 */
#include "compiler.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define STACK_BASE 0x8000
#define IMM_MIN (-(1L << 19))
#define IMM_MAX ((1L << 19) - 1)

static char  *ERR;
static size_t ERRCAP;
static int    g_label;
static char   cur_epi[80];

static char **lines;
static int    nlines, linecap;

static struct { char name[64]; int off; int is_array; } sym[256];
static int nsym;
static struct { char name[64]; int nparams; } funcs[128];
static int nfuncs;
static struct { char name[64]; int is_array; } globs[128];
static int nglobs;

static char *sdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); memcpy(p, s, n); return p; }

static int fail(int line, const char *fmt, ...) {
    char m[120];
    va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    snprintf(ERR, ERRCAP, "line %d: %s", line, m);
    return 1;
}

static void add_line(const char *s) {
    if (nlines == linecap) { linecap = linecap ? linecap * 2 : 256; lines = realloc(lines, (size_t)linecap * sizeof(char *)); }
    lines[nlines++] = sdup(s);
}
static void put_raw(const char *fmt, ...) {
    char b[176]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    add_line(b);
}
static void emit(const char *fmt, ...) {
    char b[160], full[176];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    snprintf(full, sizeof full, "        %s", b);
    add_line(full);
}
static int newlabel(void) { return g_label++; }
static void emit_label(int id) { put_raw("_L%d:", id); }
static void emitj(const char *mn, int id) { emit("%s _L%d", mn, id); }

static int sym_find(const char *name, int *off, int *is_array) {
    for (int i = 0; i < nsym; i++)
        if (!strcmp(sym[i].name, name)) { if (off) *off = sym[i].off; if (is_array) *is_array = sym[i].is_array; return 1; }
    return 0;
}
static void sym_add(const char *name, int off, int is_array) {
    strncpy(sym[nsym].name, name, sizeof sym[0].name - 1);
    sym[nsym].off = off; sym[nsym].is_array = is_array; nsym++;
}
static int func_find(const char *name, int *nparams) {
    for (int i = 0; i < nfuncs; i++) if (!strcmp(funcs[i].name, name)) { *nparams = funcs[i].nparams; return 1; }
    return 0;
}
static int resolve(const char *name, int *off, int *is_array) {
    if (sym_find(name, off, is_array)) return 1;
    for (int i = 0; i < nglobs; i++)
        if (!strcmp(globs[i].name, name)) { if (is_array) *is_array = globs[i].is_array; return 2; }
    return 0;
}

static void collect(const Node *n, int *bytes) {
    if (!n) return;
    switch (n->type) {
        case N_BLOCK: for (int i = 0; i < n->nkids; i++) collect(n->kids[i], bytes); break;
        case N_VARDECL:
            if (!sym_find(n->name, NULL, NULL)) {
                *bytes += n->is_array ? (int)n->arr_size * 4 : 4;
                sym_add(n->name, -(*bytes), n->is_array);
            }
            break;
        case N_IF: collect(n->b, bytes); collect(n->c, bytes); break;
        case N_WHILE: collect(n->b, bytes); break;
        case N_FOR: collect(n->a, bytes); collect(n->d, bytes); break;
        default: break;
    }
}

static int gen_expr(const Node *n);
static int gen_addr(const Node *n);

static int gen_addr(const Node *n) {
    if (n->type == N_VAR) {
        int off, isa, k = resolve(n->name, &off, &isa);
        if (k == 0) return fail(n->line, "undefined variable '%s'", n->name);
        if (k == 1) { emit("MOV R0, R6"); if (off) { emit("LOADI R1, %d", off); emit("ADD R0, R1"); } }
        else emit("LOADI R0, %s", n->name);
        return 0;
    }
    if (n->type == N_INDEX) {
        const Node *base = n->a;
        if (base->type == N_VAR) {
            int off, isa;
            if (resolve(base->name, &off, &isa) == 0) return fail(base->line, "undefined variable '%s'", base->name);
            if (isa) { if (gen_addr(base)) return 1; } else { if (gen_expr(base)) return 1; }
        } else if (gen_expr(base)) return 1;
        emit("PUSH R0");
        if (gen_expr(n->b)) return 1;
        emit("LOADI R1, 2"); emit("SHL R0, R1");
        emit("POP R1"); emit("ADD R0, R1");
        return 0;
    }
    if (n->type == N_UNARY && n->op == T_STAR) return gen_expr(n->a);
    return fail(n->line, "not an lvalue");
}

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
    emit_label(lset); emit("LOADI R0, 1");
    emit_label(ldone);
    return 0;
}
static int gen_and(const Node *n) {
    int lf = newlabel(), le = newlabel();
    if (gen_expr(n->a)) return 1;
    emit("AND R0, R0"); emitj("JZ", lf);
    if (gen_expr(n->b)) return 1;
    emit("AND R0, R0"); emitj("JZ", lf);
    emit("LOADI R0, 1"); emitj("JMP", le);
    emit_label(lf); emit("LOADI R0, 0");
    emit_label(le);
    return 0;
}
static int gen_or(const Node *n) {
    int lt = newlabel(), le = newlabel();
    if (gen_expr(n->a)) return 1;
    emit("AND R0, R0"); emitj("JNZ", lt);
    if (gen_expr(n->b)) return 1;
    emit("AND R0, R0"); emitj("JNZ", lt);
    emit("LOADI R0, 0"); emitj("JMP", le);
    emit_label(lt); emit("LOADI R0, 1");
    emit_label(le);
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
    emit("POP R1");
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
        emit("OUT R0"); return 0;
    }
    if (!strcmp(n->name, "in")) { emit("IN R0"); return 0; }
    int np;
    if (!func_find(n->name, &np)) return fail(n->line, "undefined function '%s'", n->name);
    if (n->nkids != np) return fail(n->line, "'%s' expects %d argument(s)", n->name, np);
    for (int i = n->nkids - 1; i >= 0; i--) {
        if (gen_expr(n->kids[i])) return 1;
        emit("LOADI R1, 4"); emit("SUB R7, R1"); emit("STORE [R7+0], R0");
    }
    emit("CALL %s", n->name);
    if (n->nkids) { emit("LOADI R1, %d", 4 * n->nkids); emit("ADD R7, R1"); }
    return 0;
}

static int gen_expr(const Node *n) {
    switch (n->type) {
        case N_INT:
            if (n->ival < IMM_MIN || n->ival > IMM_MAX) return fail(n->line, "integer literal %ld too large", n->ival);
            emit("LOADI R0, %ld", n->ival);
            return 0;
        case N_VAR: {
            int off, isa, k = resolve(n->name, &off, &isa);
            if (k == 0) return fail(n->line, "undefined variable '%s'", n->name);
            if (isa) return gen_addr(n);                    /* array decays to address */
            if (k == 1) emit("LOAD R0, [R6%+d]", off);      /* scalar local/param */
            else { emit("LOADI R0, %s", n->name); emit("LOAD R0, [R0+0]"); }  /* scalar global */
            return 0;
        }
        case N_INDEX:
            if (gen_addr(n)) return 1;
            emit("LOAD R0, [R0+0]");
            return 0;
        case N_ASSIGN: {
            const Node *lhs = n->a;
            if (lhs->type == N_VAR) {
                int off, isa, k = resolve(lhs->name, &off, &isa);
                if (k == 0) return fail(lhs->line, "undefined variable '%s'", lhs->name);
                if (isa) return fail(lhs->line, "cannot assign to an array");
                if (gen_expr(n->b)) return 1;
                if (k == 1) emit("STORE [R6%+d], R0", off);
                else { emit("LOADI R1, %s", lhs->name); emit("STORE [R1+0], R0"); }
                return 0;
            }
            if (lhs->type == N_INDEX || (lhs->type == N_UNARY && lhs->op == T_STAR)) {
                if (gen_addr(lhs)) return 1;
                emit("PUSH R0");
                if (gen_expr(n->b)) return 1;
                emit("POP R1");
                emit("STORE [R1+0], R0");
                return 0;
            }
            return fail(n->line, "invalid assignment target");
        }
        case N_UNARY:
            if (n->op == T_AMP) return gen_addr(n->a);
            if (n->op == T_STAR) { if (gen_addr(n)) return 1; emit("LOAD R0, [R0+0]"); return 0; }
            if (gen_expr(n->a)) return 1;
            if (n->op == T_MINUS) { emit("LOADI R1, 0"); emit("SUB R1, R0"); emit("MOV R0, R1"); }
            else if (n->op == T_TILDE) emit("NOT R0");
            else if (n->op == T_BANG) {
                int le = newlabel();
                emit("AND R0, R0"); emit("LOADI R0, 0"); emitj("JNZ", le);
                emit("LOADI R0, 1"); emit_label(le);
            } else return fail(n->line, "unsupported unary operator");
            return 0;
        case N_BINARY: return gen_binary(n);
        case N_CALL: return gen_call(n);
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
        case N_VARDECL:
            if (n->a) { int off; sym_find(n->name, &off, NULL); if (gen_expr(n->a)) return 1; emit("STORE [R6%+d], R0", off); }
            return 0;
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
                emitj("JMP", lend); emit_label(lelse);
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
            emitj("JMP", ltop); emit_label(lend);
            return 0;
        }
        case N_FOR: {
            if (n->a && gen_stmt(n->a)) return 1;
            int ltop = newlabel(), lend = newlabel();
            emit_label(ltop);
            if (n->b && gen_cond_false(n->b, lend)) return 1;
            if (gen_stmt(n->d)) return 1;
            if (n->c && gen_expr(n->c)) return 1;
            emitj("JMP", ltop); emit_label(lend);
            return 0;
        }
        default: return fail(n->line, "unsupported statement");
    }
}

static int gen_function(const Node *fn) {
    nsym = 0;
    int np = 0;
    for (int i = 0; i < fn->nkids; i++)
        if (fn->kids[i]->type == N_PARAM) { sym_add(fn->kids[i]->name, 4 + 4 * np, 0); np++; }
    int bytes = 0;
    collect(fn->a, &bytes);

    put_raw("%s:", fn->name);
    emit("LOADI R1, 4"); emit("SUB R7, R1");
    emit("STORE [R7+0], R6"); emit("MOV R6, R7");
    if (bytes) { emit("LOADI R1, %d", bytes); emit("SUB R7, R1"); }

    snprintf(cur_epi, sizeof cur_epi, "%s_epi", fn->name);
    if (gen_stmt(fn->a)) return 1;

    put_raw("%s:", cur_epi);
    emit("MOV R7, R6");
    emit("LOAD R6, [R7+0]");
    emit("LOADI R1, 4"); emit("ADD R7, R1");
    emit("RET");
    return 0;
}

/* ---- peephole over the emitted line buffer ---- */
static const char *body(const char *s) { while (*s == ' ') s++; return s; }
static int is_target(const char *s) { size_t n = strlen(s); return n && s[n - 1] == ':'; }
static int p_store(const char *s, char *mem) { return sscanf(body(s), "STORE [%63[^]]], R0", mem) == 1; }
static int p_load0(const char *s, char *mem) { return sscanf(body(s), "LOAD R0, [%63[^]]]", mem) == 1; }

static void peephole(void) {
    int changed = 1;
    while (changed) {
        changed = 0;
        char **out = malloc((size_t)(nlines ? nlines : 1) * sizeof(char *));
        int w = 0;
        for (int i = 0; i < nlines; i++) {
            char *cur = lines[i];
            char a[16], b[16], m1[64], m2[64];
            if (sscanf(body(cur), "MOV %15[^,], %15s", a, b) == 2 && !strcmp(a, b)) { free(cur); changed = 1; continue; }
            if (w > 0 && sscanf(body(out[w - 1]), "PUSH %15s", a) == 1 && sscanf(body(cur), "POP %15s", b) == 1) {
                free(out[--w]); free(cur);
                if (strcmp(a, b)) { char t[40]; snprintf(t, sizeof t, "        MOV %s, %s", b, a); out[w++] = sdup(t); }
                changed = 1; continue;
            }
            if (w > 0 && !is_target(out[w - 1]) && !is_target(cur) &&
                p_store(out[w - 1], m1) && p_load0(cur, m2) && !strcmp(m1, m2)) { free(cur); changed = 1; continue; }
            out[w++] = cur;
        }
        free(lines); lines = out; nlines = w;
    }
}

int codegen(const Node *root, FILE *out, char *err, size_t errcap) {
    ERR = err; ERRCAP = errcap; g_label = 0; nfuncs = 0; nglobs = 0;
    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines); lines = NULL; nlines = linecap = 0;

    const Node *main_fn = NULL;
    for (int i = 0; i < root->nkids; i++) {
        const Node *d = root->kids[i];
        if (d->type == N_VARDECL) {
            strncpy(globs[nglobs].name, d->name, sizeof globs[0].name - 1);
            globs[nglobs].is_array = d->is_array; nglobs++;
        } else if (d->type == N_FUNC) {
            int p = 0;
            for (int k = 0; k < d->nkids; k++) if (d->kids[k]->type == N_PARAM) p++;
            strncpy(funcs[nfuncs].name, d->name, sizeof funcs[0].name - 1);
            funcs[nfuncs].nparams = p; nfuncs++;
            if (!strcmp(d->name, "main")) main_fn = d;
        }
    }
    if (!main_fn) { snprintf(err, errcap, "no main() function"); return 1; }

    put_raw("; generated by mcc");
    emit("LOADI R7, %d", STACK_BASE);
    emit("CALL main");
    emit("HALT");
    for (int i = 0; i < root->nkids; i++)
        if (root->kids[i]->type == N_FUNC && gen_function(root->kids[i])) return 1;

    for (int i = 0; i < root->nkids; i++) {
        const Node *d = root->kids[i];
        if (d->type != N_VARDECL) continue;
        put_raw("%s:", d->name);
        if (d->is_array) for (long k = 0; k < d->arr_size; k++) emit(".word 0");
        else {
            long v = 0;
            if (d->a) { if (d->a->type != N_INT) return fail(d->line, "global initializer must be a constant"); v = d->a->ival; }
            emit(".word %ld", v);
        }
    }

    peephole();
    for (int i = 0; i < nlines; i++) { fputs(lines[i], out); fputc('\n', out); free(lines[i]); }
    free(lines); lines = NULL; nlines = linecap = 0;
    return 0;
}
