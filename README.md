# Custom ISA — Assembler, Emulator & Pipeline Simulator

A complete toolchain for a custom 32-bit instruction set architecture, written
in C11 with no dependencies beyond libc:

- **Assembler** (`asm`) — two-pass, label resolution, directives, flat-binary output
- **Emulator** (`emu`) — fetch-decode-execute CPU with trace mode and throughput stats
- **Disassembler** (`disasm`) — binary back to assembly (round-trip verified)
- **Pipeline simulator** (`pipe`) — trace-driven 5-stage timing model with hazard
  analysis, forwarding comparison, and CPI reporting

```
  program.asm ──▶ [ asm ] ──▶ program.bin ──▶ [ emu ]  ──▶ output + stats
                                   │
                                   ├──▶ [ disasm ] ──▶ assembly
                                   └──▶ [ pipe ]   ──▶ CPI / hazard analysis
```

## Build & test

```sh
make          # builds asm, emu, disasm, pipe into build/
make test     # runs unit, assembler, integration, and round-trip suites
```

## Quick start

```sh
./build/asm examples/factorial.asm -o factorial.bin
./build/emu --stats factorial.bin       # run, then print instruction count & MIPS
./build/emu --trace factorial.bin       # per-cycle register/flag trace
./build/disasm factorial.bin            # annotated disassembly
./build/pipe factorial.bin              # pipeline / CPI analysis
```

## Instruction set architecture

32-bit fixed-width instructions:

```
 31        26 25     23 22     20 19                   0
+-----------+---------+---------+----------------------+
|  opcode   |  rdest  |  rsrc   |   immediate / offset |
|  6 bits   | 3 bits  | 3 bits  |        20 bits       |
+-----------+---------+---------+----------------------+
```

- **Registers:** `R0`–`R7`, plus `PC`, `SP`, and `FLAGS` (Zero, Carry, Overflow, Negative)
- **Memory:** 64 KB, byte-addressable, little-endian; stack grows down from the top
- **Addressing:** register-direct, immediate, and base+offset memory (`[R2+8]`)

| Category         | Mnemonics                                       |
|------------------|-------------------------------------------------|
| Arithmetic/logic | ADD SUB MUL DIV AND OR XOR NOT SHL SHR          |
| Data movement    | MOV LOAD STORE LOADI PUSH POP                   |
| Control flow     | JMP JZ JNZ JG JL CALL RET                       |
| System           | NOP HALT IN OUT                                 |

### Assembly syntax

```asm
; comments start with a semicolon
        LOADI R0, 5        ; immediate (decimal or 0x hex, may be negative)
loop:                      ; label
        LOAD  R1, [R2+4]   ; base + offset memory access
        JNZ   loop         ; labels resolve to addresses
.org 256                   ; set assembly address
arr:    .word 42           ; emit a literal word
```

## Pipeline analysis

`pipe` executes the program to capture the real dynamic instruction stream, then
runs it through an in-order 5-stage (IF/ID/EX/MEM/WB) timing model. It models RAW
data hazards on registers and flags, load-use hazards, and taken-branch control
hazards, and reports cycles/CPI both with and without operand forwarding.

Measured on the bundled examples:

| Program     | Dyn. instrs | CPI (no fwd) | CPI (fwd) | Forwarding speedup |
|-------------|------------:|-------------:|----------:|-------------------:|
| factorial   |          20 |        2.150 |     1.600 |              1.34x |
| fibonacci   |          66 |        2.091 |     1.333 |              1.57x |
| bubble_sort |         144 |        2.076 |     1.382 |              1.50x |

Forwarding eliminates nearly all data stalls; the residual stalls are
unavoidable load-use hazards (a load result isn't ready until after MEM).

## Project layout

```
include/   isa.h  cpu.h  memory.h  assembler.h
src/       cpu.c memory.c isa.c          # emulator core
           lexer.c parser.c encoder.c    # assembler (two-pass)
           main_emu.c main_asm.c         # CLI entry points
           disasm.c pipeline.c           # tooling
examples/  factorial.asm fibonacci.asm bubble_sort.asm
tests/     test_cpu_core.c test_assembler.c test_integration.c roundtrip.sh
```

## Testing

The suite covers per-opcode unit tests (hand-encoded), assembler encoding and
error paths (undefined labels, immediate overflow), full assemble-and-run
integration on the example programs, and an assemble→disassemble→assemble
round-trip that verifies encoder/decoder symmetry.
