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

// SRAM Test Memory Locations (Dedicated MCU Domain)
#define SRAM_C_LOC1    ((volatile uint32_t *)0x07130000UL)
#define SRAM_C_LOC2    ((volatile uint32_t *)0x07131000UL)
#define DTCM_SCRATCH   ((volatile uint32_t *)0x00081000UL)

int main(void) {
    // 1. Initialize HAL Trace and Timer
    hal::Trace::init();
    hal::Timer::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testBasic App                     \n");
    hal::Trace::puts("  Writing magic signatures to Dedicated MCU SRAM C & DTCM...    \n");
    hal::Trace::puts("================================================================\n");

    // 2. Write Initial Magic Signatures
    SRAM_C_LOC1[0]  = 0xDEADBEEF;
    SRAM_C_LOC1[1]  = 0;

    SRAM_C_LOC2[0]  = 0x52495343; // "RISC"
    SRAM_C_LOC2[1]  = 0;

    DTCM_SCRATCH[0] = 0xCAFE1234;
    DTCM_SCRATCH[1] = 0;

    uint32_t count = 0;

    // 3. Periodic Increment Loop
    while (1) {
        count++;

        SRAM_C_LOC1[1]  = count;
        SRAM_C_LOC2[1]  = count;
        DTCM_SCRATCH[1] = count;

        hal::Trace::printf("[testBasic] Loop #%u | SRAM C 0x07130000 = 0x%08x | DTCM 0x00081000 = 0x%08x\n",
                           count, SRAM_C_LOC1[1], DTCM_SCRATCH[1]);

        hal::Timer::delay_ms(500);
    }

    return 0;
}
