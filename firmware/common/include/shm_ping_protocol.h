#ifndef SHM_PING_PROTOCOL_H
#define SHM_PING_PROTOCOL_H

#include <stdint.h>

#define SHM_PING_MAGIC      0x50494E47UL /* "PING" */
#define SHM_PONG_MAGIC      0x504F4E47UL /* "PONG" */

#if defined(__riscv)
#define SHM_PING_SRAM_ADDR  0x3FFF0000UL /* Core DA in SRAM Space 0 (offset +0x30000) */
#else
#define SHM_PING_SRAM_ADDR  0x072B0000UL /* Host PA in SRAM Space 0 (Host 0x07280000 + 0x30000) */
#endif
#define SHM_PING_SRAM_SIZE  0x1000UL     /* 4 KB */

/*
 * Lite-libmetal style Shared Memory Ping-Pong Packet Structure (512 bytes, 8-byte aligned)
 */
struct __attribute__((aligned(8))) ShmPingPacket {
    uint32_t magic;         // SHM_PING_MAGIC or SHM_PONG_MAGIC (4 bytes)
    uint32_t seq;           // Monotonic sequence counter (4 bytes)
    uint64_t host_tx_ts_ns; // Linux Host TX timestamp (8 bytes)
    uint64_t riscv_cycles;  // RISC-V mcycle hardware counter (8 bytes)
    uint32_t payload_len;   // Length of text payload (4 bytes)
    char     payload[484];  // Text payload buffer (484 bytes -> total 512 bytes)
};

/*
 * Bidirectional Fast Shared Memory Channel Layout
 */
struct __attribute__((aligned(8))) ShmPingChannel {
    volatile uint32_t host_doorbell;  // Written by Linux (1 = new ping ready, 0 = ack)
    volatile uint32_t riscv_doorbell; // Written by RISC-V (1 = new pong ready, 0 = ack)
    volatile uint32_t total_pings;    // Total pings received by RISC-V
    volatile uint32_t total_pongs;    // Total pongs transmitted by RISC-V
    ShmPingPacket     ping_pkt;       // Host -> RISC-V Packet
    ShmPingPacket     pong_pkt;       // RISC-V -> Host Packet
};

#endif /* SHM_PING_PROTOCOL_H */
