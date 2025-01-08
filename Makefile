
# Makefile to build doom64
.PHONY: wadtool

TARGET_STRING := doom64.elf
TARGET := $(TARGET_STRING)

# Preprocessor definitions
DEFINES := _FINALROM=1 NDEBUG=1 F3DEX_GBI_2=1

SRC_DIRS :=

# Whether to hide commands or not
VERBOSE ?= 1
ifeq ($(VERBOSE),0)
  V := @
endif

# Whether to colorize build messages
COLOR ?= 1

#==============================================================================#
# Target Executable and Sources                                                #
#==============================================================================#
# BUILD_DIR is the location where all build artifacts are placed
BUILD_DIR := build

# Directories containing source files
SRC_DIRS += src

C_FILES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))

# Object files
O_FILES := $(foreach file,$(C_FILES),$(file:.c=.o))

CFLAGS = $(KOS_CFLAGS)
# -DRANGECHECK=1

# tools
PRINT = printf

ifeq ($(COLOR),1)
NO_COL  := \033[0m
RED     := \033[0;31m
GREEN   := \033[0;32m
BLUE    := \033[0;34m
YELLOW  := \033[0;33m
BLINK   := \033[33;5m
endif

# Common build print status function
define print
  @$(PRINT) "$(GREEN)$(1) $(YELLOW)$(2)$(GREEN) -> $(BLUE)$(3)$(NO_COL)\n"
endef

#==============================================================================#
# Main Targets                                                                 #
#==============================================================================#

all: $(TARGET)

buildtarget:
	mkdir -p $(BUILD_DIR)

$(TARGET): wadtool $(O_FILES) | buildtarget
	${KOS_CC} ${KOS_CFLAGS} ${KOS_LDFLAGS} -o ${BUILD_DIR}/$@ ${KOS_START} $(O_FILES) array_fast_copy.o ${KOS_LIBS}

clean:
	$(RM) doom64.cdi doom64.iso header.iso bootfile.bin $(O_FILES) $(BUILD_DIR)/$(TARGET)
	wadtool/clean.sh

wadtool:
	wadtool/build.sh

cdi: #$(TARGET)
	$(RM) doom64.cdi
	mkdcdisc -d selfboot/mus -d selfboot/maps -d selfboot/sfx -d selfboot/tex -f selfboot/warn3.dt -f selfboot/symbols.raw -f selfboot/doom1mn.lmp -f selfboot/doom64monster.pal -f selfboot/doom64nonenemy.pal -f selfboot/pow2.wad -f selfboot/alt.wad -f selfboot/bump.wad -e $(BUILD_DIR)/$(TARGET) -o doom64.cdi -n "Doom 64" -N

dsiso: #$(TARGET)
	$(RM) doom64.iso
	mkdir -p ./tmp
	$(KOS_OBJCOPY) -R .stack -O binary $(BUILD_DIR)/$(TARGET) ./tmp/1ST_READ.BIN
	-cp -R selfboot/* tmp
	mkisofs -V "Doom 64" -G ip.bin -r -J -l -o doom64.iso ./tmp
	$(RM) ./tmp/1ST_READ.BIN
	$(RM) ./tmp/warn3.dt
	$(RM) ./tmp/doom1mn.lmp
	$(RM) ./tmp/symbols.raw
	$(RM) ./tmp/*.wad
	$(RM) ./tmp/*.pal
	$(RM) ./tmp/mus/*
	$(RM) ./tmp/sfx/*
	$(RM) ./tmp/maps/*
	$(RM) ./tmp/tex/*
	rmdir ./tmp/mus
	rmdir ./tmp/sfx
	rmdir ./tmp/maps
	rmdir ./tmp/tex

dcload: $(TARGET)
	sudo ./dcload-ip/host-src/tool/dc-tool-ip -x $(BUILD_DIR)/$(TARGET) -c ./selfboot/

ALL_DIRS := $(BUILD_DIR) $(addprefix $(BUILD_DIR)/,$(SRC_DIRS))

print-% : ; $(info $* is a $(flavor $*) variable set to [$($*)]) @true

include ${KOS_BASE}/Makefile.rules
