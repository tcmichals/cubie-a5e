# Makefile for XuanTie E907 RISC-V exampleRiscv Application

CROSS_COMPILE ?= /home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
ifeq ($(wildcard $(CROSS_COMPILE)gcc),)
CROSS_COMPILE := riscv-none-elf-
endif

CC      = $(CROSS_COMPILE)gcc
CXX     = $(CROSS_COMPILE)g++
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

ARCH_FLAGS = -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany
OPT_FLAGS  = -O2 -g

INCLUDES = -I. -I../../common

COMMON_FLAGS = $(ARCH_FLAGS) $(OPT_FLAGS) $(INCLUDES) -Wall -ffreestanding -ffunction-sections -fdata-sections -flto --specs=nano.specs
CFLAGS       = $(COMMON_FLAGS)
CXXFLAGS     = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics

LDFLAGS      = -T firmware.ld -Wl,-Map=firmware.map -Wl,--gc-sections -flto --specs=nano.specs -nostartfiles

SRCS_S   = startup.S
SRCS_C   = resource_table.c
SRCS_CPP = main.cpp ../../common/hal/timer.cpp

OBJS = $(SRCS_S:.S=.o) $(SRCS_C:.c=.o) $(SRCS_CPP:.cpp=.o)

all: firmware.elf firmware.bin

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

firmware.elf: $(OBJS) firmware.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

firmware.bin: firmware.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o ../../common/hal/*.o firmware.elf firmware.bin firmware.map

.PHONY: all clean
