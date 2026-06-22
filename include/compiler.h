/* C-subset compiler front-end: tokens and AST (M1 = lex + parse + print). */
#ifndef COMPILER_H
#define COMPILER_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
    T_EOF, T_INT_LIT, T_IDENT,
    T_KW_INT, T_KW_VOID, T_KW_IF, T_KW_ELSE, T_KW_WHILE, T_KW_FOR, T_KW_RETURN,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACK, T_RBRACK, T_SEMI, T_COMMA,
    T_ASSIGN, T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_AMP, T_PIPE, T_CARET, T_TILDE, T_BANG,
    T_SHL, T_SHR, T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE, T_ANDAND, T_OROR
} TokType;

typedef struct {
    TokType type;
    int     line;
    long    ival;
    char    text[64];
} Token;

typedef enum {
    N_PROGRAM, N_FUNC, N_PARAM, N_VARDECL,
    N_BLOCK, N_IF, N_WHILE, N_FOR, N_RETURN, N_EXPRSTMT,
    N_INT, N_VAR, N_UNARY, N_BINARY, N_ASSIGN, N_CALL, N_INDEX
} NodeType;

typedef struct Node Node;
struct Node {
    NodeType type;
    int      line;
    long     ival;          /* N_INT */
    char     name[64];      /* identifiers, callee/func/var names */
    int      op;            /* TokType for N_UNARY / N_BINARY */
    int      is_ptr;        /* pointer type on a decl/param */
    int      is_array;
    long     arr_size;
    int      ret_void;      /* N_FUNC returns void */
    Node    *a, *b, *c, *d; /* fixed children (role depends on node type) */
    Node   **kids;          /* variable-length list (program/block/params/args) */
    int      nkids;
};

/* Lex src into a malloc'd token array (caller frees). NULL on error. */
Token *lex(const char *src, size_t *out_n, char *err, size_t errcap);

/* Parse tokens into an AST (arena-allocated; free with arena_free). */
Node  *parse(const Token *toks, size_t n, char *err, size_t errcap);

/* Convenience: lex + parse from source text. */
Node  *parse_source(const char *src, char *err, size_t errcap);

void   ast_print(const Node *root, FILE *out);
void   arena_free(void);
const char *tok_name(int t);   /* operator/token spelling, for printing */

/* Emit assembly for the program to `out`. Returns 0 on success, 1 on a codegen
 * error (message written to err). M2: a single straight-line main(). */
int    codegen(const Node *root, FILE *out, char *err, size_t errcap);

#endif /* COMPILER_H */
