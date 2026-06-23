# Makefile — custom ISA CPU emulator & assembler

CC      := cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Iinclude -g
BUILD   := build

CORE_SRC := src/memory.c src/cpu.c src/isa.c src/device.c
ASM_SRC  := src/lexer.c src/parser.c src/encoder.c src/isa.c
CC_SRC   := src/clexer.c src/cparser.c src/cgen.c

.PHONY: all test clean

all: $(BUILD)/emu $(BUILD)/asm $(BUILD)/disasm $(BUILD)/pipe $(BUILD)/dbg $(BUILD)/mcc

$(BUILD)/emu: src/main_emu.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/dbg: src/main_dbg.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/mcc: src/main_cc.c $(CC_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/asm: src/main_asm.c $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/disasm: src/disasm.c src/isa.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/pipe: src/main_pipe.c src/pipe_sim.c src/bpred.c src/cache.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_cpu_core $(BUILD)/test_assembler $(BUILD)/test_integration \
      $(BUILD)/test_pipe $(BUILD)/test_compiler \
      $(BUILD)/emu $(BUILD)/asm $(BUILD)/disasm $(BUILD)/dbg $(BUILD)/mcc
	./$(BUILD)/test_cpu_core
	./$(BUILD)/test_assembler
	./$(BUILD)/test_integration
	./$(BUILD)/test_pipe
	./$(BUILD)/test_compiler
	sh tests/roundtrip.sh
	sh tests/dbg_test.sh
	sh tests/cc_test.sh
	sh tests/cc_difftest.sh

$(BUILD)/test_cpu_core: tests/test_cpu_core.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_assembler: tests/test_assembler.c $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_integration: tests/test_integration.c $(CORE_SRC) $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_integration.c src/memory.c src/cpu.c src/isa.c src/device.c src/lexer.c src/parser.c src/encoder.c -o $@

$(BUILD)/test_pipe: tests/test_pipe.c src/pipe_sim.c src/bpred.c src/cache.c $(CORE_SRC) $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_pipe.c src/pipe_sim.c src/bpred.c src/cache.c src/memory.c src/cpu.c src/isa.c src/device.c src/lexer.c src/parser.c src/encoder.c -o $@

$(BUILD)/test_compiler: tests/test_compiler.c $(CC_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD)
