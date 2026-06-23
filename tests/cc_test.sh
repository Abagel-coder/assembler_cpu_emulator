#!/bin/sh
# End-to-end M2: compile C -> assembly -> binary -> run, check output.
set -e
B=build
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

run() {   # $1 = source.c  -> echoes emulator output
    $B/mcc "$1" -o "$T/p.asm"
    $B/asm "$T/p.asm" -o "$T/p.bin"
    $B/emu "$T/p.bin"
}

out=$(run examples/straight.c)
if echo "$out" | grep -q '^50$' && echo "$out" | grep -q '^1$' \
   && echo "$out" | grep -q '^8$' && echo "$out" | grep -q 'R0 = 50'; then
    echo "cc OK: straight.c -> 50, 1, 8, R0=50"
else
    echo "cc FAIL straight.c:"; echo "$out"; exit 1
fi

out=$(run examples/factorial.c)
if echo "$out" | grep -q '^120$'; then
    echo "cc OK: factorial.c -> 120"
else
    echo "cc FAIL factorial.c:"; echo "$out"; exit 1
fi

out=$(run examples/control.c)
if echo "$out" | grep -q '^112$' && echo "$out" | grep -q 'R0 = 112'; then
    echo "cc OK: control.c -> 112 (if/for/&&/||/!)"
else
    echo "cc FAIL control.c:"; echo "$out"; exit 1
fi

out=$(run examples/funcs.c)
if echo "$out" | grep -q '^25$' && echo "$out" | grep -q 'R0 = 25'; then
    echo "cc OK: funcs.c -> 25 (params, nested calls)"
else
    echo "cc FAIL funcs.c:"; echo "$out"; exit 1
fi

out=$(run examples/fib.c)
if echo "$out" | grep -q '^55$'; then
    echo "cc OK: fib.c -> 55 (recursion)"
else
    echo "cc FAIL fib.c:"; echo "$out"; exit 1
fi

out=$(run examples/pointers.c)
if echo "$out" | grep -q '^15$'; then
    echo "cc OK: pointers.c -> 15 (globals, array decay, pointer param)"
else
    echo "cc FAIL pointers.c:"; echo "$out"; exit 1
fi

out=$(run examples/arrays.c)
if echo "$out" | grep -q '^149$' && echo "$out" | grep -q 'R0 = 149'; then
    echo "cc OK: arrays.c -> 149 (local array, &elem, *p)"
else
    echo "cc FAIL arrays.c:"; echo "$out"; exit 1
fi
