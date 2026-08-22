/* main.cpp - XuanTie RISC-V E907 Hello World & Ingestion Firmware */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "uart0.h"
#include "trace.h"
#include "mailbox.hpp"
#include "ringbuffer.h"
#include "spi.h"

using namespace hardware;

/* Fixed Telemetry Address in Shared SRAM for Linux Host devmem inspection */
#define TELEMETRY_BASE      0x00028000   /* Host Physical: 0x00028000 */

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
    /* Initialize Telemetry Block in Shared SRAM C */
    telem->magic = 0x52495343;        /* "RISC" */
    telem->boot_flag = 0x00000001;
    telem->heartbeat = 0;
    telem->loop_counter = 0;
    telem->status_magic = 0x414C4956; /* "ALIV" */
    telem->last_cmd = 0;

    /* 1. Print Hello World over physical UART0 serial port */
    uart0_puts("\n========================================\n");
    uart0_puts(" Hello World from XuanTie RISC-V Core!  \n");
    uart0_puts(" Running bare-metal on Radxa Cubie A5E  \n");
    uart0_puts("========================================\n\n");

    /* 2. Log boot banner to remoteproc trace buffer */
    trace_puts("[RISC-V E907] Hello World Firmware Booted!\n");
    trace_puts("[RISC-V E907] Core: XuanTie E907 (RV32IMAC @ 600MHz)\n");

    /* 3. Initialize Mailbox IPC */
    Mailbox::init();
    trace_puts("[RISC-V E907] Mailbox hardware initialized.\n");

    /* Send initial hello doorbell to ARM host (Channel 1, payload 'HELO' 0x48454C4F) */
    Mailbox::send_msg(1, 0x48454C4F);
    trace_puts("[RISC-V E907] Sent initial doorbell notification to ARM host.\n");

    uint32_t heartbeat = 0;
    uint32_t fast_loops = 0;
    volatile uint32_t delay_counter = 0;

    while (1) {
        /* Update fast loop counters on every single iteration */
        fast_loops++;
        telem->loop_counter = fast_loops;

        /* Check for incoming mailbox messages from ARM Linux host */
        if (Mailbox::has_new_msg(0)) {
            uint32_t cmd = Mailbox::read_msg(0);
            telem->last_cmd = cmd;
            trace_puts("[RISC-V E907] Received Mailbox CMD from host!\n");
            
            /* Respond with echo payload incremented by 1 */
            Mailbox::send_msg(1, cmd + 1);
        }

        /* Periodic heartbeat pulse (~every 50,000 iterations @ 600MHz = ~10-20ms) */
        delay_counter++;
        if (delay_counter >= 50000) {
            delay_counter = 0;
            heartbeat++;

            telem->heartbeat = heartbeat;

            /* Periodic print on UART0 every ~2 seconds (100 heartbeats) */
            if ((heartbeat % 100) == 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "[RISC-V E907] Heartbeat pulse #%lu\n", (unsigned long)heartbeat);
                trace_puts(msg);
                uart0_puts(msg);
            }
        }
    }

    return 0;
}

