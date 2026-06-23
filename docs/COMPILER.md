# mcc — a C-subset compiler for the custom ISA

`mcc` compiles a small C dialect to the project's assembly, which the existing
assembler turns into a binary the emulator runs. It reuses the whole toolchain as
its backend.

```
prog.c ──▶ [ mcc ] ──▶ prog.asm ──▶ [ asm ] ──▶ prog.bin ──▶ [ emu / pipe / dbg ]
```

## Usage

```sh
./build/mcc prog.c -o prog.asm     # compile to assembly
./build/mcc --ast prog.c           # print the parsed AST instead
./build/mcc prog.c -o p.asm && ./build/asm p.asm -o p.bin && ./build/emu p.bin
```

## Supported subset

- **Types:** `int` (32-bit), `int*`, `int[]`, `void` (return type only).
- **Declarations:** globals (incl. arrays, constant initializers), functions with
  parameters, locals (incl. arrays).
- **Statements:** `if`/`else`, `while`, `for`, `return`, blocks, expressions.
- **Expressions:** `+ - * / %`, comparisons, `&& || !` (short-circuit), bitwise
  `& | ^ ~`, shifts `<< >>`, unary `-`, assignment, calls, `a[i]`, `*p`, `&x`.
- **Built-ins:** `out(x)` prints an integer; `in()` reads one.
- **Recursion** is supported.

Not supported: structs, floats, `char`/strings, function pointers, `switch`,
`goto`, the preprocessor, multiple source files.

## Code generation

Stack-machine codegen: every expression leaves its result in `R0`, with the
hardware `PUSH`/`POP` stack for temporaries. The ISA is used unmodified.

| Register | Role |
|---|---|
| R0 | accumulator / return value |
| R1 | operator temporary |
| R2, R3 | scratch (e.g. modulo) |
| R6 | frame pointer (FP) |
| R7 | software stack pointer |

A software stack in `R7` holds call frames (no SP-arithmetic instruction is
needed since `R6`/`R7` are ordinary registers); `CALL`/`RET` use the hardware
stack for return addresses. Per call frame (grows down):

```
[R6 + 4 + 4i]   parameter i      (pushed by caller)
[R6 + 0]        saved caller FP
[R6 + off]      locals (off < 0); arrays occupy contiguous words
```

The caller pushes arguments onto the `R7` stack, `CALL`s, then pops them; the
callee saves/sets FP, allocates locals, runs, then restores and `RET`s. `main` is
an ordinary function reached from a small entry stub (`CALL main; HALT`). Globals
are emitted as `.word` data after the code and addressed by label. Array/pointer
lvalues compute `base + index*4` with word `LOAD`/`STORE`.

A peephole pass cleans the emitted assembly: store-to-load forwarding, adjacent
`PUSH`/`POP` → `MOV`, and no-op `MOV` removal.

## Testing

- `tests/test_compiler.c` — lexer/parser unit tests (valid programs, error cases,
  precedence, associativity).
- `tests/cc_test.sh` — compiles and runs the example programs, checking output.
- `tests/cc_difftest.sh` — differential test: compiles each program with the host
  C compiler and with `mcc`, runs both, and requires identical output.

Example programs: `straight.c`, `factorial.c`, `control.c`, `funcs.c`, `fib.c`,
`pointers.c`, `arrays.c`.

## Implementation

```
include/compiler.h   tokens + AST
src/clexer.c         lexer
src/cparser.c        recursive-descent parser, AST, pretty-printer
src/cgen.c           code generator + peephole
src/main_cc.c        CLI
```
