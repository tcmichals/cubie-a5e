cmake_minimum_required(VERSION 3.20)
project(ioprocessor_firmware C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Default to Debug build type if not explicitly set
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug, Release, RelWithDebInfo)" FORCE)
endif()

message(STATUS "Build Configuration: ${CMAKE_BUILD_TYPE}")

# Architecture & Target Flags for XuanTie E907
set(ARCH_FLAGS "-march=rv32imac -mabi=ilp32 -mcmodel=medlow")
set(WARN_FLAGS "-Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -fno-common")

# Debug flags: -Og preserves coroutine readability and registers while producing full DWARF -g3 -ggdb symbols
set(CMAKE_C_FLAGS_DEBUG          "-Og -g3 -ggdb -DDEBUG=1 -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG        "-Og -g3 -ggdb -DDEBUG=1 -fno-omit-frame-pointer")
set(CMAKE_ASM_FLAGS_DEBUG        "-g3 -ggdb")

# Release flags: Max performance optimization
set(CMAKE_C_FLAGS_RELEASE        "-O3 -DNDEBUG=1 -flto")
set(CMAKE_CXX_FLAGS_RELEASE      "-O3 -DNDEBUG=1 -flto")

# RelWithDebInfo: Production optimization with debug symbols
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3 -DNDEBUG=1")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g3 -DNDEBUG=1")

set(CMAKE_C_FLAGS   "${ARCH_FLAGS} ${WARN_FLAGS}")
set(CMAKE_CXX_FLAGS "${ARCH_FLAGS} ${WARN_FLAGS} -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics")
set(CMAKE_ASM_FLAGS "${ARCH_FLAGS} -x assembler-with-cpp")

# Linker Script & Flags
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/ioprocessor.ld")
set(CMAKE_EXE_LINKER_FLAGS "${ARCH_FLAGS} -T${LINKER_SCRIPT} -nostartfiles -Wl,--gc-sections -Wl,-Map=${CMAKE_PROJECT_NAME}.map")

# Include Directories
include_directories(
    include
    src
    ../../../common/include
    ../../../common/hal
    ../../../common/ipc
    ../../../common/third_party/AbstractX/include
    ../../../common/third_party/etl/include
    ../../../common/third_party/pigweed
    ../../../common/third_party/barectf
)

# Source Files
set(SRCS
    ../../../common/arch_riscv/startup.S
    ../../../common/arch_riscv/resource_table.c
    src/main.cpp
    ../../../common/hal/ccu.cpp
    ../../../common/hal/pio.cpp
    ../../../common/hal/spi.cpp
    ../../../common/hal/uart.cpp
    ../../../common/hal/msgbox.cpp
    src/coroutines/io_tasks.cpp
    ../../../common/third_party/barectf/barectf.c
)

add_executable(${CMAKE_PROJECT_NAME}.elf ${SRCS})

# Generate .bin file and print size statistics
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_PROJECT_NAME}.elf ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${CMAKE_PROJECT_NAME}.elf
    COMMENT "Building raw binary and analyzing section sizes"
)
