# Custom ISA — Assembler, Emulator & Microarchitecture Simulator

A toolchain and cycle-accurate performance model for a custom 32-bit instruction
set, written in C11 with no dependencies beyond libc. A C-subset compiler, an
assembler, and an emulator form a source → assembly → binary → execution stack:

- **`mcc`** — C-subset compiler (`int`, pointers, arrays, globals, functions,
  recursion) emitting assembly; see [docs/COMPILER.md](docs/COMPILER.md)
- **`asm`** — two-pass assembler (labels, directives, error checking)
- **`emu`** — functional emulator with trace mode and instruction-throughput stats
- **`disasm`** — disassembler (binary → assembly, round-trip verified)
- **`dbg`** — interactive step debugger with a full-screen terminal dashboard
  (live registers/flags, disassembly around PC, memory, and program output) and
  single-key step/continue/breakpoint controls; falls back to a line REPL when
  input is piped
- **`pipe`** — microarchitecture simulator: a structural 5-stage pipeline with
  hazard detection, forwarding, static/bimodal/gshare predictors with 2- and
  3-way tournament choosers, a configurable cache hierarchy, in-order superscalar
  issue (1–4 wide), and a Tomasulo-style out-of-order engine, reporting CPI, IPC,
  MPKI, miss rate, and AMAT

```
  program.asm ──▶ [ asm ] ──▶ program.bin ──┬──▶ [ emu ]    functional run + MIPS
                  2-pass                     ├──▶ [ disasm ] binary → assembly
                                             └──▶ [ pipe ]   pipeline + bpred + caches
```

## Build & test

```sh
make          # builds asm, emu, disasm, pipe into build/
make test     # CPU, assembler, integration, pipeline, and round-trip suites
```

## Quick start

```sh
./build/asm examples/bubble_sort.asm -o bs.bin
./build/emu --stats bs.bin                  # run; print instruction count + MIPS
./build/disasm bs.bin                        # annotated disassembly
./build/dbg bs.bin                           # interactive step debugger
./build/pipe bs.bin                          # full pipeline/predictor/cache report
./build/pipe --predictor gshare --l1d 2048:4:32 bs.bin --csv   # one configured run
```

---

## Instruction set architecture

32-bit fixed-width instructions: 6-bit opcode, two 3-bit register fields, 20-bit
sign-extended immediate. Registers `R0`–`R7`, `PC`, `SP`, and `FLAGS` (Zero,
Carry, Overflow, Negative). Memory is 64 KB, byte-addressable, little-endian;
the stack grows down from the top.

Addresses `0xE000`–`0xE00F` are memory-mapped device registers: `LOAD`/`STORE`
there reach devices instead of RAM — `0xE000` prints a decimal integer, `0xE004`
prints a byte as a character, `0xE008` reads an integer, `0xE00C` halts.

| Category         | Mnemonics                                       |
|------------------|-------------------------------------------------|
| Arithmetic/logic | ADD SUB MUL DIV AND OR XOR NOT SHL SHR          |
| Data movement    | MOV LOAD STORE LOADI PUSH POP                   |
| Control flow     | JMP JZ JNZ JG JL CALL RET                       |
| System           | NOP HALT IN OUT                                 |

```asm
        LOADI R0, 5        ; immediate (decimal or 0x hex, may be negative)
loop:                      ; label
        LOAD  R1, [R2+4]   ; base + offset memory access
        JNZ   loop         ; labels resolve to addresses
.org 256
arr:    .word 42           ; data directive
```

---

## Microarchitecture simulator

`pipe` uses a decoupled functional + timing design. A reference CPU executes the
program, so architectural results are correct by construction; the instruction
stream (with real branch outcomes and memory addresses) then drives a structural
timing model. Instructions are generated on demand into a bounded ring buffer, so
simulator memory is independent of program length — a 20M-instruction run uses
~1.3 MB.

- **Pipeline** — five stages (IF/ID/EX/MEM/WB) advanced one cycle at a time with
  explicit stage latches. A hazard-detection unit inspects the EX/MEM latches; a
  forwarding toggle switches between full EX/MEM forwarding (only load-use stalls
  remain) and none (a consumer waits until its producer reaches WB). Flags are
  modeled as a 9th register so flag-setter → conditional-branch dependencies count.
- **Branch prediction** — static-not-taken, 1-bit, 2-bit bimodal, gshare
  (global-history XOR PC), a 2-way tournament whose chooser selects between
  bimodal and gshare per context, and a 3-way tournament that adds static as a
  fallback; plus a BTB for targets. Mispredictions drive the flush penalty.
- **Caches** — configurable L1 I/D plus a unified L2 (size, block, associativity,
  LRU/FIFO/random replacement); an L1 miss probes L2, an L2 miss goes to memory.
  Misses stall the relevant stage and feed a multi-level AMAT.
- **Superscalar issue** — an in-order N-wide issue model (width 1 reproduces the
  scalar pipeline exactly) reporting IPC scaling and its recurrence-bound limit.
- **Out-of-order** — a Tomasulo-style model: in-order dispatch/commit, OoO
  execute, implicit register renaming (true deps only), a finite ROB, and
  multi-cycle FU latencies (DIV 20, MUL 3, load 2). An `in_order` toggle gives an
  identical-resource baseline to isolate the gain from dynamic scheduling.

### Methodology & validation
- **Oracle equality** — the simulator's final register and memory state is
  asserted equal to an independent `cpu.c` run on every benchmark.
- **Analytical cross-check** — pipeline cycle counts are locked against a
  hand-derived model (this caught a retirement off-by-one during development).
- **37 timing assertions** across pipeline, predictor, cache, and tournament tests.

---

## Results

Measured on the bundled benchmark suite via `tools/run_suite.sh`. Regenerate the
data and charts with `make && sh tools/run_suite.sh && python3 tools/plot.py`.

### Throughput (functional emulator)
~**67 MIPS** — 20,000,063 instructions in 0.30 s.

### Forwarding
Forwarding yields **1.34×–1.71×** by removing data-hazard stalls. The remaining
stalls are load-use hazards, which forwarding cannot eliminate.

| Program | CPI no-fwd | CPI fwd | Speedup |
|---|--:|--:|--:|
| fibonacci | 2.09 | 1.33 | 1.57× |
| bubble_sort | 2.08 | 1.38 | 1.50× |
| nested_loops | 1.71 | 1.05 | 1.63× |
| streaming | 1.93 | 1.13 | 1.71× |

### Branch prediction
![branch prediction accuracy](docs/predictors_accuracy.svg)

The ordering depends on the workload:

- **Correlated branches** (`nested_loops`, a short fixed inner loop): gshare
  learns the repeating `T,T,T,N` pattern and reaches **99.4%**, vs. 79.8% for
  bimodal and 20% for static. It predicts the loop exits, which a per-PC counter
  cannot.
- **Predictable loops** (`streaming`): every dynamic predictor reaches ~99.5%.
- **Tiny / data-dependent code** (`gcd`, `recursive`): dynamic predictors lose to
  static-not-taken. There are too few branches to warm up, and a {bimodal, gshare}
  tournament cannot recover when both components mispredict.
- **Tournament** is never worse than its worse component and tracks the better one
  (nested_loops 99.2% ≈ gshare; bubble_sort matches bimodal).

The 2-way tournament cannot recover the `gcd`/`recursive` cases, since both its
components lose to static there. A 3-way tournament adds static as the warmup
default and lets the dynamic predictors take over once they out-score it
(mispredicts, lower is better):

| Benchmark | static-NT | 2-way tournament | 3-way (+static) |
|---|--:|--:|--:|
| recursive | 1 | 2 | **1** |
| gcd | 2 | 4 | **3** |
| nested_loops | 399 | 4 | 5 |
| bubble_sort | 19 | 9 | 13 |

The 3-way variant recovers `recursive` and improves `gcd`, but the chooser's own
warmup costs one misprediction per branch site, which shows up as a small
regression on loop-heavy code. Adaptation cannot help workloads too short to
amortize training; static wins there because it needs none.

### Caches — the capacity cliff
![L1-D miss rate vs cache size](docs/cache_cliff.svg)

`streaming` sweeps a 1.6 KB array three times. While L1-D is smaller than the
working set the array re-misses every pass (**12.5%**); once it fits (≥ 2 KB) only
first-pass compulsory misses remain (**3.1%**), and CPI drops 1.30 → 1.16.

With a realistic memory latency (100 cycles), adding a unified 16 KB L2 behind the
1 KB L1 lets the spilled working set hit L2 (10 cycles) instead of memory:

| Config | streaming cycles | CPI |
|---|--:|--:|
| L1 only | 32440 | 3.00 |
| L1 + L2 | 16510 | 1.53 |

L2 catches the array (13% L2 miss rate, all compulsory), cutting memory stalls
20400 → 4470 for a **1.96×** speedup; the resulting L1-D AMAT is 3.9 cycles.

### Superscalar issue width
In-order issue at 1, 2, and 4 instructions per cycle (bimodal-2bit, forwarding
on, cycles):

| Benchmark | width 1 | width 2 | width 4 | 1→2 | 1→4 |
|---|--:|--:|--:|--:|--:|
| fibonacci | 74 | 43 | 42 | 1.72× | 1.76× |
| streaming | 12041 | 8839 | 7237 | 1.36× | 1.66× |
| bubble_sort | 181 | 142 | 127 | 1.27× | 1.43× |
| nested_loops | 1313 | 915 | 914 | 1.44× | 1.44× |

Two-wide issue gives 1.3–1.7×. Going to 4-wide helps `streaming` (independent
address and counter chains) but barely moves `fibonacci` and `nested_loops`,
whose loop-carried recurrences (the `a,b` update and the loop counter) cap the
available ILP regardless of issue width.

### Out-of-order vs in-order
Same model, same resources (width 2, ROB 64, DIV=20/MUL=3/load=2 latencies); the
only difference is in-order vs out-of-order execution.

| Benchmark | in-order CPI | OoO CPI | OoO speedup |
|---|--:|--:|--:|
| ooo | 2.92 | 0.64 | 4.60× |
| streaming | 0.67 | 0.50 | 1.33× |
| bubble_sort | — | — | 1.04× |
| fibonacci / nested_loops | — | — | 1.00× |

`ooo` issues an independent long-latency `DIV` each iteration with unrelated
accumulation behind it; OoO overlaps the divides and runs the independent work
during them, reaching IPC 1.58 against in-order's 0.34. `streaming` reaches IPC
1.99 (near the width-2 limit). Recurrence-bound loops (`fibonacci`,
`nested_loops`) gain nothing — dynamic scheduling cannot create ILP that the
dependency chain does not allow.

---

## Project layout

```
include/   isa.h cpu.h memory.h device.h assembler.h bpred.h cache.h pipe_sim.h
src/       cpu.c memory.c isa.c device.c        # emulator core + MMIO devices
           lexer.c parser.c encoder.c          # assembler (two-pass)
           bpred.c cache.c pipe_sim.c          # microarchitecture model
           main_emu.c main_asm.c disasm.c main_pipe.c main_dbg.c
examples/  factorial fibonacci bubble_sort nested_loops gcd recursive
           streaming ooo mmio
tests/     test_cpu_core test_assembler test_integration test_pipe
           roundtrip.sh dbg_test.sh
tools/     run_suite.sh (CSV sweeps)  plot.py (SVG charts)
```

## Future work
- Finite execution ports / load-store queue in the OoO model
- Additional MMIO devices (timer, framebuffer) and interrupts
