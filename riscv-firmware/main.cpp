/* main.c - Ingestion firmware entry point */
#include <stdint.h>
#include <stdbool.h>
#include "spi.h"
#include "ringbuffer.h"
#include "mailbox.hpp"

using namespace hardware;

/* SRAM C Shared Memory Interface Addresses */
#define SHARED_WINDOW_BASE 0x00078000
#define SHARED_RB_OFFSET    0x0000

static ringbuffer_t *shared_rb = (ringbuffer_t *)(SHARED_WINDOW_BASE + SHARED_RB_OFFSET);

int main(void) {
    /* 1. Initialize SPI0 (FPGA Link A) and SPI1 (FPGA Link B) */
    spi_init(SPI0_BASE);
    spi_init(SPI1_BASE);

    /* 2. Initialize the shared memory ring buffer */
    ringbuffer_init(shared_rb);

    /* 3. Initialize the mailbox controller */
    Mailbox::init();

    uint8_t temp_buf[PACKET_SIZE];
    uint8_t rx_frame[PACKET_SIZE];

    /* Initialize temporary buffer with test header pattern */
    for (int i = 0; i < PACKET_SIZE; i++) {
        temp_buf[i] = 0xAA;
    }

    while (1) {
        /* 4. Ingest frame from FPGA SPI Link A */
        spi_transfer(SPI0_BASE, temp_buf, rx_frame, PACKET_SIZE);

        /* Validate packet signature: e.g. 0x5A 0xA5 header byte check */
        if (rx_frame[0] == 0x5A && rx_frame[1] == 0xA5) {
            /* Successfully read frame - push it onto the shared ring buffer */
            ringbuffer_push(shared_rb, rx_frame, PACKET_SIZE);
            /* Wake up the ARM Linux host! */
            Mailbox::send_msg(0, 1);
        }

        /* 5. Check for mailbox commands from ARM host */
        if (Mailbox::has_new_msg(0)) {
            uint32_t cmd = Mailbox::read_msg(0);
            /* Return echo acknowledgement incremented by 1 */
            Mailbox::send_msg(1, cmd + 1);
        }

        /* 6. Telemetry Loop Check - check for outgoing host packets to SPI1 */
        /* Simple instruction delay loop to prevent bus clogging */
        for (volatile int i = 0; i < 500; i++);
    }

    return 0;
}
