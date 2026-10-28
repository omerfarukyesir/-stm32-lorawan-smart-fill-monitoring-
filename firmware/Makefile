# Host-side build for the hardware-independent firmware logic.
#   make test    build and run unit tests
#   make clean   remove build output

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Werror -O2
INC      = -Iinclude
BUILD    = build

.PHONY: all test clean

all: test

$(BUILD)/test_fill_logic: src/fill_logic.c test/test_fill_logic.c include/fill_logic.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INC) src/fill_logic.c test/test_fill_logic.c -o $@

test: $(BUILD)/test_fill_logic
	./$(BUILD)/test_fill_logic

clean:
	rm -rf $(BUILD)
