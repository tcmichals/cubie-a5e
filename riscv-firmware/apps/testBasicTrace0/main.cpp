/*
 * main.cpp - testBasicTrace0: RemoteProc Trace0 Banner & 1-Second Periodic Heartbeat
 *
 * Target: Allwinner T527 XuanTie E907 (RV32IMAFDC @ 600 MHz)
 *
 * Demonstrates:
 * 1. RemoteProc Resource Table initialization with trace0 buffer.
 * 2. Emitting an ASCII startup banner to /sys/kernel/debug/remoteproc/remoteproc0/trace0 and S_UART0.
 * 3. 1-second periodic timestamped heartbeat updates using hardware cycle counters.
 * 4. Mirrored heartbeat writes to shared SRAM A2 (0x00040000).
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"

#define SRAM_HEARTBEAT_LOC   ((volatile uint32_t *)0x00040000UL)

int main(void) {
    // 1. Initialize HAL subsystems
    hal::Trace::init(/*enable_serial_mirror=*/true);
    hal::Timer::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 RemoteProc Trace0 Test            \n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testBasicTrace0                  \n");
    hal::Trace::puts("  Core Clock  : Up to 200 MHz                                   \n");
    hal::Trace::puts("  Target Buffer: /sys/kernel/debug/remoteproc/remoteproc0/trace0\n");
    hal::Trace::puts("================================================================\n");

    uint32_t uptime_sec = 0;
    SRAM_HEARTBEAT_LOC[0] = 0x54524143; // "TRAC"
    SRAM_HEARTBEAT_LOC[1] = 0;

    // 2. Periodic 1-second update loop
    while (1) {
        uptime_sec++;
        SRAM_HEARTBEAT_LOC[1] = uptime_sec;

        hal::Trace::printf("[E907 Trace0] Heartbeat #%u | Uptime: %us | Status: OK | SRAM: 0x%08x\n",
                           uptime_sec, uptime_sec, SRAM_HEARTBEAT_LOC[1]);

        hal::Timer::delay_ms(1000);
    }

    return 0;
}
