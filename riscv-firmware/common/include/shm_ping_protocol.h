#ifndef SHM_PING_PROTOCOL_H
#define SHM_PING_PROTOCOL_H

#include <stdint.h>

#define SHM_PING_MAGIC      0x50494E47UL /* "PING" */
#define SHM_PONG_MAGIC      0x504F4E47UL /* "PONG" */

#define SHM_PING_SRAM_ADDR  0x07131000UL /* Dedicated MCU SRAM C (offset past 4KB trace_buffer) */
#define SHM_PING_SRAM_SIZE  0x1000UL     /* 4 KB */

/*
 * Lite-libmetal style Shared Memory Ping-Pong Packet Structure (64 bytes)
 */
struct __attribute__((packed, aligned(4))) ShmPingPacket {
    uint32_t magic;         // SHM_PING_MAGIC or SHM_PONG_MAGIC
    uint32_t seq;           // Monotonic sequence counter
    uint64_t host_tx_ts_ns; // Linux Host TX timestamp (CLOCK_MONOTONIC ns)
    uint64_t riscv_cycles;  // RISC-V mcycle hardware counter
    uint32_t payload_len;   // Length of text payload
    char     payload[40];   // Text payload (e.g. "Ping payload from Linux")
};

/*
 * Bidirectional Fast Shared Memory Channel Layout
 */
struct __attribute__((packed, aligned(4))) ShmPingChannel {
    volatile uint32_t host_doorbell;  // Written by Linux (1 = new ping ready, 0 = ack)
    volatile uint32_t riscv_doorbell; // Written by RISC-V (1 = new pong ready, 0 = ack)
    volatile uint32_t total_pings;    // Total pings received by RISC-V
    volatile uint32_t total_pongs;    // Total pongs transmitted by RISC-V
    ShmPingPacket     ping_pkt;       // Host -> RISC-V Packet
    ShmPingPacket     pong_pkt;       // RISC-V -> Host Packet
};

#endif /* SHM_PING_PROTOCOL_H */
