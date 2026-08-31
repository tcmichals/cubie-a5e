/*
 * main.cpp - XuanTie E907 RISC-V Example Application
 *
 * Demonstrates:
 * 1. C++ HAL integration (hal::Timer)
 * 2. RemoteProc Resource Table initialization
 * 3. 1-second periodic trace heartbeat updating /sys/kernel/debug/remoteproc0/trace0
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal/timer.hpp"

extern "C" {
    void trace_init(void);
    void trace_puts(const char *s);
}

static void put_uint(uint32_t val) {
    char buf[12];
    int idx = 0;
    if (val == 0) {
        trace_puts("0");
        return;
    }
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; i--) {
        char c[2] = {buf[i], '\0'};
        trace_puts(c);
    }
}

int main(void) {
    // 1. Initialize remoteproc trace buffer
    trace_init();
    trace_puts("================================================================\n");
    trace_puts("  Radxa Cubie A7A - XuanTie E907 RISC-V Co-Processor Ready!     \n");
    trace_puts("  Firmware: exampleRiscv (C++ HAL Engine)                       \n");
    trace_puts("  Trace Buffer: /sys/kernel/debug/remoteproc/remoteproc0/trace0 \n");
    trace_puts("================================================================\n");

    // 2. Initialize hardware timer
    hal::Timer::init();

    uint32_t uptime_sec = 0;

    // 3. Periodic 1-second heartbeat loop
    while (1) {
        hal::Timer::delay_ms(1000);
        uptime_sec++;

        trace_puts("[E907 HAL] Heartbeat #");
        put_uint(uptime_sec);
        trace_puts(" (uptime: ");
        put_uint(uptime_sec);
        trace_puts("s) | Core: E907 @ 600MHz | Status: OK\n");
    }

    return 0;
}
