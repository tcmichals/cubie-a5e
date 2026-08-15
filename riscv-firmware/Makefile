# Makefile for XuanTie RISC-V E906/E907 co-processor firmware

# Toolchain Auto-detection & Configuration
ifeq ($(origin TOOLCHAIN_PREFIX), undefined)
  ifneq ($(CROSS_COMPILE),)
    TOOLCHAIN_PREFIX := $(CROSS_COMPILE)
  else ifneq ($(shell which riscv-none-elf-gcc 2>/dev/null),)
    TOOLCHAIN_PREFIX := riscv-none-elf-
  else
    # Find any working installed xpack toolchain in ~/.tools
    EXISTING_XPACK := $(lastword $(sort $(wildcard $(HOME)/.tools/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-gcc)))
    ifneq ($(EXISTING_XPACK),)
      TOOLCHAIN_PREFIX := $(patsubst %gcc,%,$(EXISTING_XPACK))
      XPACK_DIR := $(patsubst %/bin/riscv-none-elf-gcc,%,$(EXISTING_XPACK))
    else
      XPACK_VER ?= 15.2.0-1
      XPACK_DIR ?= $(HOME)/.tools/xpack-riscv-none-elf-gcc-$(XPACK_VER)
      TOOLCHAIN_PREFIX ?= $(XPACK_DIR)/bin/riscv-none-elf-
    endif
  endif
endif

XPACK_VER ?= 15.2.0-1
XPACK_DIR ?= $(HOME)/.tools/xpack-riscv-none-elf-gcc-$(XPACK_VER)
TOOLCHAIN_CC := $(TOOLCHAIN_PREFIX)gcc

CC      = $(TOOLCHAIN_PREFIX)gcc
CXX     = $(TOOLCHAIN_PREFIX)g++
OBJCOPY = $(TOOLCHAIN_PREFIX)objcopy
OBJDUMP = $(TOOLCHAIN_PREFIX)objdump
SIZE    = $(TOOLCHAIN_PREFIX)size

ARCH_FLAGS = -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany

# Build Configuration (0 = Release, 1 = Debug)
DEBUG ?= 0

ifeq ($(DEBUG), 1)
  OPT_FLAGS = -Og -g
  LTO_FLAGS = 
else
  OPT_FLAGS = -O2 -g
  LTO_FLAGS = -flto
endif

# Common flags for both C and C++
# -ffunction-sections -fdata-sections: Allow linker to dead-code strip
# -flto: Link Time Optimization for maximum performance/size reduction (Release only)
# --specs=nano.specs: Use newlib-nano for tiny libc footprints
COMMON_FLAGS = $(ARCH_FLAGS) $(OPT_FLAGS) -Wall -ffreestanding -ffunction-sections -fdata-sections $(LTO_FLAGS) --specs=nano.specs

CFLAGS   = $(COMMON_FLAGS)

# C++ specific bloat-removal flags
# -fno-exceptions -fno-rtti: Remove massive C++ standard library exception/type-info overhead
# -fno-use-cxa-atexit -fno-threadsafe-statics: Remove hidden C++ startup/shutdown bloat
CXXFLAGS = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics

# Linker flags
# -Wl,--gc-sections: Dead code elimination
# -nostartfiles: We provide our own startup.S, but we DO link nano libc
LDFLAGS  = -T firmware.ld -Wl,-Map=firmware.map -Wl,--gc-sections $(LTO_FLAGS) --specs=nano.specs -nostartfiles

SRCS = startup.S main.cpp melis_hello_world.c spi.c ringbuffer.c resource_table.c
OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)
OBJS := $(OBJS:.cpp=.o)

.DEFAULT_GOAL := all
.PHONY: all clean size check_toolchain

all: firmware.bin firmware.elf

# Ensure check_toolchain completes before any object is compiled in parallel builds
$(OBJS): | check_toolchain

check_toolchain:
	@if ! command -v "$(TOOLCHAIN_CC)" >/dev/null 2>&1 && [ ! -x "$(TOOLCHAIN_CC)" ]; then \
		set -e; \
		echo "=== RISC-V toolchain not found at $(TOOLCHAIN_CC) ==="; \
		echo "=== Downloading latest xPack RISC-V Toolchain v$(XPACK_VER) ==="; \
		mkdir -p $(HOME)/.tools; \
		TMP_TAR=$$(mktemp /tmp/xpack-riscv-XXXXXX.tar.gz); \
		trap 'rm -f "$$TMP_TAR"' EXIT INT TERM; \
		if ! curl -fSL --retry 3 --retry-delay 2 -o "$$TMP_TAR" \
			https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v$(XPACK_VER)/xpack-riscv-none-elf-gcc-$(XPACK_VER)-linux-x64.tar.gz; then \
			echo "ERROR: Failed to download xPack RISC-V toolchain." >&2; \
			exit 1; \
		fi; \
		echo "=== Extracting toolchain to $(HOME)/.tools/ ==="; \
		if ! tar -xzf "$$TMP_TAR" -C $(HOME)/.tools/; then \
			echo "ERROR: Failed to extract xPack RISC-V toolchain archive." >&2; \
			exit 1; \
		fi; \
		echo "=== xPack RISC-V Toolchain v$(XPACK_VER) successfully installed to $(XPACK_DIR) ==="; \
	fi

firmware.elf: $(OBJS) firmware.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

firmware.bin: firmware.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o firmware.elf firmware.bin firmware.map

size: firmware.elf
	$(SIZE) $<
