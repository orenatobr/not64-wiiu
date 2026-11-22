# Host-side unit tests (no Wii U toolchain needed): plain gcc/clang on the build machine.
CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer
BUILD := build

TESTS := test_heap

.PHONY: all clean
all: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $(TESTS); do ./$(BUILD)/$$t; done

$(BUILD)/test_heap: test_heap.c ../wiiu/source/core/wiiu_heap.c ../wiiu/source/core/compat/ogc/lwp_heap.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_heap.c ../wiiu/source/core/wiiu_heap.c

clean:
	rm -rf $(BUILD)
