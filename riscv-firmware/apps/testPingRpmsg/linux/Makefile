# Makefile for Linux Host ping_rpmsg benchmark

CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -pthread
TARGET = ping_rpmsg

SRCS = ping_rpmsg.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -I../../../common -I../../../common/include $< -o $@ -lm

clean:
	rm -f $(TARGET)

.PHONY: all clean
