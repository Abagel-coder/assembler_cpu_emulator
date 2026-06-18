# Makefile — custom ISA CPU emulator & assembler

CC      := cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Iinclude -g
BUILD   := build

CORE_SRC := src/memory.c src/cpu.c src/isa.c

.PHONY: all test clean

all: $(BUILD)/emu

$(BUILD)/emu: src/main_emu.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_cpu_core
	./$(BUILD)/test_cpu_core

$(BUILD)/test_cpu_core: tests/test_cpu_core.c $(CORE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD)
