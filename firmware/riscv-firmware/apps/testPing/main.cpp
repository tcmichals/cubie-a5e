/*
 * main.cpp - testPing: Shared Memory Ping-Pong Firmware (UIO / direct SRAM)
 *
 * Target: Allwinner T527 XuanTie E906 (RV32IMAFDC @ 200 MHz)
 *
 * Protocol (common/include/shm_ping_protocol.h): one ping/pong slot in SRAM,
 * one writer per field. Linux posts a ping by advancing ping_seq; the E906
 * answers by writing the pong and advancing pong_seq to match. Nothing is
 * answered twice and nothing is overwritten before Linux has read it, because
 * Linux posts the next ping only after pong_seq caught up.
 *
 * The E906 -> Linux mailbox doorbell (Channel 0, GIC SPI 0, the UIO IRQ) is a
 * wake-up hint only. It is written when the host's FIFO is empty; if an entry
 * is still queued, a wake-up is already pending and adding another gains
 * nothing. The pong itself is always in SRAM.
 */

#include <stdint.h>
#include <string.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"
#include "hal/msgbox.hpp"
#include "include/shm_ping_protocol.h"

#define SHM_CHANNEL ((volatile ShmPingChannel *)SHM_PING_SRAM_ADDR)

namespace {

void post_pong(uint32_t magic, uint32_t seq, uint64_t host_ts)
{
    volatile ShmPingChannel *ch = SHM_CHANNEL;

    ch->pong_pkt.magic         = magic;
    ch->pong_pkt.seq           = seq;
    ch->pong_pkt.host_tx_ts_ns = host_ts;
    ch->pong_pkt.riscv_cycles  = hal::Timer::get_ticks();
    ch->pong_pkt.payload_len   = sizeof(ch->pong_pkt.payload);

    // Echo the whole payload in 4-byte words, then tag it
    for (size_t i = 0; i < sizeof(ch->pong_pkt.payload); i += 4) {
        *(volatile uint32_t *)(&ch->pong_pkt.payload[i]) =
            *(volatile uint32_t *)(&ch->ping_pkt.payload[i]);
    }
    ch->pong_pkt.payload[0] = 'P';
    ch->pong_pkt.payload[1] = 'O';
    ch->pong_pkt.payload[2] = 'N';
    ch->pong_pkt.payload[3] = 'G';

    // Publish: the packet must be visible before pong_seq says it is there
    asm volatile("fence rw, rw" ::: "memory");
    ch->pong_seq = seq;
    asm volatile("fence rw, rw" ::: "memory");

    if (hal::MsgBox::tx_pending(hal::MsgBox::Channel::Channel0) == 0) {
        (void)hal::MsgBox::send(hal::MsgBox::Channel::Channel0, seq);
    }
}

} // namespace

int main(void) {
    hal::Trace::init();
    hal::Timer::init();
    hal::MsgBox::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E906 testPing (UIO / SHM ping-pong)   \n");
    hal::Trace::puts("  Channel : SRAM ping/pong slot, sequence-numbered            \n");
    hal::Trace::puts("  Doorbell: MSGBOX Channel 0 -> ARM GIC SPI 0 (wake-up hint)  \n");
    hal::Trace::puts("================================================================\n");

    volatile ShmPingChannel *ch = SHM_CHANNEL;

    // Adopt whatever Linux last posted: an old ping is not answered again.
    uint32_t handled = ch->ping_seq;
    ch->pong_seq = handled;
    ch->total_pings = 0;
    ch->errors = 0;

    uint32_t last_reported = 0;
    uint32_t heartbeat = 0;

    while (1) {
        // The mailbox carries no data for this protocol; keep the FIFO empty.
        while (hal::MsgBox::is_rx_pending(hal::MsgBox::Channel::Channel1)) {
            (void)hal::MsgBox::receive(hal::MsgBox::Channel::Channel1);
        }

        uint32_t posted = ch->ping_seq;
        if (posted != handled) {
            asm volatile("fence rw, rw" ::: "memory"); // read ping_pkt after ping_seq

            uint32_t pkt_seq = ch->ping_pkt.seq;
            uint64_t host_ts = ch->ping_pkt.host_tx_ts_ns;

            if (posted != handled + 1 || pkt_seq != posted || ch->ping_pkt.magic != SHM_PING_MAGIC) {
                ch->errors = ch->errors + 1;
                hal::Trace::printf("[testPing] ERROR: malformed ping (ping_seq %u, expected %u, packet seq %u)\n",
                                   posted, handled + 1, pkt_seq);
                post_pong(SHM_ERROR_MAGIC, posted, host_ts);
            } else {
                post_pong(SHM_PONG_MAGIC, posted, host_ts);
                ch->total_pings = ch->total_pings + 1;
            }
            handled = posted;
        }

        uint32_t pings = ch->total_pings;
        if (pings >= last_reported + 10000) {
            last_reported = pings;
            hal::Trace::printf("[testPing] Answered %u pings | errors: %u\n", pings, ch->errors);
        }

        if ((++heartbeat & 0x7FFFFFF) == 0) {
            hal::Trace::printf("[testPing] Idle | answered %u pings | errors: %u\n", ch->total_pings, ch->errors);
        }
    }

    return 0;
}
