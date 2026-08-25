cmake_minimum_required(VERSION 3.20)
project(io-bridge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

include_directories(
    include
    src
    ../../../common/include
    ../../../common/linux
    ../../../common/third_party/pigweed
)

set(SRCS
    src/main.cpp
    ../../../common/linux/ipc_bridge.cpp
    src/log_detokenizer.cpp
    src/trace_writer.cpp
)

add_executable(io-bridge ${SRCS})
