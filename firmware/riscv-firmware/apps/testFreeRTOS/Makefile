# Makefile for testFreeRTOS (Allwinner T527 XuanTie E906)

FREERTOS_DIR = ../../third_party/FreeRTOS-Kernel
ifeq ($(wildcard $(FREERTOS_DIR)/tasks.c),)
$(info FreeRTOS-Kernel submodule missing. Initializing git submodules...)
$(shell cd ../../.. && git submodule update --init --recursive)
endif

APP_SRCS_CPP = main.cpp

APP_SRCS_C = \
    $(FREERTOS_DIR)/tasks.c \
    $(FREERTOS_DIR)/list.c \
    $(FREERTOS_DIR)/queue.c \
    $(FREERTOS_DIR)/timers.c \
    $(FREERTOS_DIR)/portable/MemMang/heap_4.c \
    $(FREERTOS_DIR)/portable/GCC/RISC-V/port.c

APP_SRCS_S = \
    $(FREERTOS_DIR)/portable/GCC/RISC-V/portASM.S

INCLUDES += -I. -I$(FREERTOS_DIR)/include \
            -I$(FREERTOS_DIR)/portable/GCC/RISC-V \
            -I$(FREERTOS_DIR)/portable/GCC/RISC-V/chip_specific_extensions/RISCV_MTIME_CLINT_no_extensions

include ../../../common/common.mk
