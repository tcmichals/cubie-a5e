cmake_minimum_required(VERSION 3.20)
project(ioprocessor_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O0 -g -pthread -DCPPUTEST_MEM_LEAK_DETECTION_DISABLED=1")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/hal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ipc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/third_party/AbstractX/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/third_party/etl/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/src/coroutines
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/src/logging
    /usr/local/include
)

set(TEST_SRCS
    test_main.cpp
    mock_hal.cpp
    test_ringbuffer.cpp
    test_pcie_tlp.cpp
    test_coroutine_scheduler.cpp
)

add_executable(ioprocessor_tests ${TEST_SRCS})

# Link CppUTest archive
target_link_libraries(ioprocessor_tests PRIVATE
    /usr/local/lib/libCppUTest.a
    /usr/local/lib/libCppUTestExt.a
)

enable_testing()
add_test(NAME AllUnitTests COMMAND ioprocessor_tests)
