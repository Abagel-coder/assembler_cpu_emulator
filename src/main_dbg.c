/* Interactive debugger. On a terminal it renders a full-screen dashboard
 * (registers, disassembly, memory, output) with single-key controls; when input
 * is piped it falls back to a line-oriented REPL (for scripts/tests). */
#include "cpu.h"
#include "memory.h"
#include "isa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define MAX_BP 32
#define R "\033[0m"
#define DIM "\033[2m"
#define BOLD "\033[1m"
#define REV "\033[7m"
#define RED "\033[31m"
#define GRN "\033[32m"
#define CYN "\033[36m"
#define YEL "\033[33m"

static uint32_t bps[MAX_BP];
static int      nbp = 0;
static uint32_t prog_size = 0;
static uint32_t watch = 0;
static char     msg[160] = "ready";

static int is_bp(uint32_t a) { for (int i = 0; i < nbp; i++) if (bps[i] == a) return 1; return 0; }
static void add_bp(uint32_t a) {
    if (is_bp(a)) { snprintf(msg, sizeof msg, "breakpoint already at 0x%x", a); return; }
    if (nbp >= MAX_BP) { snprintf(msg, sizeof msg, "breakpoint table full"); return; }
    bps[nbp++] = a; snprintf(msg, sizeof msg, "breakpoint set at 0x%x", a);
}
static void del_bp(uint32_t a) {
    for (int i = 0; i < nbp; i++) if (bps[i] == a) {
        bps[i] = bps[--nbp]; snprintf(msg, sizeof msg, "breakpoint cleared at 0x%x", a); return;
    }
    snprintf(msg, sizeof msg, "no breakpoint at 0x%x", a);
}

static void do_step(CPU *cpu, long n) {
    for (long i = 0; i < n && !cpu->halted; i++) cpu_step(cpu);
    snprintf(msg, sizeof msg, cpu->halted ? "halted" : "stepped");
}
static void do_continue(CPU *cpu) {
    if (cpu->halted) { snprintf(msg, sizeof msg, "already halted"); return; }
    long guard = 200000000;
    do { cpu_step(cpu); } while (!cpu->halted && !is_bp(cpu->pc) && --guard > 0);
    if (cpu->halted) snprintf(msg, sizeof msg, "halted");
    else if (guard <= 0) snprintf(msg, sizeof msg, "stopped: step limit");
    else snprintf(msg, sizeof msg, "stopped at breakpoint 0x%x", cpu->pc);
}

static int load(Memory *mem, const char *path) {
    mem_init(mem);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    prog_size = (uint32_t)fread(mem->bytes, 1, MEM_SIZE, f);
    fclose(f);
    return 0;
}

/* ---------- full-screen TUI ---------- */
static struct termios orig_tio;
static void raw_on(void) {
    struct termios t = orig_tio;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    fputs("\033[?25l", stdout);              /* hide cursor */
}
static void raw_off(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
    fputs("\033[?25h\033[2J\033[H", stdout); /* show cursor, clear */
    fflush(stdout);
}

static void print_output(void) {
    const char *b = cpu_capture_buf();
    int total = 0; for (const char *p = b; *p; p++) if (*p == '\n') total++;
    int skip = total > 5 ? total - 5 : 0, line = 0;
    const char *p = b;
    while (line < skip && *p) { if (*p == '\n') line++; p++; }
    fputs("  ", stdout);
    for (; *p; p++) { putchar(*p); if (*p == '\n') fputs("  ", stdout); }
    putchar('\n');
}

static void render_tui(CPU *cpu, Memory *mem) {
    char text[64];
    fputs("\033[2J\033[H", stdout);
    printf(BOLD CYN "  Custom ISA Debugger" R "   %s\n\n",
           cpu->halted ? GRN "[HALTED]" R : "");

    printf("  " DIM "regs" R "   ");
    for (int i = 0; i < 4; i++) printf("R%d=%-10u ", i, cpu->regs[i]);
    printf("\n         ");
    for (int i = 4; i < 8; i++) printf("R%d=%-10u ", i, cpu->regs[i]);
    printf("\n  " DIM "ctrl" R "   PC=" YEL "0x%04x" R "  SP=0x%05x   "
           "Z%d C%d O%d N%d   icount=%llu\n",
           cpu->pc, cpu->sp, cpu->flags.z, cpu->flags.c, cpu->flags.o, cpu->flags.n,
           (unsigned long long)cpu->icount);

    printf("\n  " DIM "disassembly" R "\n");
    uint32_t start = cpu->pc >= 20 ? cpu->pc - 20 : 0;
    for (int i = 0; i < 12; i++) {
        uint32_t a = start + (uint32_t)i * 4;
        if (a + 4 > prog_size) break;
        isa_disasm(text, sizeof text, mem_read32(mem, a));
        if (a == cpu->pc) printf("  " REV GRN "> %04x  %-22s" R "\n", a, text);
        else if (is_bp(a)) printf("  " RED "* %04x" R "  %-22s\n", a, text);
        else printf("    %04x  %-22s\n", a, text);
    }

    printf("\n  " DIM "memory @ 0x%04x" R "\n", watch);
    for (uint32_t r = 0; r < 4; r++) {
        uint32_t base = watch + r * 16;
        printf("  " DIM "%04x:" R, base);
        for (uint32_t c = 0; c < 4; c++)
            if (base + c * 4 + 4 <= MEM_SIZE) printf(" %08x", mem_read32(mem, base + c * 4));
        printf("\n");
    }

    printf("\n  " DIM "output" R "\n");
    print_output();

    printf("\n  " DIM "[s]tep  [c]ontinue  [f]inish  [r]eset  [b]reak  [m]em  [q]uit" R
           "   %s\n", msg);
    fflush(stdout);
}

static int tui_prompt(const char *label, uint32_t *out) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);   /* temporary canonical input */
    printf("\033[?25h\033[999;1H\033[K  %s", label);
    fflush(stdout);
    char line[64];
    int ok = 0;
    if (fgets(line, sizeof line, stdin)) { *out = (uint32_t)strtoul(line, NULL, 0); ok = 1; }
    raw_on();
    return ok;
}

static void run_tui(CPU *cpu, Memory *mem, const char *path) {
    tcgetattr(STDIN_FILENO, &orig_tio);
    cpu_capture = 1; cpu_capture_reset();
    raw_on();
    for (;;) {
        render_tui(cpu, mem);
        char ch;
        if (read(STDIN_FILENO, &ch, 1) <= 0) break;
        if (ch == 'q') break;
        else if (ch == 's' || ch == ' ' || ch == '\r' || ch == '\n') do_step(cpu, 1);
        else if (ch == 'c') do_continue(cpu);
        else if (ch == 'f') { cpu_run(cpu, 0); snprintf(msg, sizeof msg, "ran to halt"); }
        else if (ch == 'r') { load(mem, path); cpu_init(cpu, mem); cpu_capture_reset(); snprintf(msg, sizeof msg, "reset"); }
        else if (ch == 'b') { uint32_t a; if (tui_prompt("breakpoint addr: ", &a)) { if (is_bp(a)) del_bp(a); else add_bp(a); } }
        else if (ch == 'm') { uint32_t a; if (tui_prompt("memory addr: ", &a)) watch = a; }
    }
    raw_off();
    cpu_capture = 0;
}

/* ---------- line REPL (non-tty: scripts/tests) ---------- */
static void show_plain(CPU *cpu, Memory *mem) {
    char text[64];
    printf("\n");
    for (int i = 0; i < NUM_REGS; i++) printf("R%d=%-10u%s", i, cpu->regs[i], (i % 4 == 3) ? "\n" : " ");
    printf("PC=0x%04x  SP=0x%05x  Z%d C%d O%d N%d%s\n", cpu->pc, cpu->sp,
           cpu->flags.z, cpu->flags.c, cpu->flags.o, cpu->flags.n, cpu->halted ? "  [HALTED]" : "");
    printf("--- disasm ---\n");
    uint32_t start = cpu->pc >= 20 ? cpu->pc - 20 : 0;
    for (int i = 0; i < 12; i++) {
        uint32_t a = start + (uint32_t)i * 4;
        if (a + 4 > prog_size) break;
        isa_disasm(text, sizeof text, mem_read32(mem, a));
        printf("%c %04x  %s\n", a == cpu->pc ? '>' : (is_bp(a) ? '*' : ' '), a, text);
    }
    printf("[%s]\n", msg);
}

static void repl(CPU *cpu, Memory *mem, const char *path) {
    char line[128];
    for (;;) {
        show_plain(cpu, mem);
        printf("(dbg) "); fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        char cmd[16] = {0}, arg[64] = {0};
        int got = sscanf(line, "%15s %63s", cmd, arg);
        if (got <= 0) do_step(cpu, 1);
        else if (!strcmp(cmd, "q")) break;
        else if (!strcmp(cmd, "s")) do_step(cpu, got > 1 ? strtol(arg, NULL, 0) : 1);
        else if (!strcmp(cmd, "c")) do_continue(cpu);
        else if (!strcmp(cmd, "b") && got > 1) add_bp((uint32_t)strtoul(arg, NULL, 0));
        else if (!strcmp(cmd, "d") && got > 1) del_bp((uint32_t)strtoul(arg, NULL, 0));
        else if (!strcmp(cmd, "m") && got > 1) { watch = (uint32_t)strtoul(arg, NULL, 0); snprintf(msg, sizeof msg, "memory -> 0x%x", watch); }
        else if (!strcmp(cmd, "reset")) { load(mem, path); cpu_init(cpu, mem); snprintf(msg, sizeof msg, "reset"); }
        else snprintf(msg, sizeof msg, "unknown '%s'", cmd);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s program.bin\n", argv[0]); return 2; }
    Memory mem;
    if (load(&mem, argv[1]) != 0) return 1;
    CPU cpu;
    cpu_init(&cpu, &mem);

    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) run_tui(&cpu, &mem, argv[1]);
    else repl(&cpu, &mem, argv[1]);
    return 0;
}
