/*
 * shm_ping_host.hpp - Linux side of the SRAM ping/pong slot (shm_ping_protocol.h)
 *
 * Shared by ping_shm and ping_uio. Linux writes only ping_seq and ping_pkt;
 * the E906 writes only pong_seq and pong_pkt. A ping is posted only when the
 * slot is free (pong_seq == ping_seq), so nothing is ever overwritten.
 */
#pragma once

#include <cstdint>
#include <string>
#include <time.h>

#include "include/shm_ping_protocol.h"

namespace shm_ping {

inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

inline uint8_t pattern_byte(uint32_t seq, size_t i) {
    return (uint8_t)(seq * 31u + i * 7u);
}

enum class Outcome { Pong, Timeout, Malformed };

/*
 * Wait until the slot is free and return the last sequence number used, so a
 * new run continues after whatever an earlier run left behind.
 */
inline bool sync(volatile ShmPingChannel *ch, uint32_t timeout_ms, uint32_t &last_seq, std::string &why) {
    uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ULL;
    for (;;) {
        uint32_t posted = ch->ping_seq;
        uint32_t answered = ch->pong_seq;
        if (posted == answered) {
            last_seq = posted;
            return true;
        }
        if (now_ns() >= deadline) {
            why = "slot busy: ping_seq " + std::to_string(posted) + " never answered (pong_seq " +
                  std::to_string(answered) + "); is testPing.elf running?";
            return false;
        }
    }
}

/*
 * Post ping `seq` (must be last_seq + 1) and spin until the E906 answers it.
 * Every byte of the pong is checked; anything unexpected is Malformed.
 */
inline Outcome exchange(volatile ShmPingChannel *ch, uint32_t seq, uint32_t timeout_ms,
                        uint64_t &tx_ns, uint64_t &rx_ns, std::string &why) {
    ch->ping_pkt.magic = SHM_PING_MAGIC;
    ch->ping_pkt.seq = seq;
    ch->ping_pkt.riscv_cycles = 0;
    ch->ping_pkt.payload_len = sizeof(ch->ping_pkt.payload);
    for (size_t i = 0; i < sizeof(ch->ping_pkt.payload); ++i) {
        ch->ping_pkt.payload[i] = (char)pattern_byte(seq, i);
    }
    tx_ns = now_ns();
    ch->ping_pkt.host_tx_ts_ns = tx_ns;

    __sync_synchronize(); // packet visible before ping_seq announces it
    ch->ping_seq = seq;

    uint64_t deadline = tx_ns + (uint64_t)timeout_ms * 1000000ULL;
    for (;;) {
        uint32_t answered = ch->pong_seq;
        if (answered == seq) {
            break;
        }
        if (answered != seq - 1) {
            why = "pong_seq jumped to " + std::to_string(answered) + " while waiting for " + std::to_string(seq);
            return Outcome::Malformed;
        }
        if (now_ns() >= deadline) {
            why = "no pong for seq " + std::to_string(seq);
            return Outcome::Timeout;
        }
    }
    rx_ns = now_ns();
    __sync_synchronize(); // read pong_pkt after pong_seq

    uint32_t magic = ch->pong_pkt.magic;
    if (magic == SHM_ERROR_MAGIC) {
        why = "E906 rejected ping seq " + std::to_string(seq) + " as malformed";
        return Outcome::Malformed;
    }
    if (magic != SHM_PONG_MAGIC || ch->pong_pkt.seq != seq || ch->pong_pkt.host_tx_ts_ns != tx_ns) {
        why = "bad pong header (magic 0x" + std::to_string(magic) + ", seq " + std::to_string(ch->pong_pkt.seq) +
              ", expected " + std::to_string(seq) + ")";
        return Outcome::Malformed;
    }
    if (ch->pong_pkt.payload[0] != 'P' || ch->pong_pkt.payload[1] != 'O' ||
        ch->pong_pkt.payload[2] != 'N' || ch->pong_pkt.payload[3] != 'G') {
        why = "pong tag missing in seq " + std::to_string(seq);
        return Outcome::Malformed;
    }
    for (size_t i = 4; i < sizeof(ch->pong_pkt.payload); ++i) {
        if ((uint8_t)ch->pong_pkt.payload[i] != pattern_byte(seq, i)) {
            why = "payload byte " + std::to_string(i) + " wrong in seq " + std::to_string(seq);
            return Outcome::Malformed;
        }
    }
    return Outcome::Pong;
}

} // namespace shm_ping
