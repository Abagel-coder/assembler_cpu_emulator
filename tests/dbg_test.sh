#!/bin/sh
# Drive the debugger non-interactively and check continue + breakpoint behavior.
set -e

BUILD=build
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

$BUILD/asm examples/factorial.asm -o "$TMP/f.bin"

if printf 'c\nq\n' | $BUILD/dbg "$TMP/f.bin" | grep -q 'R0=120'; then
    echo "dbg OK: continue to halt (R0=120)"
else
    echo "dbg FAIL: continue to halt"; exit 1
fi

if printf 'b 0xc\nc\nq\n' | $BUILD/dbg "$TMP/f.bin" | grep -q 'stopped at breakpoint 0xc'; then
    echo "dbg OK: breakpoint stop"
else
    echo "dbg FAIL: breakpoint stop"; exit 1
fi
