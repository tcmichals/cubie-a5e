# Makefile for Linux Host ping_rpmsg benchmark

CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -pthread
TARGETS = ping_rpmsg stress_rpmsg

all: $(TARGETS)

%: %.cpp
	$(CXX) $(CXXFLAGS) -I../../../../common -I../../../../common/include $< -o $@ -lm

clean:
	rm -f $(TARGETS)

.PHONY: all clean
