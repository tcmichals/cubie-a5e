/*
 * main.cpp - testDRAMMsg: Hybrid SRAM SPSC Queue / DDR DRAM Payload Buffers
 *
 * Target: Allwinner T527 XuanTie E907 (RV32IMAFDC @ 200 MHz)
 *
 * Demonstrates:
 * 1. Control structures (descriptors, head/tail pointers, doorbells) in fast
 *    zero-wait-state On-Chip SRAM A2 (@ 0x00040000).
 * 2. High-capacity payload buffers allocated in DDR DRAM Carveout (@ 0x48100000).
 * 3. PMP / XuanTie Cache Maintenance configuration for un-cached DMA coherence.
 * 4. Latency and throughput benchmarking compared against pure on-chip SRAM.
 */

#include <stdint.h>
#include <string.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"
#include "hal/pmp.hpp"
#include "include/dram_spsc_protocol.h"

#define SPSC_CTRL ((volatile DramSpscControlBlock *)DRAM_SPSC_SRAM_ADDR)
#define DRAM_POOL ((uint8_t *)DRAM_SPSC_DRAM_ADDR)

int main(void) {
    // 1. Initialize HAL
    hal::Trace::init(/*enable_serial_mirror=*/true);
    hal::Timer::init();

    // 2. Configure PMP & XuanTie Cache Maintenance for DDR Carveout
    hal::Pmp::init();
    hal::Pmp::configure_dram_carveout(DRAM_SPSC_DRAM_ADDR, DRAM_SPSC_DRAM_SIZE);

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testDRAMMsg (Hybrid SRAM/DRAM IPC)\n");
    hal::Trace::puts("  Control Block: Shared SRAM A2 @ 0x00040000 (Zero-Wait-State) \n");
    hal::Trace::puts("  Payload Pool : DDR DRAM Carveout @ 0x48100000 (1 MB Window)  \n");
    hal::Trace::puts("  PMP / Cache  : Direct Uncached / Strongly-Ordered Coherent   \n");
    hal::Trace::puts("================================================================\n");

    // 3. Initialize SPSC Control Block in SRAM A2
    SPSC_CTRL->magic             = DRAM_SPSC_MAGIC_INIT;
    SPSC_CTRL->ring_size         = DRAM_SPSC_RING_ENTRIES;
    SPSC_CTRL->dram_pool_phys    = DRAM_SPSC_DRAM_ADDR;
    SPSC_CTRL->dram_pool_size    = DRAM_SPSC_DRAM_SIZE;

    SPSC_CTRL->host_head         = 0;
    SPSC_CTRL->riscv_tail        = 0;
    SPSC_CTRL->host_doorbell     = 0;

    SPSC_CTRL->riscv_head        = 0;
    SPSC_CTRL->host_tail         = 0;
    SPSC_CTRL->riscv_doorbell    = 0;

    SPSC_CTRL->total_pings_recv  = 0;
    SPSC_CTRL->total_pongs_sent  = 0;
    SPSC_CTRL->total_bytes_transferred = 0;

    // Initialize Descriptor Buffers in DRAM Pool
    // TX Slots (Host -> RISC-V): Offsets 0 .. 15 * 4096 (0 KB - 64 KB)
    // RX Slots (RISC-V -> Host): Offsets 16 .. 31 * 4096 (64 KB - 128 KB)
    for (uint32_t i = 0; i < DRAM_SPSC_RING_ENTRIES; ++i) {
        SPSC_CTRL->tx_ring[i].dram_buf_offset = i * DRAM_SPSC_MAX_BUF_LEN;
        SPSC_CTRL->tx_ring[i].payload_len     = 0;
        SPSC_CTRL->tx_ring[i].seq             = 0;
        SPSC_CTRL->tx_ring[i].flags           = 0;

        SPSC_CTRL->rx_ring[i].dram_buf_offset = (DRAM_SPSC_RING_ENTRIES + i) * DRAM_SPSC_MAX_BUF_LEN;
        SPSC_CTRL->rx_ring[i].payload_len     = 0;
        SPSC_CTRL->rx_ring[i].seq             = 0;
        SPSC_CTRL->rx_ring[i].flags           = 0;
    }

    hal::Pmp::memory_fence();

    uint32_t last_reported_pings = 0;
    uint32_t heartbeat = 0;

    // 4. High-Speed SPSC Processing Loop
    while (1) {
        // Check for incoming packet from Linux Host
        uint32_t head = SPSC_CTRL->host_head;
        uint32_t tail = SPSC_CTRL->riscv_tail;

        if (head != tail || SPSC_CTRL->host_doorbell == 1) {
            uint32_t tx_slot = tail % DRAM_SPSC_RING_ENTRIES;
            uint32_t rx_slot = SPSC_CTRL->riscv_head % DRAM_SPSC_RING_ENTRIES;

            volatile DramSpscDesc *tx_desc = &SPSC_CTRL->tx_ring[tx_slot];
            volatile DramSpscDesc *rx_desc = &SPSC_CTRL->rx_ring[rx_slot];

            uint32_t len = tx_desc->payload_len;
            if (len > DRAM_SPSC_MAX_BUF_LEN) len = DRAM_SPSC_MAX_BUF_LEN;

            uint32_t tx_dram_offset = tx_desc->dram_buf_offset;
            uint32_t rx_dram_offset = rx_desc->dram_buf_offset;

            volatile uint8_t *tx_dram_buf = DRAM_POOL + tx_dram_offset;
            volatile uint8_t *rx_dram_buf = DRAM_POOL + rx_dram_offset;

            // Invalidate D-cache for DRAM buffer
            hal::Pmp::dcache_invalidate_range((uintptr_t)tx_dram_buf, len);

            // Read payload from DRAM and write Pong Echo response to DRAM
            for (uint32_t i = 0; i < len; ++i) {
                rx_dram_buf[i] = tx_dram_buf[i];
            }

            // Add response signature tag if payload length permits
            if (len >= 4) {
                rx_dram_buf[0] = 'D'; rx_dram_buf[1] = 'R'; rx_dram_buf[2] = 'A'; rx_dram_buf[3] = 'M';
            }

            // Clean D-cache for response buffer
            hal::Pmp::dcache_clean_range((uintptr_t)rx_dram_buf, len);

            // Populate RX Descriptor in SRAM
            rx_desc->seq           = tx_desc->seq;
            rx_desc->host_tx_ts_ns = tx_desc->host_tx_ts_ns;
            rx_desc->riscv_cycles  = hal::Timer::get_ticks();
            rx_desc->payload_len   = len;
            rx_desc->flags         = 2; // ACK

            hal::Pmp::memory_fence();

            // Advance SPSC Ring Pointers in SRAM
            SPSC_CTRL->riscv_tail = tail + 1;
            SPSC_CTRL->riscv_head = SPSC_CTRL->riscv_head + 1;
            SPSC_CTRL->host_doorbell = 0;
            SPSC_CTRL->riscv_doorbell = 1;

            SPSC_CTRL->total_pings_recv = SPSC_CTRL->total_pings_recv + 1;
            SPSC_CTRL->total_pongs_sent = SPSC_CTRL->total_pongs_sent + 1;
            SPSC_CTRL->total_bytes_transferred = SPSC_CTRL->total_bytes_transferred + len;
        }

        // Periodic Telemetry Reporting
        uint32_t current_pings = SPSC_CTRL->total_pings_recv;
        if (current_pings >= last_reported_pings + 10000) {
            last_reported_pings = current_pings;
            hal::Trace::printf("[testDRAMMsg] Processed %u DRAM payload messages (%u KB total)\n",
                               current_pings, (uint32_t)(SPSC_CTRL->total_bytes_transferred / 1024));
        }

        heartbeat++;
        if ((heartbeat & 0x7FFFFFF) == 0) {
            hal::Trace::printf("[testDRAMMsg] Idle Waiting... Total DRAM Msgs: %u\n", SPSC_CTRL->total_pings_recv);
        }
    }

    return 0;
}
