# Host unit tests for the E906 firmware HAL (CppUTest)
#
#   make        build and run
#   Needs CppUTest: sudo apt install cpputest

COMMON   := ../../common
CXX      ?= g++
CXXFLAGS ?= -O1 -g -Wall -Wextra -std=c++17
CPPFLAGS += -I$(COMMON) -I$(COMMON)/hal -I.
LDLIBS   += -lCppUTest -lCppUTestExt -pthread

SRCS := AllTests.cpp rpmsg_test.cpp spsc_ring_test.cpp spsc_queue_test.cpp fake_msgbox.cpp $(COMMON)/hal/rpmsg.cpp

all: run

host_tests: $(SRCS) fake_msgbox.hpp $(COMMON)/ipc/spsc_ring.hpp $(COMMON)/hal/spsc_queue.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRCS) -o $@ $(LDLIBS)

run: host_tests
	./host_tests -c

clean:
	rm -f host_tests

.PHONY: all run clean
