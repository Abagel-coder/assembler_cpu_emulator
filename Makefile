# Makefile — custom ISA CPU emulator & assembler

CC      := cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Iinclude -g
BUILD   := build

CORE_SRC := src/memory.c src/cpu.c src/isa.c
ASM_SRC  := src/lexer.c src/parser.c src/encoder.c src/isa.c

.PHONY: all test clean

all: $(BUILD)/emu $(BUILD)/asm $(BUILD)/disasm $(BUILD)/pipe

$(BUILD)/emu: src/main_emu.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/asm: src/main_asm.c $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/disasm: src/disasm.c src/isa.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/pipe: src/main_pipe.c src/pipe_sim.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_cpu_core $(BUILD)/test_assembler $(BUILD)/test_integration \
      $(BUILD)/test_pipe $(BUILD)/asm $(BUILD)/disasm
	./$(BUILD)/test_cpu_core
	./$(BUILD)/test_assembler
	./$(BUILD)/test_integration
	./$(BUILD)/test_pipe
	sh tests/roundtrip.sh

$(BUILD)/test_cpu_core: tests/test_cpu_core.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_assembler: tests/test_assembler.c $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_integration: tests/test_integration.c $(CORE_SRC) $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_integration.c src/memory.c src/cpu.c src/lexer.c src/parser.c src/encoder.c src/isa.c -o $@

$(BUILD)/test_pipe: tests/test_pipe.c src/pipe_sim.c $(CORE_SRC) $(ASM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_pipe.c src/pipe_sim.c src/memory.c src/cpu.c src/lexer.c src/parser.c src/encoder.c src/isa.c -o $@

clean:
	rm -rf $(BUILD)
