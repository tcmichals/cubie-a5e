/*
 * main.cpp - testDRAMMsg: Hybrid SRAM SPSC Queue / DDR DRAM Payload Buffers
 *
 * Target: Allwinner T527 XuanTie E906 (RV32IMAFDC @ 200 MHz)
 *
 * Demonstrates:
 * 1. Control structures (descriptors, head/tail pointers, doorbells) in fast
 *    zero-wait-state On-Chip SRAM A2 (@ 0x00040000).
 * 2. High-capacity payload buffers allocated in DDR DRAM Carveout (@ 0x48100000).
 * 3. PMP access control and uncached DMA coherency via mhcr.DE = 0.
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
    hal::Trace::init();
    hal::Timer::init();

    // 2. Configure PMP & Memory Access
    // NOTE: PMP only configures basic R/W/X permissions in Machine mode;
    // it CANNOT mark memory as non-cacheable or uncached. The E906 lacks
    // programmable PMA registers. D-Cache is intentionally kept DISABLED
    // (mhcr.DE = 0) so DDR accesses bypass cache and remain coherent with
    // the Linux host. (AbstractX will implement proper cache architecture).
    hal::Pmp::init();
    hal::Pmp::configure_dram_carveout(DRAM_SPSC_DRAM_ADDR, DRAM_SPSC_DRAM_SIZE);

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E906 testDRAMMsg (Hybrid SRAM/DRAM IPC)\n");
    hal::Trace::puts("  Control Block: Dedicated MCU SRAM C @ 0x07130000 (Zero-Wait)  \n");
    hal::Trace::puts("  Payload Pool : DDR DRAM Carveout @ 0x48100000 (1 MB Window)  \n");
    hal::Trace::puts("  D-Cache      : Disabled (DE=0) for Coherent Uncached DMA/IPC  \n");
    hal::Trace::puts("================================================================\n");

    // 3. Initialize SPSC Control Block in Dedicated MCU SRAM C
    SPSC_CTRL->magic             = DRAM_SPSC_MAGIC_INIT;
    SPSC_CTRL->ring_size         = DRAM_SPSC_RING_ENTRIES;
    SPSC_CTRL->dram_pool_phys    = DRAM_SPSC_DRAM_ADDR;
    SPSC_CTRL->dram_pool_size    = DRAM_SPSC_DRAM_SIZE;

    SPSC_CTRL->host_head         = 0;
    SPSC_CTRL->riscv_tail        = 0;
    SPSC_CTRL->reserved0         = 0;

    SPSC_CTRL->riscv_head        = 0;
    SPSC_CTRL->host_tail         = 0;
    SPSC_CTRL->reserved1         = 0;

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

    // 4. SPSC processing loop. Take a ping only when one is queued AND the
    // reply ring has room; otherwise leave it queued (Linux then waits).
    uint32_t ring_errors = 0;
    uint32_t reply_full_waits = 0;

    while (1) {
        uint32_t head = SPSC_CTRL->host_head;
        uint32_t tail = SPSC_CTRL->riscv_tail;
        uint32_t queued = head - tail;

        if (queued > DRAM_SPSC_RING_ENTRIES) {
            // Only a corrupt index can say more than the ring holds
            if (ring_errors++ == 0) {
                hal::Trace::printf("[testDRAMMsg] ERROR: host_head %u - riscv_tail %u exceeds ring size\n",
                                   head, tail);
            }
            continue;
        }

        if (queued != 0) {
            uint32_t rx_head = SPSC_CTRL->riscv_head;
            if (rx_head - SPSC_CTRL->host_tail >= DRAM_SPSC_RING_ENTRIES) {
                reply_full_waits++;  // reply ring full: Linux has not read the replies yet
                continue;
            }
            hal::Pmp::memory_fence(); // read the descriptor after host_head

            uint32_t tx_slot = tail % DRAM_SPSC_RING_ENTRIES;
            uint32_t rx_slot = rx_head % DRAM_SPSC_RING_ENTRIES;

            volatile DramSpscDesc *tx_desc = &SPSC_CTRL->tx_ring[tx_slot];
            volatile DramSpscDesc *rx_desc = &SPSC_CTRL->rx_ring[rx_slot];

            uint32_t len = tx_desc->payload_len;
            uint32_t tx_dram_offset = tx_desc->dram_buf_offset;
            uint32_t rx_dram_offset = (DRAM_SPSC_RING_ENTRIES + rx_slot) * DRAM_SPSC_MAX_BUF_LEN;
            bool ok = tx_desc->flags == DRAM_SPSC_FLAG_READY && len >= 4 && len <= DRAM_SPSC_MAX_BUF_LEN &&
                      tx_dram_offset == tx_slot * DRAM_SPSC_MAX_BUF_LEN;

            if (ok) {
                volatile uint8_t *tx_dram_buf = DRAM_POOL + tx_dram_offset;
                volatile uint8_t *rx_dram_buf = DRAM_POOL + rx_dram_offset;

                // NOTE: dcache_invalidate_range and dcache_clean_range below are
                // no-ops because D-Cache is turned off (DE=0). PMP cannot mark
                // DDR non-cacheable. AbstractX will implement proper cache maintenance.
                hal::Pmp::dcache_invalidate_range((uintptr_t)tx_dram_buf, len);

                // Echo the payload, tagged "DRAM"
                rx_dram_buf[0] = 'D'; rx_dram_buf[1] = 'R'; rx_dram_buf[2] = 'A'; rx_dram_buf[3] = 'M';
                for (uint32_t k = 4; k < len; ++k) {
                    rx_dram_buf[k] = tx_dram_buf[k];
                }

                hal::Pmp::dcache_clean_range((uintptr_t)rx_dram_buf, len);
            } else {
                if (ring_errors++ == 0) {
                    hal::Trace::printf("[testDRAMMsg] ERROR: malformed ping in slot %u (len %u, offset 0x%x)\n",
                                       tx_slot, len, tx_dram_offset);
                }
                len = 0;
            }

            rx_desc->dram_buf_offset = rx_dram_offset;
            rx_desc->seq           = tx_desc->seq;
            rx_desc->host_tx_ts_ns = tx_desc->host_tx_ts_ns;
            rx_desc->riscv_cycles  = hal::Timer::get_ticks();
            rx_desc->payload_len   = len;
            rx_desc->flags         = ok ? DRAM_SPSC_FLAG_ACK : DRAM_SPSC_FLAG_ERROR;

            // Publish the reply, then release the ping slot
            hal::Pmp::memory_fence();
            SPSC_CTRL->riscv_head = rx_head + 1;
            SPSC_CTRL->riscv_tail = tail + 1;

            SPSC_CTRL->total_pings_recv = SPSC_CTRL->total_pings_recv + 1;
            SPSC_CTRL->total_pongs_sent = SPSC_CTRL->total_pongs_sent + 1;
            SPSC_CTRL->total_bytes_transferred = SPSC_CTRL->total_bytes_transferred + len;
        }

        // Periodic Telemetry Reporting
        uint32_t current_pings = SPSC_CTRL->total_pings_recv;
        if (current_pings >= last_reported_pings + 10000) {
            last_reported_pings = current_pings;
            hal::Trace::printf("[testDRAMMsg] Processed %u DRAM payload messages (%u KB total, "
                               "ring errors: %u, waits reply-ring-full: %u)\n",
                               current_pings, (uint32_t)(SPSC_CTRL->total_bytes_transferred / 1024),
                               ring_errors, reply_full_waits);
        }

        heartbeat++;
        if ((heartbeat & 0x7FFFFFF) == 0) {
            hal::Trace::printf("[testDRAMMsg] Idle Waiting... Total DRAM Msgs: %u\n", SPSC_CTRL->total_pings_recv);
        }
    }

    return 0;
}
