# Makefile for XuanTie RISC-V E906/E907 co-processor firmware

TOOLCHAIN_PREFIX = /home/tcmichals/.tools/gcc-riscv-none-eabi/bin/riscv-none-elf-

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

SRCS = startup.S main.cpp spi.c ringbuffer.c resource_table.c
OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)
OBJS := $(OBJS:.cpp=.o)

.PHONY: all clean size

all: firmware.bin firmware.elf

firmware.elf: $(OBJS) firmware.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o firmware.elf firmware.bin firmware.map

size: firmware.elf
	$(SIZE) $<
