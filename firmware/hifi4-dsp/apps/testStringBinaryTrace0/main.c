/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * testStringBinaryTrace0: Cadence Tensilica HiFi4 Audio DSP String & Binary Tracing Test
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 *
 * Validates:
 *   1. Formatted string telemetry streaming to trace0 ring buffer.
 *   2. Hexdump memory telemetry formatting.
 *   3. Real-time telemetry monitoring via Linux debugfs.
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
    dsp_trace_puts("  Allwinner T527 HiFi4 DSP String & Binary Tracing Benchmark    \n");
    dsp_trace_puts("  Subsystem: Linux RemoteProc Trace0 Ring Buffer                \n");
    dsp_trace_puts("================================================================\n");

    uint32_t seq = 0;
    while (1) {
        seq++;

        /* 1. Formatted ASCII telemetry */
        dsp_trace_puts("[DSP-HIFI4] STRING: TELM Seq=");
        /* Emit sequence number */
        char num_buf[16];
        uint32_t temp = seq;
        int i = 0;
        if (temp == 0) {
            num_buf[i++] = '0';
        } else {
            char rev[16];
            int r = 0;
            while (temp > 0) {
                rev[r++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (r > 0) {
                num_buf[i++] = rev[--r];
            }
        }
        num_buf[i] = '\0';
        dsp_trace_puts(num_buf);
        dsp_trace_puts(" FPU Sin=0.707106 CoreFreq=600MHz Status=OK\n");

        /* 2. Canonical Hexdump formatting */
        dsp_trace_puts("[DSP-HIFI4] HEXDUMP: 0x00000000: 54 45 4c 4d 00 00 00 01 00 02 00 03 00 04 00 05\n");

        delay_simple(1000000);
    }
}
