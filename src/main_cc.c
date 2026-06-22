/* mcc — C-subset compiler.
 *   mcc file.c            emit assembly to stdout
 *   mcc file.c -o out.asm emit assembly to a file
 *   mcc --ast file.c      print the AST instead
 */
#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t rd = fread(buf, 1, (size_t)n, f); buf[rd] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    int ast = 0;
    const char *in = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ast")) ast = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else in = argv[i];
    }
    if (!in) { fprintf(stderr, "usage: %s [--ast] [-o out.asm] file.c\n", argv[0]); return 2; }

    char *src = read_file(in);
    if (!src) return 1;
    char err[160] = {0};
    Node *root = parse_source(src, err, sizeof err);
    free(src);
    if (!root) { fprintf(stderr, "%s: %s\n", in, err); return 1; }

    int rc = 0;
    if (ast) {
        ast_print(root, stdout);
    } else {
        FILE *o = out ? fopen(out, "w") : stdout;
        if (!o) { fprintf(stderr, "cannot write %s\n", out); arena_free(); return 1; }
        rc = codegen(root, o, err, sizeof err);
        if (out) fclose(o);
        if (rc) fprintf(stderr, "%s: %s\n", in, err);
    }
    arena_free();
    return rc;
}
