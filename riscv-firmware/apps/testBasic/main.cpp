/*
 * main.cpp - testBasic: Simple Boot & SRAM Memory Probe Test
 *
 * Target: Allwinner T527 / A523 XuanTie E907
 * Memory: Shared PubSRAM C (0x00020000, 128 KB)
 *
 * Demonstrates:
 * 1. Booting directly from PubSRAM C (0x00020000).
 * 2. Writes signature magic words upon entry:
 *    - sram_c_loc1: Magic 0xDEADBEEF + Counter
 *    - sram_c_loc2: Magic 0x52495343 ("RISC") + Counter
 *    - dtcm_scratch: Magic 0xCAFE1234 + Counter
 * 3. In-memory RemoteProc trace0 ring buffer logging.
 * 4. Continuous incrementing loop for devmem verification from Linux.
 */

#include <stdint.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"

// Test Memory Locations mapped explicitly by Linker Script (firmware_t527.ld)
__attribute__((used, section(".sram_c_loc1"), aligned(4)))
static volatile uint32_t sram_c_loc1[2];

__attribute__((used, section(".sram_c_loc2"), aligned(4)))
static volatile uint32_t sram_c_loc2[2];

__attribute__((used, section(".dtcm_scratch"), aligned(4)))
static volatile uint32_t dtcm_scratch[2];

int main(void) {
    // 1. Write Initial Magic Signatures immediately upon entry
    sram_c_loc1[0]  = 0xDEADBEEF;
    sram_c_loc1[1]  = 0;

    sram_c_loc2[0]  = 0x52495343; // "RISC"
    sram_c_loc2[1]  = 0;

    dtcm_scratch[0] = 0xCAFE1234;
    dtcm_scratch[1] = 0;

    // 2. Initialize In-Memory HAL Trace (disable unmapped S_UART0 mirror) and Timer
    hal::Trace::init(false);
    hal::Timer::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testBasic App                     \n");
    hal::Trace::printf("  SRAM C Window : %p, %p\n", (void *)sram_c_loc1, (void *)sram_c_loc2);
    hal::Trace::printf("  DTCM Scratch  : %p\n", (void *)dtcm_scratch);
    hal::Trace::puts("================================================================\n");

    uint32_t count = 0;

    // 3. Periodic Increment Loop
    while (1) {
        count++;

        sram_c_loc1[1]  = count;
        sram_c_loc2[1]  = count;
        dtcm_scratch[1] = count;

        hal::Trace::printf("[testBasic] Loop #%u | SRAM C %p = 0x%08x | DTCM %p = 0x%08x\n",
                           count, (void *)&sram_c_loc1[1], sram_c_loc1[1],
                           (void *)&dtcm_scratch[1], dtcm_scratch[1]);

        hal::Timer::delay_ms(500);
    }

    return 0;
}
