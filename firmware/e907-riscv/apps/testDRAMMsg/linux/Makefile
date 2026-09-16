# Makefile for Linux Host ping_dram benchmark

CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -pthread
TARGET = ping_dram

SRCS = ping_dram.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -I../../../common -I../../../common/include $< -o $@ -lm

clean:
	rm -f $(TARGET)

.PHONY: all clean
