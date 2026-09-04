# ==============================================================================
# Common Makefile Fragment for XuanTie E907 (Allwinner T527) RISC-V Applications
# ==============================================================================

COMMON_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# 1. Automatic Toolchain Detection
CROSS_COMPILE ?= /home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
ifeq ($(wildcard $(CROSS_COMPILE)gcc),)
CROSS_COMPILE := /home/tcmichals/tools/Xilinx/2025.2/gnu/riscv/lin/bin/riscv64-unknown-elf-
ifeq ($(wildcard $(CROSS_COMPILE)gcc),)
CROSS_COMPILE := riscv-none-elf-
endif
endif

CC      = $(CROSS_COMPILE)gcc
CXX     = $(CROSS_COMPILE)g++
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

# 2. Target Architecture Flags (Allwinner T527 XuanTie E907)
# RV32IMAFDC: 32 GPRs, Hardware Multiplier, Atomics, Double-Float FPU, Compressed Insts
ARCH_FLAGS ?= -march=rv32imafdc_zicsr_zifencei -mabi=ilp32d -mcmodel=medany
OPT_FLAGS  ?= -O2 -g

# 3. Include Directories
INCLUDES += -I. -I$(COMMON_DIR) -I$(COMMON_DIR)/include

# 4. Compiler Flags
COMMON_FLAGS = $(ARCH_FLAGS) $(OPT_FLAGS) $(INCLUDES) $(DEFINES) \
               -Wall -Wextra -ffreestanding -ffunction-sections -fdata-sections -flto

CFLAGS   += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics

# 5. Linker Flags & Script
LDSCRIPT ?= $(COMMON_DIR)/arch_riscv/firmware_t527.ld
LDFLAGS  ?= $(ARCH_FLAGS) -T $(LDSCRIPT) -Wl,-Map=firmware.map -Wl,--gc-sections -flto -nostartfiles -lm

# 6. Default HAL Sources
COMMON_SRCS_S   ?= $(COMMON_DIR)/arch_riscv/startup.S
COMMON_SRCS_C   ?= $(COMMON_DIR)/arch_riscv/resource_table.c
COMMON_SRCS_CPP ?= $(COMMON_DIR)/hal/trace.cpp \
                   $(COMMON_DIR)/hal/timer.cpp \
                   $(COMMON_DIR)/hal/crash.cpp \
                   $(COMMON_DIR)/hal/pmp.cpp \
                   $(COMMON_DIR)/hal/rpmsg.cpp



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

clean:
	rm -rf $(BUILD_DIR) $(ELF) $(BIN) $(MAP) firmware.elf firmware.bin firmware.map

.PHONY: all clean


