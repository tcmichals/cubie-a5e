/* main.cpp - XuanTie RISC-V E907 Flight Firmware & Live Trace Logging */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "uart0.h"
#include "trace.h"

/* Fixed Telemetry Address in Dedicated RISC-V SRAM for Linux Host devmem inspection */
#define TELEMETRY_BASE      0x00037000   /* Core: 0x00037000 / Host Physical: 0x07147000 */

/* Telemetry Block Structure (24 bytes) */
struct TelemetryBlock {
    volatile uint32_t magic;         /* 0x00: 0x52495343 ("RISC") */
    volatile uint32_t boot_flag;     /* 0x04: 0x00000001 (Booted) */
    volatile uint32_t heartbeat;     /* 0x08: Increments ~10Hz (1, 2, 3...) */
    volatile uint32_t loop_counter;  /* 0x0C: Fast raw loop counter */
    volatile uint32_t status_magic;  /* 0x10: 0x414C4956 ("ALIV") */
    volatile uint32_t last_cmd;      /* 0x14: Last received mailbox command */
};

static TelemetryBlock *telem = (TelemetryBlock *)TELEMETRY_BASE;

int main(void) {
    /* 1. Initialize Telemetry Block in Shared SRAM C */
    telem->magic = 0x52495343;        /* "RISC" */
    telem->boot_flag = 0x00000001;
    telem->heartbeat = 1;
    telem->loop_counter = 1;
    telem->status_magic = 0x414C4956; /* "ALIV" */
    telem->last_cmd = 0;

    /* 2. Initialize and write ASCII trace buffer (0x00029000) */
    trace_init();
    trace_puts("================================================================\n");
    trace_puts("  XuanTie E907 RISC-V Co-Processor Flight Controller Online!   \n");
    trace_puts("  SoC: Allwinner A523/A527 | Execution: 256KB Dedicated SRAM   \n");
    trace_puts("  Driver: Linux sunxi-rproc | Interface: Debugfs trace0        \n");
    trace_puts("================================================================\n");

    uint32_t heartbeat = 1;
    uint32_t fast_loops = 1;
    volatile uint32_t delay_counter = 0;

    while (1) {
        /* Fast inner loop counter */
        fast_loops++;
        telem->loop_counter = fast_loops;

        /* Periodic heartbeat pulse (~10 Hz) */
        delay_counter++;
        if (delay_counter >= 50000) {
            delay_counter = 0;
            heartbeat++;
            telem->heartbeat = heartbeat;
        }
    }

    return 0;
}
