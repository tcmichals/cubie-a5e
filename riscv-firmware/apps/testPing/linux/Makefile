# Makefile for Linux Host testPing benchmarks (ping_shm and ping_uio)

CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -pthread
INCLUDES = -I../../../common -I../../../common/include

TARGETS = ping_shm ping_uio

all: $(TARGETS)

ping_shm: ping_shm.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ -lm

ping_uio: ping_uio.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ -lm

clean:
	rm -f $(TARGETS)

.PHONY: all clean
