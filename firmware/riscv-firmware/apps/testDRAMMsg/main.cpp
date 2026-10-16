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
    // Rings first, magic last: Linux starts when it sees magic
    SPSC_CTRL->magic             = 0;
    SPSC_CTRL->ring_size         = DRAM_SPSC_RING_ENTRIES;
    SPSC_CTRL->dram_pool_phys    = DRAM_SPSC_DRAM_ADDR;
    SPSC_CTRL->dram_pool_size    = DRAM_SPSC_DRAM_SIZE;

    ipc::ring_init(&SPSC_CTRL->tx_ring);
    ipc::ring_init(&SPSC_CTRL->rx_ring);
    SPSC_CTRL->magic             = DRAM_SPSC_MAGIC_INIT;

    ipc::Consumer<DramTxRing> tx_consumer(&SPSC_CTRL->tx_ring);
    ipc::Producer<DramRxRing> rx_producer(&SPSC_CTRL->rx_ring);

    uint32_t total_pings_recv = 0;
    uint64_t total_bytes = 0;
    uint32_t last_reported_pings = 0;
    uint32_t heartbeat = 0;

    // 4. SPSC processing loop using standard ipc::Consumer & Producer
    uint32_t ring_errors = 0;
    uint32_t reply_full_waits = 0;

    while (1) {
        if (tx_consumer.corrupt() || rx_producer.corrupt()) {
            if (ring_errors++ == 0) {
                hal::Trace::printf("[testDRAMMsg] ERROR: SpscRing index corrupt!\n");
            }
            continue;
        }

        const volatile DramSpscDesc *tx_desc = tx_consumer.peek();
        if (tx_desc != nullptr) {
            volatile DramSpscDesc *rx_desc = rx_producer.reserve();
            if (rx_desc == nullptr) {
                reply_full_waits++;  // reply ring full: Linux has not read the replies yet
                continue;
            }

            uint32_t tx_slot = tx_consumer.tail() & DramTxRing::kMask;
            uint32_t rx_slot = rx_producer.head() & DramRxRing::kMask;

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

            // Commit the reply, then release the ping slot
            rx_producer.commit();
            tx_consumer.release();

            total_pings_recv++;
            total_bytes += len;
        }

        // Periodic Telemetry Reporting
        if (total_pings_recv >= last_reported_pings + 10000) {
            last_reported_pings = total_pings_recv;
            hal::Trace::printf("[testDRAMMsg] Processed %u DRAM payload messages (%u KB total, "
                               "ring errors: %u, waits reply-ring-full: %u)\n",
                               total_pings_recv, (uint32_t)(total_bytes / 1024),
                               ring_errors, reply_full_waits);
        }

        heartbeat++;
        if ((heartbeat & 0x7FFFFFF) == 0) {
            hal::Trace::printf("[testDRAMMsg] Idle Waiting... Total DRAM Msgs: %u\n", total_pings_recv);
        }
    }

    return 0;
}
