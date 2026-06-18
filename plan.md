# Custom ISA & CPU Emulator — Implementation Plan

Language: **C** (C11, no external dependencies beyond libc)

## 1. ISA Specification

Lock this down on paper before writing any code — it's the contract between the assembler and the emulator.

### Instruction format (32-bit fixed width)

```
 31        26 25     23 22     20 19                  0
+-----------+---------+---------+----------------------+
|  opcode   |  rdest  |  rsrc   |   immediate / offset  |
|  6 bits   | 3 bits  | 3 bits  |        20 bits         |
+-----------+---------+---------+----------------------+
```

### Registers

- `R0`–`R7` — general purpose (3-bit field can address all 8)
- `PC` — program counter
- `SP` — stack pointer
- `FLAGS` — Zero, Carry, Overflow, Negative

### Opcode table (starter set)

| Category      | Mnemonics                                  |
|----------------|---------------------------------------------|
| Arithmetic/logic | ADD, SUB, MUL, DIV, AND, OR, XOR, NOT, SHL, SHR |
| Data movement  | MOV, LOAD, STORE, LOADI, PUSH, POP          |
| Control flow   | JMP, JZ, JNZ, JG, JL, CALL, RET             |
| System         | NOP, HALT, IN, OUT                          |

### Addressing modes

- Register-direct: `ADD R1, R2`
- Immediate: `LOADI R1, 42`
- Memory direct/indirect: `LOAD R1, [R2+offset]`

```c
/* isa.h */
typedef enum {
    OP_NOP = 0x00, OP_HALT,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_AND, OP_OR,  OP_XOR, OP_NOT, OP_SHL, OP_SHR,
    OP_MOV, OP_LOAD, OP_STORE, OP_LOADI, OP_PUSH, OP_POP,
    OP_JMP, OP_JZ,  OP_JNZ, OP_JG,  OP_JL,  OP_CALL, OP_RET,
    OP_IN,  OP_OUT
} Opcode;

typedef struct {
    Opcode   opcode;   /* bits 31-26 */
    uint8_t  rdest;    /* bits 25-23 */
    uint8_t  rsrc;     /* bits 22-20 */
    int32_t  imm;      /* bits 19-0, sign-extended */
} Instruction;

#define OPCODE_MASK   0xFC000000u
#define RDEST_MASK    0x03800000u
#define RSRC_MASK     0x00700000u
#define IMM_MASK      0x000FFFFFu

#define DECODE_OPCODE(w)  (((w) & OPCODE_MASK) >> 26)
#define DECODE_RDEST(w)   (((w) & RDEST_MASK)  >> 23)
#define DECODE_RSRC(w)    (((w) & RSRC_MASK)   >> 20)
#define DECODE_IMM(w)     sign_extend20((w) & IMM_MASK)

#define ENCODE_INSTR(op, rd, rs, imm) \
    (((uint32_t)(op) << 26) | ((uint32_t)(rd) << 23) | \
     ((uint32_t)(rs) << 20) | ((uint32_t)(imm) & IMM_MASK))
```

## 2. Project Structure

```
project/
  include/
    isa.h
    cpu.h
    memory.h
    assembler.h
  src/
    cpu.c           /* fetch-decode-execute loop */
    memory.c        /* memory array + load/store helpers */
    lexer.c
    parser.c
    encoder.c
    disassembler.c
    main_emu.c       /* emulator entry point */
    main_asm.c        /* assembler entry point */
  examples/
    factorial.asm
    fibonacci.asm
    bubble_sort.asm
  tests/
  Makefile
  README.md
```

## 3. CPU Emulator Core (build this first)

Build before the assembler so instructions can be tested by hand-encoding raw bytes — isolates bugs to one half of the system at a time.

```c
/* memory.h */
#define MEM_SIZE (1 << 16)   /* 64KB */

typedef struct {
    uint8_t bytes[MEM_SIZE];
} Memory;

uint32_t mem_read32(Memory *m, uint32_t addr);
void     mem_write32(Memory *m, uint32_t addr, uint32_t value);
```

```c
/* cpu.h */
typedef struct {
    uint32_t regs[8];
    uint32_t pc;
    uint32_t sp;
    struct { uint8_t z, c, o, n; } flags;
    Memory  *mem;
    int      halted;
} CPU;

void cpu_init(CPU *cpu, Memory *mem);
void cpu_step(CPU *cpu);   /* one fetch-decode-execute cycle */
void cpu_run(CPU *cpu, int trace);
```

```c
/* cpu.c — core loop skeleton */
void cpu_step(CPU *cpu) {
    uint32_t raw = mem_read32(cpu->mem, cpu->pc);
    Instruction ins = {
        .opcode = DECODE_OPCODE(raw),
        .rdest  = DECODE_RDEST(raw),
        .rsrc   = DECODE_RSRC(raw),
        .imm    = DECODE_IMM(raw)
    };
    cpu->pc += 4;

    switch (ins.opcode) {
        case OP_ADD:
            cpu->regs[ins.rdest] += cpu->regs[ins.rsrc];
            update_flags(cpu, cpu->regs[ins.rdest]);
            break;
        case OP_LOADI:
            cpu->regs[ins.rdest] = ins.imm;
            break;
        case OP_JMP:
            cpu->pc = ins.imm;
            break;
        case OP_HALT:
            cpu->halted = 1;
            break;
        /* ... remaining opcodes ... */
        default:
            fprintf(stderr, "Illegal opcode 0x%x at PC=0x%x\n",
                    ins.opcode, cpu->pc - 4);
            cpu->halted = 1;
    }
}
```

Implementation order:

1. Memory model + read/write helpers (handle byte vs. word access)
2. Register file struct + flags
3. Fetch-decode-execute loop with a `switch` dispatch
4. Instructions in batches: arithmetic → data movement → control flow → stack (CALL/RET, PUSH/POP)
5. `--trace` mode: print PC, decoded instruction, and register state every cycle

Test by writing raw `uint32_t[]` arrays of hand-encoded instructions in a C test file and asserting on final register state.

## 4. Assembler (two-pass design)

```c
/* assembler.h */
typedef struct {
    char    *name;
    uint32_t address;
} Symbol;

typedef struct {
    Symbol  *symbols;
    size_t   count;
} SymbolTable;

int assemble(const char *src_path, const char *out_path);
```

1. **Lexer** — tokenize mnemonics, register names (`R0`–`R7`), immediates, labels (`loop:`), directives (`.data`, `.org`), comments.
2. **Pass 1 (symbol resolution)** — walk tokens, track instruction addresses, build the label → address table. Don't encode yet; forward references aren't resolvable until this pass finishes.
3. **Pass 2 (encoding)** — re-walk tokens, resolve labels via the symbol table, encode each instruction with `ENCODE_INSTR(...)`.
4. **Output** — write a flat binary file, or a small custom object format with a header (entry point, data section offset/size).

## 5. Integration & Testing

- Pipeline: `./assembler input.asm -o output.bin` then `./emulator output.bin`
- Example programs exercising the full ISA: factorial (loops + arithmetic), fibonacci (loops + branching), bubble sort (memory + nested control flow)
- Unit tests per opcode in isolation; integration tests on full programs checking final register/memory state
- Edge cases: division by zero, stack overflow/underflow, invalid opcodes, immediate overflow

## 6. Stretch Goals

- Disassembler (binary → assembly) — doubles as a sanity check on the encoder
- Interactive step debugger with breakpoints
- Simple syscall-style I/O (`OUT` prints a register to stdout)
- Macro support in the assembler

## Suggested Build Order

Spec doc → memory/register structs → CPU loop with arithmetic only → expand CPU instructions → lexer → two-pass parser/encoder → integration testing with example programs → debugger/disassembler polish.

## Milestone Roadmap (execution tracker)

- **M1 — done.** ISA spec (`isa.h`), memory model, CPU core with arithmetic/logic + NOP/HALT/LOADI/JMP, hand-encoded unit tests.
- **M2 — done.** Full instruction set (data movement, stack, control flow, CALL/RET, I/O), `--trace` mode, `emu` CLI.
- **M3 — assembler lexer + parser.** Tokenize mnemonics/registers/immediates/labels/directives/comments; parse into an instruction IR with unresolved label references.
- **M4 — assembler encoder + two-pass + CLI.** Pass 1 builds the label→address table; pass 2 resolves labels and emits via `ENCODE_INSTR`; `asm` CLI (`./asm in.asm -o out.bin`).
- **M5 — integration.** Example programs (factorial, fibonacci, bubble sort), end-to-end assemble→run tests, edge cases (immediate overflow, undefined labels, div-by-zero).
- **M6 — high-ROI resume differentiators.** Run real benchmark programs and report instruction throughput; 5-stage pipeline simulation (IF/ID/EX/MEM/WB) with hazard detection and CPI reporting; disassembler (binary → assembly, reusing `opcode_name()`).