/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * testBasic: Cadence Tensilica HiFi4 Audio DSP Remoteproc & Trace0 Test
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 *
 * Validates:
 *   1. Remoteproc parsing and loading of the ELF binary at 0x40100000.
 *   2. Core reset release and execution startup.
 *   3. Trace buffer registration and string output at
 *      /sys/kernel/debug/remoteproc/remoteproc0/trace0.
 */

#include "resource_table.h"

void _start(void)
{
    /* Initialize trace output */
    dsp_trace_init();
    dsp_trace_puts("========================================\n");
    dsp_trace_puts("[DSP-HIFI4] testBasic: Firmware Booted!\n");
    dsp_trace_puts("[DSP-HIFI4] Core: Cadence Tensilica HiFi4 @ 600 MHz\n");
    dsp_trace_puts("[DSP-HIFI4] Memory: 0x40100000 (Execution Window)\n");
    dsp_trace_puts("[DSP-HIFI4] Status: RUNNING - Remoteproc Lifecycle OK\n");
    dsp_trace_puts("========================================\n");

    /* Main loop: idle wait */
    volatile uint32_t counter = 0;
    while (1) {
        counter++;
        if ((counter % 1000000) == 0) {
            dsp_trace_puts("[DSP-HIFI4] Heartbeat tick.\n");
        }
    }
}
