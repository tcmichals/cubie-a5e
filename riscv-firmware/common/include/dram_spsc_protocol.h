#ifndef DRAM_SPSC_PROTOCOL_H
#define DRAM_SPSC_PROTOCOL_H

#include <stdint.h>

#define DRAM_SPSC_MAGIC_INIT   0x53505343UL /* "SPSC" */
#define DRAM_SPSC_MAGIC_DATA   0x44415441UL /* "DATA" */

/*
 * Physical Memory Mapping
 *  - Control Block in Fast Zero-Wait-State Dedicated MCU SRAM C (0x07130000)
 *  - Payload Buffer Pool in DDR DRAM Carveout (0x48100000)
 */
#define DRAM_SPSC_SRAM_ADDR    0x07130000UL /* Dedicated MCU SRAM C Base */
#define DRAM_SPSC_SRAM_SIZE    0x1000UL     /* 4 KB Control Window */

#define DRAM_SPSC_DRAM_ADDR    0x48100000UL /* DDR Reserved Memory Carveout */
#define DRAM_SPSC_DRAM_SIZE    0x00100000UL /* 1 MB Buffer Pool */

#define DRAM_SPSC_RING_ENTRIES 16           /* 16 Slots per Ring */
#define DRAM_SPSC_MAX_BUF_LEN  4096UL       /* 4 KB Max Payload per Slot */

/*
 * SPSC Ring Descriptor (Placed in SRAM C)
 */
struct __attribute__((packed, aligned(4))) DramSpscDesc {
    uint32_t dram_buf_offset; // Byte offset into DRAM buffer pool (0x48100000 + offset)
    uint32_t payload_len;     // Actual valid payload length in bytes
    uint32_t seq;             // Monotonic sequence number
    uint32_t flags;           // Status flags (0 = Empty, 1 = Ready, 2 = Ack)
    uint64_t host_tx_ts_ns;   // Linux Host TX timestamp
    uint64_t riscv_cycles;    // RISC-V hardware mcycle timestamp
};

/*
 * Bidirectional SPSC Queue Control Block (Placed in SRAM C @ 0x07130000)
 */
struct __attribute__((packed, aligned(4))) DramSpscControlBlock {
    // Control block initialization signature
    volatile uint32_t magic;
    volatile uint32_t ring_size;
    volatile uint32_t dram_pool_phys;
    volatile uint32_t dram_pool_size;

    // Host -> RISC-V SPSC Queue Pointers & Doorbell
    volatile uint32_t host_head;      // Written by Linux (Producer)
    volatile uint32_t riscv_tail;     // Written by RISC-V (Consumer)
    volatile uint32_t host_doorbell;  // 1 = New packet ready for RISC-V

    // RISC-V -> Host SPSC Queue Pointers & Doorbell
    volatile uint32_t riscv_head;     // Written by RISC-V (Producer)
    volatile uint32_t host_tail;      // Written by Linux (Consumer)
    volatile uint32_t riscv_doorbell; // 1 = New packet ready for Host

    // Diagnostic Counters
    volatile uint32_t total_pings_recv;
    volatile uint32_t total_pongs_sent;
    volatile uint64_t total_bytes_transferred;

    // Descriptor Rings (16 slots Host->RISC-V, 16 slots RISC-V->Host)
    DramSpscDesc tx_ring[DRAM_SPSC_RING_ENTRIES]; // Host -> RISC-V
    DramSpscDesc rx_ring[DRAM_SPSC_RING_ENTRIES]; // RISC-V -> Host
};

#endif /* DRAM_SPSC_PROTOCOL_H */
