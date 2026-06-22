/* M1 tests: the front-end parses valid programs, rejects malformed ones, and
 * builds the expected AST (precedence, structure). */
#include "compiler.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                                 \
    }                                                               \
} while (0)

static Node *P(const char *src) {
    char err[160] = {0};
    Node *r = parse_source(src, err, sizeof err);
    if (!r) fprintf(stderr, "  parse error: %s\n", err);
    return r;
}

static void test_valid_programs(void) {
    CHECK(P("int main(void) { return 0; }") != NULL);
    CHECK(P("int f(int a, int b) { return a + b; } int main(void){ out(f(2,3)); return 0; }") != NULL);
    CHECK(P("int g[10]; int main(void){ g[1] = 5; return g[1]; }") != NULL);
    CHECK(P("int main(void){ int i; for (i = 0; i < 10; i = i + 1) { out(i); } return 0; }") != NULL);
    CHECK(P("int main(void){ int x = 1; if (x) x = 2; else x = 3; while (x) x = x - 1; return x; }") != NULL);
    arena_free();
}

static void test_errors(void) {
    char err[160];
    CHECK(parse_source("int main(void) { return 0 }", err, sizeof err) == NULL);   /* missing ; */
    CHECK(parse_source("int main(void) { return ; ", err, sizeof err) == NULL);    /* missing } */
    CHECK(parse_source("int 3 = 4;", err, sizeof err) == NULL);                     /* bad name */
    arena_free();
}

/* 1 + 2 * 3 must parse as +(1, *(2,3)) */
static void test_precedence(void) {
    Node *root = P("int main(void){ return 1 + 2 * 3; }");
    CHECK(root != NULL);
    Node *fn = root->kids[0];                 /* func */
    Node *body = fn->a;                        /* block */
    Node *ret = body->kids[0];                 /* return */
    Node *e = ret->a;                          /* expr: binary + */
    CHECK(e->type == N_BINARY && e->op == T_PLUS);
    CHECK(e->a->type == N_INT && e->a->ival == 1);
    CHECK(e->b->type == N_BINARY && e->b->op == T_STAR);
    CHECK(e->b->a->ival == 2 && e->b->b->ival == 3);
    arena_free();
}

/* assignment is right-associative and lower than comparison */
static void test_assign(void) {
    Node *root = P("int main(void){ int a; a = 1; return a; }");
    CHECK(root != NULL);
    Node *assignStmt = root->kids[0]->a->kids[1];   /* func->block->stmt[1] (after decl) */
    CHECK(assignStmt->type == N_EXPRSTMT);
    CHECK(assignStmt->a->type == N_ASSIGN);
    CHECK(assignStmt->a->a->type == N_VAR);
    arena_free();
}

int main(void) {
    test_valid_programs();
    test_errors();
    test_precedence();
    test_assign();
    if (failures == 0) { printf("all compiler (M1) tests passed\n"); return 0; }
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
