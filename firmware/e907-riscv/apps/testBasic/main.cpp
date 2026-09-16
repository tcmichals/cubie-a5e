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
    // 1. Read standard RISC-V MISA register (CSR 0x301)
    uint32_t misa = 0;
    asm volatile ("csrr %0, misa" : "=r"(misa));

    // 2. Write MISA and status signatures to SRAM
    sram_c_loc1[0] = 0xDEADBEEF;
    sram_c_loc1[1] = misa;
    sram_c_loc2[0] = 0x52495343; // "RISC"
    sram_c_loc2[1] = 0;

    // 3. Initialize In-Memory HAL Trace ring buffer and Timer
    hal::Trace::init();
    hal::Timer::init();

    uint32_t mstatus = 0;
    asm volatile ("csrr %0, mstatus" : "=r"(mstatus));

    // Try setting 'D' extension in misa to test if it is writable/supported
    asm volatile ("csrs misa, %0" :: "r"(1U << 3));
    uint32_t misa_d_test = 0;
    asm volatile ("csrr %0, misa" : "=r"(misa_d_test));

    hal::Trace::puts("\n================================================================\n");
    hal::Trace::puts("  Allwinner T527 / A527 XuanTie E907 testBasic Hardware Probe   \n");
    hal::Trace::puts("================================================================\n");
    hal::Trace::printf("  MISA Register    : 0x%08x\n", misa);
    hal::Trace::printf("  MSTATUS Register : 0x%08x (FS Status: %u)\n",
                       mstatus, (unsigned)((mstatus >> 13) & 3));
    hal::Trace::printf("  MISA D-Bit Write : 0x%08x (D Support: %s)\n",
                       misa_d_test, (misa_d_test & (1 << 3)) ? "YES (Double FPU Supported)" : "NO (Single FPU Only)");
    hal::Trace::puts("  --------------------------------------------------------------\n");
    hal::Trace::puts("  Decoded Hardware Extensions (from MISA):\n");

    if (misa & (1 << 0))  hal::Trace::puts("    [A] Atomic Instructions (LR/SC, AMO)\n");
    if (misa & (1 << 1))  hal::Trace::puts("    [B] Bit Manipulation Extension\n");
    if (misa & (1 << 2))  hal::Trace::puts("    [C] Compressed 16-bit Instructions (RVC)\n");
    if (misa & (1 << 3))  hal::Trace::puts("    [D] Double-Precision Floating Point (64-bit FPU)\n");
    if (misa & (1 << 4))  hal::Trace::puts("    [E] Embedded 16-register Base ISA\n");
    if (misa & (1 << 5))  hal::Trace::puts("    [F] Single-Precision Floating Point (32-bit FPU)\n");
    if (misa & (1 << 8))  hal::Trace::puts("    [I] Base Integer ISA (32 registers)\n");
    if (misa & (1 << 12)) hal::Trace::puts("    [M] Integer Hardware Multiply/Divide\n");
    if (misa & (1 << 18)) hal::Trace::puts("    [S] Supervisor Mode Supported\n");
    if (misa & (1 << 20)) hal::Trace::puts("    [U] User Mode Supported\n");
    if (misa & (1 << 23)) hal::Trace::puts("    [X] Non-standard / Vendor XuanTie Extensions\n");

    // Test a basic floating point operation
    volatile float f_test1 = 12.5f;
    volatile float f_test2 = 4.0f;
    volatile float f_res = f_test1 * f_test2;
    hal::Trace::puts("  --------------------------------------------------------------\n");
    hal::Trace::printf("  Hardware Float Test: 12.5 * 4.0 = %u (as integer result)\n", (unsigned)f_res);
    hal::Trace::printf("  SRAM Locations     : loc1=%p, loc2=%p, dtcm=%p\n",
                       (void *)sram_c_loc1, (void *)sram_c_loc2, (void *)dtcm_scratch);
    hal::Trace::puts("================================================================\n\n");

    uint32_t count = 0;

    // 4. Periodic Increment Loop
    while (1) {
        count++;

        // Update devmem heartbeat counter
        sram_c_loc2[1] = count;

        hal::Trace::printf("[testBasic] Heartbeat #%u | MISA=0x%08x | count=%u\n",
                           count, misa, count);

        hal::Timer::delay_ms(1000);
    }

    return 0;
}
