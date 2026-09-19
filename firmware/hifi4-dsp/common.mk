# ==============================================================================
# Common Makefile Fragment for Cadence Tensilica HiFi4 Audio DSP (Allwinner T527)
# ==============================================================================

COMMON_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../common)

# 1. Toolchain Configuration
# Supports Cadence Xtensa HiFi4 GCC toolchain or cross-compiler fallback
ifeq ($(CROSS_COMPILE),)
  ifneq ($(wildcard /home/tcmichals/.tools/xtensa-hifi4-gcc/bin/xtensa-hifi4-elf-gcc),)
    CROSS_COMPILE = /home/tcmichals/.tools/xtensa-hifi4-gcc/bin/xtensa-hifi4-elf-
  else ifneq ($(shell which xtensa-hifi4-elf-gcc 2>/dev/null),)
    CROSS_COMPILE = xtensa-hifi4-elf-
  else ifneq ($(wildcard /home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc),)
    CROSS_COMPILE = /home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
  else
    CROSS_COMPILE = xtensa-hifi4-elf-
  endif
endif

CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

# 2. Compilation Flags
INCLUDES += -I. -I$(COMMON_DIR) -I$(COMMON_DIR)/include -I$(COMMON_DIR)/hal
CFLAGS   += -O2 -g $(INCLUDES) -Wall -Wextra -ffreestanding -ffunction-sections -fdata-sections
ifeq ($(findstring riscv,$(CROSS_COMPILE)),riscv)
  ARCH_FLAGS ?= -march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany
  CFLAGS     += $(ARCH_FLAGS)
  LDFLAGS    += $(ARCH_FLAGS)
endif
LDSCRIPT ?= $(COMMON_DIR)/arch_dsp/dsp.ld
LDFLAGS  += -T $(LDSCRIPT) -Wl,-Map=$(TARGET).map -Wl,--gc-sections -nostdlib -lgcc

# 3. Source Files (Unified with RISC-V co-processor)
COMMON_SRCS ?= $(COMMON_DIR)/resource_table.c $(COMMON_DIR)/hal/sunxi_msgbox.c
SRCS        += $(COMMON_SRCS)
OBJS        += $(patsubst %.c, build/%.o, $(notdir $(SRCS)))

vpath %.c $(dir $(SRCS))


# 4. Build Targets
all: build/$(TARGET).elf build/$(TARGET).bin

build:
	mkdir -p build

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "--- Memory Layout ---"
	$(SIZE) $@

build/$(TARGET).bin: build/$(TARGET).elf
	$(OBJCOPY) -O binary -S $< $@

clean:
	rm -rf build

.PHONY: all clean
