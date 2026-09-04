/*
 * main.cpp - testBasic: Simple Boot & SRAM Memory Probe Test
 *
 * Target: Allwinner T527 XuanTie E907 (RV32IMAFDC @ 600 MHz)
 *
 * Demonstrates:
 * 1. Booting from ITCM (0x00000000) with DTCM stack (0x00080000).
 * 2. Writes signature magic words to multiple SRAM windows:
 *    - Shared SRAM A2 (0x00040000): Magic 0xDEADBEEF + Counter
 *    - Shared SRAM A2 (0x00050000): Magic 0x52495343 ("RISC") + Heartbeat
 *    - DTCM Scratchpad (0x00081000): Magic 0xCAFE1234
 *    - System SRAM C (0x07130000): Magic 0xAA55AA55
 * 3. Continuous incrementing loop with HAL Timer for devmem verification from Linux.
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
    // 1. Initialize HAL Trace and Timer
    hal::Trace::init();
    hal::Timer::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testBasic App                     \n");
    hal::Trace::printf("  SRAM C Window : %p, %p\n", (void *)sram_c_loc1, (void *)sram_c_loc2);
    hal::Trace::printf("  DTCM Scratch  : %p\n", (void *)dtcm_scratch);
    hal::Trace::puts("================================================================\n");

    // 2. Write Initial Magic Signatures
    sram_c_loc1[0]  = 0xDEADBEEF;
    sram_c_loc1[1]  = 0;

    sram_c_loc2[0]  = 0x52495343; // "RISC"
    sram_c_loc2[1]  = 0;

    dtcm_scratch[0] = 0xCAFE1234;
    dtcm_scratch[1] = 0;

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
