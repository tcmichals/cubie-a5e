/*
 * main.cpp - testCrash: Hardware Exception / Crash Reporting via RemoteProc Trace0
 *
 * Target: Allwinner T527 XuanTie E907 (RV32IMAFDC @ 600 MHz)
 *
 * Demonstrates:
 * 1. Global mtvec exception trap vector capturing CPU fault context into hal::CrashFrame.
 * 2. Emitting regular countdown heartbeats to trace0 before triggering an intentional fault.
 * 3. Deliberately triggering a hardware exception (Illegal Instruction / Fault).
 * 4. Formatting and outputting an exhaustive crash autopsy (mepc, mcause, mtval,
 *    and registers x1..x31) to /sys/kernel/debug/remoteproc/remoteproc0/trace0
 *    and S_UART0, and writing fatal signature 0xDEADF00D to Shared SRAM A2 (0x00040000).
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"
#include "hal/crash.hpp"

#define SRAM_HEARTBEAT_LOC ((volatile uint32_t *)0x07130000UL)

int main(void) {
    // 1. Initialize HAL
    hal::Trace::init(/*enable_serial_mirror=*/true);
    hal::Timer::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 Crash Reporting Test              \n");
    hal::Trace::puts("  Goal: Run 3 countdown heartbeats then trigger intentional trap \n");
    hal::Trace::puts("  Trace: /sys/kernel/debug/remoteproc/remoteproc0/trace0        \n");
    hal::Trace::puts("================================================================\n");

    SRAM_HEARTBEAT_LOC[0] = 0x54455354; // "TEST"
    SRAM_HEARTBEAT_LOC[1] = 0;

    // 2. Emit 3 normal countdown heartbeats
    for (uint32_t i = 1; i <= 3; i++) {
        SRAM_HEARTBEAT_LOC[1] = i;

        hal::Trace::printf("[testCrash] Normal Heartbeat #%u / 3 (countdown to intentional fault)\n", i);

        hal::Timer::delay_ms(1000);
    }

    hal::Trace::puts("[testCrash] >>> Triggering intentional Illegal Instruction fault NOW <<<\n");
    hal::Timer::delay_ms(100);

    // 3. Intentionally trigger illegal instruction exception (unimp / 0x00000000)
    asm volatile(".word 0x00000000");

    // Execution will never reach here; default_trap_entry routes to hal::CrashHandler::handle
    hal::Trace::puts("[testCrash] ERROR: Exception failed to trap!\n");
    while (1) {
        hal::Timer::delay_ms(1000);
    }

    return 0;
}
