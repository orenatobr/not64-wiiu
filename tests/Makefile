# Host-side unit tests (no Wii U toolchain needed): plain gcc/clang on the build machine.
# not CC: the Wii U build exports the cross compiler in CC
HOSTCC ?= gcc
CFLAGS ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer
# the original core is old C: build the files from it that the tests use leniently
CORE_CFLAGS := -std=gnu17 -fgnu89-inline -w -fsanitize=address,undefined -O1 -g -DHW_RVL -D__WIIU__ \
		-I../wiiu/source/core/compat
BUILD := build

GLUE := ../wiiu/source/core

TESTS := test_heap test_save test_rom

.PHONY: all clean
all: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $(TESTS); do ./$(BUILD)/$$t; done

$(BUILD)/test_heap: test_heap.c $(GLUE)/wiiu_heap.c $(GLUE)/compat/ogc/lwp_heap.h
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) -o $@ test_heap.c $(GLUE)/wiiu_heap.c

$(BUILD)/test_save: test_save.c $(GLUE)/save_wiiu.c $(GLUE)/save_wiiu.h
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CORE_CFLAGS) -o $@ test_save.c $(GLUE)/save_wiiu.c

$(BUILD)/test_rom: test_rom.c $(GLUE)/save_wiiu.c $(GLUE)/ROM-Cache-WiiU.c ../main/rom_gc.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CORE_CFLAGS) -o $@ test_rom.c $(GLUE)/save_wiiu.c $(GLUE)/ROM-Cache-WiiU.c ../main/rom_gc.c

clean:
	rm -rf $(BUILD)
