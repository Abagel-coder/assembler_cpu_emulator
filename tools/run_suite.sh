#!/bin/sh
# Sweep predictors and cache sizes across the benchmark suite, emitting CSV.
# Usage: make && sh tools/run_suite.sh   (writes results/*.csv)
set -e

ASM=build/asm
PIPE=build/pipe
OUT=results
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

PROGS="factorial fibonacci bubble_sort nested_loops gcd recursive streaming"
PREDICTORS="static 1bit 2bit gshare tournament"

for p in $PROGS; do
    $ASM examples/$p.asm -o "$TMP/$p.bin"
done

# --- predictor sweep (no caches) ---
HDR="program,predictor,ghist,l1d_size,l1d_assoc,l1d_block,instructions,cond,mispred,acc,mpki,l1i_miss,l1d_miss,cycles,cpi"
echo "$HDR" > "$OUT/predictors.csv"
for p in $PROGS; do
    for bp in $PREDICTORS; do
        echo "$p,$($PIPE --csv --predictor $bp "$TMP/$p.bin")" >> "$OUT/predictors.csv"
    done
done

# --- cache size sweep (2-bit predictor, 4-way, 32B blocks) ---
echo "$HDR" > "$OUT/caches.csv"
for p in $PROGS; do
    for sz in 256 512 1024 2048 4096 8192; do
        echo "$p,$($PIPE --csv --predictor 2bit --l1i 1024:4:32 --l1d $sz:4:32 "$TMP/$p.bin")" \
            >> "$OUT/caches.csv"
    done
done

echo "wrote $OUT/predictors.csv and $OUT/caches.csv"