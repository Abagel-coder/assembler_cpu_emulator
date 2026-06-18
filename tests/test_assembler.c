#include "assembler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                                 \
    }                                                               \
} while (0)

static size_t assemble_src(const char *src, uint32_t *words, size_t max_words) {
    char in[] = "/tmp/asm_test_in.asm";
    char out[] = "/tmp/asm_test_out.bin";
    FILE *f = fopen(in, "wb");
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    if (assemble(in, out) != 0) return (size_t)-1;

    f = fopen(out, "rb");
    if (!f) return (size_t)-1;
    unsigned char buf[1024];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    size_t nw = n / 4;
    for (size_t i = 0; i < nw && i < max_words; i++) {
        words[i] = (uint32_t)buf[i*4] | ((uint32_t)buf[i*4+1] << 8)
                 | ((uint32_t)buf[i*4+2] << 16) | ((uint32_t)buf[i*4+3] << 24);
    }
    return nw;
}

static void test_basic_encoding(void) {
    uint32_t w[16];
    size_t n = assemble_src(
        "LOADI R0, 20\n"
        "LOADI R1, 22\n"
        "ADD R0, R1\n"
        "HALT\n", w, 16);
    CHECK(n == 4);
    CHECK(w[0] == ENCODE_INSTR(OP_LOADI, 0, 0, 20));
    CHECK(w[1] == ENCODE_INSTR(OP_LOADI, 1, 0, 22));
    CHECK(w[2] == ENCODE_INSTR(OP_ADD,   0, 1, 0));
    CHECK(w[3] == ENCODE_INSTR(OP_HALT,  0, 0, 0));
}

static void test_labels_and_branch(void) {
    uint32_t w[16];
    /* forward + backward label resolution */
    size_t n = assemble_src(
        "  LOADI R0, 3\n"
        "loop:\n"
        "  LOADI R1, 1\n"
        "  SUB R0, R1\n"
        "  JNZ loop\n"
        "  HALT\n", w, 16);
    CHECK(n == 5);
    /* loop label is at address 4 (second instruction) */
    CHECK(w[3] == ENCODE_INSTR(OP_JNZ, 0, 0, 4));
}

static void test_memory_operands(void) {
    uint32_t w[16];
    size_t n = assemble_src(
        "LOAD R2, [R1+8]\n"
        "STORE [R3-4], R2\n"
        "HALT\n", w, 16);
    CHECK(n == 3);
    CHECK(w[0] == ENCODE_INSTR(OP_LOAD,  2, 1, 8));
    CHECK(w[1] == ENCODE_INSTR(OP_STORE, 3, 2, -4));
}

static void test_comments_and_directives(void) {
    uint32_t w[16];
    size_t n = assemble_src(
        "; a comment line\n"
        ".org 8\n"
        "LOADI R0, 5  ; inline comment\n"
        ".word 0xDEAD\n", w, 16);
    /* .org 8 leaves words[0..1] as zero padding, instr at index 2 */
    CHECK(n == 4);
    CHECK(w[0] == 0);
    CHECK(w[1] == 0);
    CHECK(w[2] == ENCODE_INSTR(OP_LOADI, 0, 0, 5));
    CHECK(w[3] == 0xDEAD);
}

static void test_undefined_label_fails(void) {
    char in[] = "/tmp/asm_bad_in.asm";
    char out[] = "/tmp/asm_bad_out.bin";
    FILE *f = fopen(in, "wb");
    const char *src = "JMP nowhere\nHALT\n";
    fwrite(src, 1, strlen(src), f);
    fclose(f);
    CHECK(assemble(in, out) != 0);
}

int main(void) {
    test_basic_encoding();
    test_labels_and_branch();
    test_memory_operands();
    test_comments_and_directives();
    test_undefined_label_fails();

    if (failures == 0) {
        printf("all assembler tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
