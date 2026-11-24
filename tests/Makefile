# Host-side unit tests (no Wii U toolchain needed): plain gcc/clang on the build machine.
# not CC: the Wii U build exports the cross compiler in CC
HOSTCC ?= gcc
# SAN=1 adds the address and undefined behaviour sanitizers (some CI runners cannot start them)
SAN_ON := -fsanitize=address,undefined -fno-omit-frame-pointer
SANFLAGS := $(if $(SAN),$(SAN_ON))
CFLAGS = -O1 -g -Wall -Wextra -Wno-unused-parameter $(SANFLAGS)
# the original core is old C: build the files from it that the tests use leniently
CORE_CFLAGS = -std=gnu17 -fgnu89-inline -w $(SANFLAGS) -O1 -g -DHW_RVL -D__WIIU__ \
		-I../wiiu/source/core/compat
BUILD := build

GLUE := ../wiiu/source/core

TESTS := test_heap test_save test_rom test_gxcast

.PHONY: all clean
all: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $(TESTS); do timeout 120 ./$(BUILD)/$$t; done

$(BUILD)/test_heap: test_heap.c $(GLUE)/wiiu_heap.c $(GLUE)/compat/ogc/lwp_heap.h
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) -o $@ test_heap.c $(GLUE)/wiiu_heap.c

$(BUILD)/test_save: test_save.c $(GLUE)/save_wiiu.c $(GLUE)/save_wiiu.h
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CORE_CFLAGS) -o $@ test_save.c $(GLUE)/save_wiiu.c

$(BUILD)/test_rom: test_rom.c $(GLUE)/save_wiiu.c $(GLUE)/ROM-Cache-WiiU.c ../main/rom_gc.c
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CORE_CFLAGS) -o $@ test_rom.c $(GLUE)/save_wiiu.c $(GLUE)/ROM-Cache-WiiU.c ../main/rom_gc.c

$(BUILD)/test_gxcast: test_gxcast.c ../glN64_GX/GXcast.h
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) -o $@ test_gxcast.c -lm

clean:
	rm -rf $(BUILD)
