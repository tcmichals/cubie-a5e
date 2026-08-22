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
    /* 1. Initialize Telemetry Block in Shared SRAM C */
    telem->magic = 0x52495343;        /* "RISC" */
    telem->boot_flag = 0x00000001;
    telem->heartbeat = 1;
    telem->loop_counter = 1;
    telem->status_magic = 0x414C4956; /* "ALIV" */
    telem->last_cmd = 0;

    uint32_t heartbeat = 1;
    uint32_t fast_loops = 1;
    volatile uint32_t delay_counter = 0;

    while (1) {
        /* Update fast loop counters on every single iteration */
        fast_loops++;
        telem->loop_counter = fast_loops;

        /* Periodic heartbeat pulse */
        delay_counter++;
        if (delay_counter >= 50000) {
            delay_counter = 0;
            heartbeat++;
            telem->heartbeat = heartbeat;
        }
    }

    return 0;
}

