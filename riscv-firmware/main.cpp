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

/* SRAM C Shared Memory Interface Addresses */
#define SHARED_WINDOW_BASE 0x00078000
#define SHARED_RB_OFFSET    0x0000

static ringbuffer_t *shared_rb = (ringbuffer_t *)(SHARED_WINDOW_BASE + SHARED_RB_OFFSET);

int main(void) {
    /* 1. Print Hello World over physical UART0 serial port */
    uart0_puts("\n========================================\n");
    uart0_puts(" Hello World from XuanTie RISC-V Core!  \n");
    uart0_puts(" Running bare-metal on Radxa Cubie A5E  \n");
    uart0_puts("========================================\n\n");

    /* 2. Log boot banner to remoteproc trace buffer (/sys/kernel/debug/remoteproc/remoteproc0/trace0) */
    trace_puts("[RISC-V E907] Hello World Firmware Booted!\n");
    trace_puts("[RISC-V E907] Core: XuanTie E907 (RV32IMAC @ 600MHz)\n");

    /* 3. Initialize Mailbox IPC */
    Mailbox::init();
    trace_puts("[RISC-V E907] Mailbox hardware initialized.\n");

    /* Send initial hello doorbell to ARM host (Channel 1, payload 'HELO' 0x48454C4F) */
    Mailbox::send_msg(1, 0x48454C4F);
    trace_puts("[RISC-V E907] Sent initial doorbell notification to ARM host.\n");

    /* 4. Initialize Ring Buffer */
    ringbuffer_init(shared_rb);

    uint32_t heartbeat = 0;
    volatile uint32_t delay_counter = 0;

    while (1) {
        /* Check for incoming mailbox messages from ARM Linux host */
        if (Mailbox::has_new_msg(0)) {
            uint32_t cmd = Mailbox::read_msg(0);
            trace_puts("[RISC-V E907] Received Mailbox CMD from host!\n");
            
            /* Respond with echo payload incremented by 1 */
            Mailbox::send_msg(1, cmd + 1);
        }

        /* Periodic heartbeat pulse */
        delay_counter++;
        if (delay_counter >= 1000000) {
            delay_counter = 0;
            heartbeat++;

            /* Format simple heartbeat decimal into trace buffer */
            char msg[64];
            snprintf(msg, sizeof(msg), "[RISC-V E907] Heartbeat pulse #%lu\n", (unsigned long)heartbeat);
            trace_puts(msg);
            uart0_puts(msg);
        }
    }

    return 0;
}
