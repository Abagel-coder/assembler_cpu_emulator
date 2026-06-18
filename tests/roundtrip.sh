#!/bin/sh
# Round-trip test: source -> asm -> disasm -r -> asm must reproduce the binary.
# Proves encoder/decoder symmetry. Runs on code-only programs (no data sections).
set -e

BUILD=build
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
for prog in factorial fibonacci; do
    $BUILD/asm examples/$prog.asm -o "$TMP/a.bin"
    $BUILD/disasm -r "$TMP/a.bin" > "$TMP/round.asm"
    $BUILD/asm "$TMP/round.asm" -o "$TMP/b.bin"
    if cmp -s "$TMP/a.bin" "$TMP/b.bin"; then
        echo "roundtrip OK: $prog"
    else
        echo "roundtrip FAIL: $prog"
        fail=1
    fi
done

exit $fail
