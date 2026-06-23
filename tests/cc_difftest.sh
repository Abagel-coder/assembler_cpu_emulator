#!/bin/sh
# Differential test: compile each program with the host C compiler and with mcc,
# run both, and require identical program output. Validates mcc's codegen against
# a reference C implementation. (Outputs are non-negative, so %u and %d agree.)
#
# No `set -e`: the test programs return their result as the process exit code
# (e.g. straight.c returns 50), which is expected, not a failure.

B=build
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

SHIM='#include <stdio.h>
static void out(int x){ printf("%d\n", x); }
static int in(void){ int x; if (scanf("%d",&x)!=1) x=0; return x; }'

fails=0
for f in straight factorial control funcs fib pointers arrays; do
    src=examples/$f.c

    printf '%s\n' "$SHIM" > "$T/h.c"
    cat "$src" >> "$T/h.c"
    cc -w "$T/h.c" -o "$T/h"
    host=$(printf '' | "$T/h")

    $B/mcc "$src" -o "$T/m.asm"
    $B/asm "$T/m.asm" -o "$T/m.bin"
    mine=$($B/emu "$T/m.bin" | sed '/^R0 = /,$d')

    if [ "$host" = "$mine" ]; then
        echo "diff OK: $f (mcc matches host cc)"
    else
        echo "diff FAIL: $f"; echo "  host: [$host]"; echo "  mcc:  [$mine]"; fails=1
    fi
done
exit $fails
