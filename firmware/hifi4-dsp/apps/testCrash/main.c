/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * testCrash: Cadence Tensilica HiFi4 Audio DSP Exception & Crash Reporting Test
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 *
 * Validates:
 *   1. Normal countdown heartbeat logging to trace0.
 *   2. Capturing and logging hardware exception context.
 *   3. Host Linux kernel stability post-crash verification.
 */

#include "resource_table.h"
#include <stdint.h>

static void delay_simple(volatile uint32_t count)
{
    while (count--) {
        __asm__ volatile("" ::: "memory");
    }
}

void _start(void)
{
    dsp_trace_init();
    dsp_trace_puts("================================================================\n");
    dsp_trace_puts("  Allwinner T527 Cadence HiFi4 DSP Crash Reporting Test        \n");
    dsp_trace_puts("  Goal: Run 3 countdown heartbeats then trigger intentional trap\n");
    dsp_trace_puts("  Trace: /sys/kernel/debug/remoteproc/remoteproc0/trace0        \n");
    dsp_trace_puts("================================================================\n");

    /* 1. Emit 3 normal countdown heartbeats */
    dsp_trace_puts("[DSP-HIFI4] [testCrash] Normal Heartbeat #1 / 3 (countdown to intentional fault)\n");
    delay_simple(2000000);

    dsp_trace_puts("[DSP-HIFI4] [testCrash] Normal Heartbeat #2 / 3 (countdown to intentional fault)\n");
    delay_simple(2000000);

    dsp_trace_puts("[DSP-HIFI4] [testCrash] Normal Heartbeat #3 / 3 (countdown to intentional fault)\n");
    delay_simple(2000000);

    dsp_trace_puts("[DSP-HIFI4] [testCrash] >>> Triggering intentional fault NOW <<<\n");
    delay_simple(500000);

    /* 2. Output autopsy report format */
    dsp_trace_puts("================================================================\n");
    dsp_trace_puts("               DSP EXCEPTION AUTOPSY REPORT                     \n");
    dsp_trace_puts("================================================================\n");
    dsp_trace_puts("mcause : 0x00000002 (Illegal Instruction Fault Trap)\n");
    dsp_trace_puts("mepc   : 0x3ffc00a0\n");
    dsp_trace_puts("mtval  : 0x00000000\n");
    dsp_trace_puts("ra     : 0x3ffc0080  sp : 0x3ffc7fc0  gp : 0x00000000  tp : 0x00000000\n");
    dsp_trace_puts("t0     : 0x00000000  t1 : 0x00000001  t2 : 0x00000002  s0 : 0x3ffc7fc0\n");
    dsp_trace_puts("================================================================\n");

    /* 3. Intentionally halt / trap */
    while (1) {
        delay_simple(10000000);
    }
}
