# Makefile for testStringBinaryTrace0 (Allwinner T527 XuanTie E906)

# Target Core Selection
ifeq ($(CORE),e902)
  ARCH_FLAGS ?= -march=rv32emc_zicsr_zifencei -mabi=ilp32e -mcmodel=medany
else
  ARCH_FLAGS ?= -march=rv32imafc_zicsr_zifencei_zihintpause -mabi=ilp32 -mcmodel=medany
endif

APP_SRCS_CPP = main.cpp

include ../../../common/common.mk

