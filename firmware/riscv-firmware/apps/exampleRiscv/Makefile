# Makefile for XuanTie RISC-V exampleRiscv Application (A733: E902, T527: E906)

CROSS_COMPILE ?= /home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
ifeq ($(wildcard $(CROSS_COMPILE)gcc),)
CROSS_COMPILE := riscv-none-elf-
endif

CC      = $(CROSS_COMPILE)gcc
CXX     = $(CROSS_COMPILE)g++
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

# Target Board Selection: a7a (Allwinner A733 / E902) or a5e (Allwinner T527 / E906)
BOARD ?= a5e

ifeq ($(BOARD),a7a)
# Allwinner A733: XuanTie E902 (RV32E 16 registers, soft-float, 2-stage, up to 200 MHz)
ARCH_FLAGS = -march=rv32emc_zicsr -mabi=ilp32e -mcmodel=medany
else
# Allwinner T527: XuanTie E906 (RV32IMAFCX, single-float, up to 200 MHz)
ARCH_FLAGS = -march=rv32imac_zicsr_zifencei_zihintpause -mabi=ilp32 -mcmodel=medany
endif

LDSCRIPT  ?= ../../../common/arch_riscv/e906_sram.ld

OPT_FLAGS  = -O2 -g

INCLUDES = -I. -I../../../common

COMMON_FLAGS = $(ARCH_FLAGS) $(OPT_FLAGS) $(INCLUDES) -Wall -ffreestanding -ffunction-sections -fdata-sections -flto
CFLAGS       = $(COMMON_FLAGS)
CXXFLAGS     = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics

LDFLAGS      = $(ARCH_FLAGS) -T $(LDSCRIPT) -Wl,-Map=firmware.map -Wl,--gc-sections -flto -nostartfiles

SRCS_S   = startup.S
SRCS_C   = resource_table.c
SRCS_CPP = main.cpp

OBJS = $(SRCS_S:.S=.o) $(SRCS_C:.c=.o) $(SRCS_CPP:.cpp=.o) timer.o

TARGET = exampleRiscv
ELF    = $(TARGET).elf
BIN    = $(TARGET).bin
MAP    = $(TARGET).map

all: $(ELF) $(BIN)

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

timer.o: ../../../common/hal/timer.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -Wl,-Map=$(MAP) -o $@
	@echo "--- Memory Footprint ($@) ---"
	$(SIZE) $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJS) $(ELF) $(BIN) $(MAP) firmware.elf firmware.bin firmware.map

.PHONY: all clean

