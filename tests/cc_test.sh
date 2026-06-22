#!/bin/sh
# End-to-end M2: compile C -> assembly -> binary -> run, check output.
set -e
B=build
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

$B/mcc examples/straight.c -o "$T/s.asm"
$B/asm "$T/s.asm" -o "$T/s.bin"
out=$($B/emu "$T/s.bin")

if echo "$out" | grep -q '^50$' && echo "$out" | grep -q '^1$' \
   && echo "$out" | grep -q '^8$' && echo "$out" | grep -q 'R0 = 50'; then
    echo "cc OK: straight.c -> 50, 1, 8, R0=50"
else
    echo "cc FAIL:"; echo "$out"; exit 1
fi
