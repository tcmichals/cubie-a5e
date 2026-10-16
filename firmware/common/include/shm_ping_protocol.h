#ifndef SHM_PING_PROTOCOL_H
#define SHM_PING_PROTOCOL_H

#include <stdint.h>

#define SHM_PING_MAGIC      0x50494E47UL /* "PING" */
#define SHM_PONG_MAGIC      0x504F4E47UL /* "PONG" */
#define SHM_ERROR_MAGIC     0x4552524FUL /* "ERRO": the E906 rejected a malformed ping */

#if defined(__riscv)
#define SHM_PING_SRAM_ADDR  0x3FFF0000UL /* Core DA in SRAM Space 0 (offset +0x30000) */
#else
#define SHM_PING_SRAM_ADDR  0x072B0000UL /* Host PA in SRAM Space 0 (Host 0x07280000 + 0x30000) */
#endif
#define SHM_PING_SRAM_SIZE  0x1000UL     /* 4 KB */

/*
 * Shared Memory Ping-Pong Packet (512 bytes, 8-byte aligned)
 */
struct __attribute__((aligned(8))) ShmPingPacket {
    uint32_t magic;         // SHM_PING_MAGIC, SHM_PONG_MAGIC or SHM_ERROR_MAGIC
    uint32_t seq;           // Sequence number, equal to the ping_seq/pong_seq that posted it
    uint64_t host_tx_ts_ns; // Linux Host TX timestamp
    uint64_t riscv_cycles;  // RISC-V mcycle hardware counter
    uint32_t payload_len;   // Length of payload
    char     payload[484];  // Payload (total packet 512 bytes)
};

/*
 * One-slot channel, one writer per field:
 *
 *   ping_seq  written only by Linux: sequence number of the newest ping posted
 *   pong_seq  written only by the E906: sequence number of the newest pong posted
 *
 * The slot is busy while pong_seq != ping_seq. Linux posts ping N+1 only when
 * pong_seq == ping_seq == N: write ping_pkt (seq = N+1), barrier, ping_seq = N+1.
 * The E906 answers when ping_seq == pong_seq + 1: check ping_pkt, write
 * pong_pkt, barrier, pong_seq = ping_seq. Any other ping_seq, or a ping_pkt
 * whose seq does not match, is malformed: the E906 answers with
 * SHM_ERROR_MAGIC and counts it in errors.
 *
 * All four fields are aligned 32-bit words, so each read or write is a single
 * access. The mailbox is only a wake-up hint for Linux; the data and its order
 * are carried here.
 */
struct __attribute__((aligned(8))) ShmPingChannel {
    volatile uint32_t ping_seq;     // Linux only
    volatile uint32_t pong_seq;     // E906 only
    volatile uint32_t total_pings;  // E906 only: pings answered
    volatile uint32_t errors;       // E906 only: malformed pings rejected
    ShmPingPacket     ping_pkt;     // Linux -> E906
    ShmPingPacket     pong_pkt;     // E906 -> Linux
};

#endif /* SHM_PING_PROTOCOL_H */
