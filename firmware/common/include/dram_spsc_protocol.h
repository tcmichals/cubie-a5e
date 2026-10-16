#ifndef DRAM_SPSC_PROTOCOL_H
#define DRAM_SPSC_PROTOCOL_H

#include <stdint.h>
#include "ipc/spsc_ring.hpp"

#define DRAM_SPSC_MAGIC_INIT   0x53505343UL /* "SPSC" */
#define DRAM_SPSC_MAGIC_DATA   0x44415441UL /* "DATA" */

/*
 * Physical Memory Mapping
 *  - Control Block in Fast Zero-Wait-State Dedicated MCU SRAM C (0x07130000)
 *  - Payload Buffer Pool in DDR DRAM Carveout (0x48100000)
 */
#if defined(__riscv)
#define DRAM_SPSC_SRAM_ADDR    0x3FFF2000UL /* Core DA in SRAM Space 0 (offset +0x32000) */
#else
#define DRAM_SPSC_SRAM_ADDR    0x072B2000UL /* Host PA in SRAM Space 0 (Host 0x07280000 + 0x32000) */
#endif
#define DRAM_SPSC_SRAM_SIZE    0x1000UL     /* 4 KB Control Window */

#define DRAM_SPSC_DRAM_ADDR    0x48000000UL /* DDR Reserved Memory Carveout */
#define DRAM_SPSC_DRAM_SIZE    0x00100000UL /* 1 MB Buffer Pool */

#define DRAM_SPSC_RING_ENTRIES 16           /* 16 Slots per Ring */
#define DRAM_SPSC_MAX_BUF_LEN  4096UL       /* 4 KB Max Payload per Slot */

/* Descriptor flags */
#define DRAM_SPSC_FLAG_READY   1U  /* tx_ring: ping posted by Linux */
#define DRAM_SPSC_FLAG_ACK     2U  /* rx_ring: echo of the ping */
#define DRAM_SPSC_FLAG_ERROR   3U  /* rx_ring: the E906 rejected a malformed ping */

/*
 * Rules (ring mechanics are in ipc/spsc_ring.hpp):
 *   - tx_ring: Linux produces, the E906 consumes. rx_ring: the E906
 *     produces, Linux consumes. Nothing is dropped: a full ring makes the
 *     producer wait.
 *   - tx slot i always uses dram_buf_offset i * MAX_BUF_LEN and rx slot i
 *     (RING_ENTRIES + i) * MAX_BUF_LEN; the E906 checks the tx offset.
 *   - The rx echo starts with "DRAM" and then repeats the ping payload.
 *   - The E906 initialises both rings and writes magic last; Linux starts
 *     only after it sees magic.
 */

/*
 * SPSC Ring Descriptor (Placed in SRAM, 32 bytes, 8-byte aligned)
 */
struct __attribute__((aligned(8))) DramSpscDesc {
    uint32_t dram_buf_offset; // Byte offset into DRAM buffer pool (0x48000000 + offset)
    uint32_t payload_len;     // Actual valid payload length in bytes
    uint32_t seq;             // Monotonic sequence number
    uint32_t flags;           // DRAM_SPSC_FLAG_* (written by the ring's producer)
    uint64_t host_tx_ts_ns;   // Linux Host TX timestamp
    uint64_t riscv_cycles;    // RISC-V hardware mcycle timestamp
};

using DramTxRing = ipc::SpscRing<DramSpscDesc, DRAM_SPSC_RING_ENTRIES>;
using DramRxRing = ipc::SpscRing<DramSpscDesc, DRAM_SPSC_RING_ENTRIES>;

IPC_SPSC_LAYOUT_CHECK(DramTxRing);
IPC_SPSC_LAYOUT_CHECK(DramRxRing);

/*
 * Bidirectional SPSC Queue Control Block (Placed in SRAM)
 * Uses standard ipc::SpscRing for both Host->RISC-V (tx_ring) and RISC-V->Host (rx_ring).
 */
struct __attribute__((aligned(64))) DramSpscControlBlock {
    // Control block initialization signature & pool descriptors
    alignas(64) volatile uint32_t magic;
    volatile uint32_t ring_size;
    volatile uint32_t dram_pool_phys;
    volatile uint32_t dram_pool_size;

    // SPSC Rings (each sits on its own 64-byte line to prevent false sharing)
    alignas(64) DramTxRing tx_ring;
    alignas(64) DramRxRing rx_ring;
};

#endif /* DRAM_SPSC_PROTOCOL_H */

