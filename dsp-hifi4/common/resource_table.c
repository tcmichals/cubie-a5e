/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Cadence Tensilica HiFi4 Audio DSP Resource Table
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 *
 * Exports a standard Remoteproc Resource Table with a trace0 buffer
 * parsed by Linux remoteproc at /sys/kernel/debug/remoteproc/remoteprocX/trace0.
 */

#include "resource_table.h"
#include <string.h>

/* Dedicated trace buffer visible to host Linux debugfs */
__attribute__((section(".trace_buffer"), aligned(64)))
char g_dsp_trace_buffer[TRACE_BUFFER_SIZE];

static uint32_t s_trace_head = 0;

/*
 * Standard Remoteproc Resource Table with RSC_TRACE.
 * Pointed to by the .resource_table ELF section header.
 */
__attribute__((section(".resource_table"), used))
const struct basic_resource_table global_dsp_resource_table = {
    .base = {
        .ver = 1,
        .num = 1,
        .reserved = {0, 0},
        .offset = {
            offsetof(struct basic_resource_table, trace),
        },
    },
    .trace = {
        .type = RSC_TRACE,
        .da = (uint32_t)&g_dsp_trace_buffer[0],
        .len = TRACE_BUFFER_SIZE,
        .reserved = 0,
        .name = "trace0",
    },
};

void dsp_trace_init(void)
{
    s_trace_head = 0;
    memset(g_dsp_trace_buffer, 0, sizeof(g_dsp_trace_buffer));
}

void dsp_trace_puts(const char *str)
{
    if (!str)
        return;

    while (*str && s_trace_head < (TRACE_BUFFER_SIZE - 1)) {
        g_dsp_trace_buffer[s_trace_head++] = *str++;
    }
    g_dsp_trace_buffer[s_trace_head] = '\0';
}
