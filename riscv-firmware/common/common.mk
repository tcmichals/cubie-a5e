# ==============================================================================
# Common Makefile Fragment for XuanTie E907 (Allwinner T527) RISC-V Applications
# ==============================================================================

COMMON_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# 1. Automatic Toolchain Detection

ifeq ($(wildcard $(CROSS_COMPILE)gcc),)
CROSS_COMPILE ?= /home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-

endif


CC      = $(CROSS_COMPILE)gcc
CXX     = $(CROSS_COMPILE)g++
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size
GDB     = $(CROSS_COMPILE)gdb

# 2. Target Architecture Flags (Allwinner T527 XuanTie E907)
# RV32IMAFDC: 32 GPRs, Hardware Multiplier, Atomics, Double-Float FPU, Compressed Insts
ARCH_FLAGS ?= -march=rv32imafdc_zicsr_zifencei_zihintpause -mabi=ilp32d -mcmodel=medany
OPT_FLAGS  ?= -Og -g

# 3. Include Directories
INCLUDES += -I. -I$(COMMON_DIR) -I$(COMMON_DIR)/include

# 4. Compiler Flags
COMMON_FLAGS = $(ARCH_FLAGS) $(OPT_FLAGS) $(INCLUDES) $(DEFINES) \
               -Wall -Wextra -ffreestanding -ffunction-sections -fdata-sections

CFLAGS   += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics

# 5. Linker Script & Memory Layout Selection
# Supported configurations (via MEM= or TARGET_MEM=):
#   MEM=sram   (default) -> On-chip SRAM (0x00020000): e907_sram.ld
#   MEM=ddr              -> Multi-bank SRAM & DDR Carveout: e907_ddr.ld
#   MEM=qemu             -> QEMU virt machine (0x80000000): qemu.ld
# Shortcut: 'make QEMU=1' sets MEM=qemu

ifeq ($(QEMU),1)
  MEM ?= qemu
endif

MEM ?= sram

ifeq ($(MEM),sram)
  LDSCRIPT ?= $(COMMON_DIR)/arch_riscv/e907_sram.ld
else ifeq ($(MEM),ddr)
  LDSCRIPT ?= $(COMMON_DIR)/arch_riscv/e907_ddr.ld
else ifeq ($(MEM),qemu)
  LDSCRIPT ?= $(COMMON_DIR)/arch_riscv/qemu.ld
else
  # Custom path specified in LDSCRIPT
  LDSCRIPT ?= $(COMMON_DIR)/arch_riscv/e907_sram.ld
endif

LDFLAGS  ?= $(ARCH_FLAGS) -T $(LDSCRIPT) -Wl,-Map=firmware.map -Wl,--gc-sections -nostartfiles -lm

# 6. Default HAL Sources
COMMON_SRCS_S   ?= $(COMMON_DIR)/arch_riscv/startup.S
COMMON_SRCS_C   ?= $(COMMON_DIR)/arch_riscv/resource_table.c
COMMON_SRCS_CPP ?= $(COMMON_DIR)/hal/trace.cpp \
                   $(COMMON_DIR)/hal/timer.cpp \
                   $(COMMON_DIR)/hal/crash.cpp \
                   $(COMMON_DIR)/hal/pmp.cpp \
                   $(COMMON_DIR)/hal/msgbox.cpp \
                   $(COMMON_DIR)/hal/rpmsg.cpp \
				   $(COMMON_DIR)/arch_riscv/irq_dispatcher.cpp



ALL_SRCS_S   = $(COMMON_SRCS_S) $(APP_SRCS_S)
ALL_SRCS_C   = $(COMMON_SRCS_C) $(APP_SRCS_C)
ALL_SRCS_CPP = $(COMMON_SRCS_CPP) $(APP_SRCS_CPP)

BUILD_DIR ?= build

OBJS = $(patsubst %.S, $(BUILD_DIR)/%.o, $(notdir $(ALL_SRCS_S))) \
       $(patsubst %.c, $(BUILD_DIR)/%.o, $(notdir $(ALL_SRCS_C))) \
       $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(notdir $(ALL_SRCS_CPP)))

VPATH = $(sort $(dir $(ALL_SRCS_S) $(ALL_SRCS_C) $(ALL_SRCS_CPP)))

TARGET ?= $(notdir $(CURDIR))
ELF     = $(TARGET).elf
BIN     = $(TARGET).bin
MAP     = $(TARGET).map

# 7. Build Rules
all: $(BUILD_DIR) $(ELF) $(BIN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -Wl,-Map=$(MAP) -o $@
	@echo "--- Memory Footprint ($@) ---"
	$(SIZE) $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

# 8. Emulation & Debug Targets
QEMU_BIN   ?= qemu-system-riscv32
QEMU_FLAGS ?= -M virt -cpu rv32 -smp 1 -m 128M -nographic -bios none

qemu: $(ELF)
	$(QEMU_BIN) $(QEMU_FLAGS) -kernel $(ELF) -s -S

qemu-run: $(ELF)
	$(QEMU_BIN) $(QEMU_FLAGS) -kernel $(ELF)

gdb: $(ELF)
	$(GDB) -ex "target remote localhost:1234" $(ELF)

clean:
	rm -rf $(BUILD_DIR) $(ELF) $(BIN) $(MAP) firmware.elf firmware.bin firmware.map

.PHONY: all clean qemu qemu-run gdb


