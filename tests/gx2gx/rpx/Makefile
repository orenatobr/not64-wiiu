#---------------------------------------------------------------------------------
# Tiny RPX that links the GX2 renderer for Wii U (compile + link proof only; never run).
#   docker run --rm -v "$PWD":/project not64-wiiu-builder make -C tests/gx2gx/rpx
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPRO")
endif

include $(DEVKITPRO)/wut/share/wut_rules

ROOT		:=	../../..
GX2GX		:=	$(ROOT)/wiiu/source/gx2gx
BUILD		:=	build
TARGET		:=	gx2gx_test

SRCS		:=	main.cpp $(GX2GX)/Gx2Core.cpp $(GX2GX)/Gx2Tev.cpp $(GX2GX)/Gx2Tex.cpp $(GX2GX)/Gx2Math.cpp $(GX2GX)/Gx2Wiiu.cpp
OBJS		:=	$(addprefix $(BUILD)/,$(notdir $(SRCS:.cpp=.o)))

CXXFLAGS	:=	-g -O2 -Wall -Wextra -Wno-unused-parameter $(MACHDEP) -std=c++17 -D__WIIU__ -D__WUT__ \
			-I$(GX2GX) -I$(GX2GX)/compat -I$(WUT_ROOT)/include
LDFLAGS		:=	-g $(MACHDEP) $(RPXSPECS)
LIBPATHS	:=	-L$(WUT_ROOT)/lib
LIBS		:=	-lwut

vpath %.cpp . $(GX2GX)

.PHONY: all clean
all: $(TARGET).rpx

$(TARGET).rpx: $(TARGET).elf
$(TARGET).elf: $(OBJS)
	@echo linking $@
	$(CXX) $(LDFLAGS) $(OBJS) $(LIBPATHS) $(LIBS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	@echo $<
	$(CXX) -MMD -MP $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).rpx

-include $(OBJS:.o=.d)
