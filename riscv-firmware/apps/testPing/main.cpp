/*
 * main.cpp - testPing: Ultra-Low-Latency Shared Memory Ping-Pong Firmware (Lite-libmetal style)
 *
 * Target: Allwinner T527 XuanTie E907 (RV32IMAFDC @ 600 MHz)
 *
 * Demonstrates:
 * 1. Zero-copy, lock-free shared SRAM memory channel (ShmPingChannel @ 0x07131000).
 * 2. Hardware Mailbox Doorbell integration (hal::MsgBox Channel 0 -> ARM GIC SPI 147).
 * 3. High-frequency ping-pong loop with sub-microsecond response latency.
 * 4. Capturing hardware cycle counts (mcycle) and echoing Linux timestamps.
 * 5. Dual-mode compatibility: Event-Driven UIO Doorbell and direct memory polling.
 */

#include <stdint.h>
#include <string.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"
#include "hal/msgbox.hpp"
#include "include/shm_ping_protocol.h"

#define SHM_CHANNEL ((volatile ShmPingChannel *)SHM_PING_SRAM_ADDR)

int main(void) {
    // 1. Initialize HAL
    hal::Trace::init(/*enable_serial_mirror=*/true);
    hal::Timer::init();
    hal::MsgBox::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testPing (Lite-libmetal UIO/SHM) \n");
    hal::Trace::puts("  Channel : Dedicated MCU SRAM C @ 0x07131000                 \n");
    hal::Trace::puts("  Doorbell: Hardware MSGBOX Channel 0 (ARM GIC SPI 147)       \n");
    hal::Trace::puts("  Mode    : Event-Driven UIO & High-Speed Ping-Pong Echo Loop \n");
    hal::Trace::puts("================================================================\n");

    // 2. Initialize Shared Memory Channel
    SHM_CHANNEL->host_doorbell  = 0;
    SHM_CHANNEL->riscv_doorbell = 0;
    SHM_CHANNEL->total_pings    = 0;
    SHM_CHANNEL->total_pongs    = 0;

    uint32_t last_reported_pings = 0;
    uint32_t loop_heartbeat = 0;

    // 3. Fast Ping-Pong Response Loop
    while (1) {
        // Check for incoming ping (either SRAM flag or Hardware Mailbox Channel 1 from Host)
        bool ping_ready = (SHM_CHANNEL->host_doorbell == 1);
        if (hal::MsgBox::is_rx_pending(hal::MsgBox::Channel::Channel1)) {
            (void)hal::MsgBox::receive(hal::MsgBox::Channel::Channel1);
            ping_ready = true;
        }

        if (ping_ready) {
            uint64_t current_cycles = hal::Timer::get_ticks();

            // Read incoming ping packet
            uint32_t seq = SHM_CHANNEL->ping_pkt.seq;
            uint64_t host_ts = SHM_CHANNEL->ping_pkt.host_tx_ts_ns;

            // Prepare Pong Response Packet
            SHM_CHANNEL->pong_pkt.magic         = SHM_PONG_MAGIC;
            SHM_CHANNEL->pong_pkt.seq           = seq;
            SHM_CHANNEL->pong_pkt.host_tx_ts_ns = host_ts;
            SHM_CHANNEL->pong_pkt.riscv_cycles  = current_cycles;
            SHM_CHANNEL->pong_pkt.payload_len   = 24;

            const char pong_msg[] = "PONG from XuanTie E907!";
            for (size_t i = 0; i < sizeof(pong_msg); ++i) {
                SHM_CHANNEL->pong_pkt.payload[i] = pong_msg[i];
            }

            // Memory barrier & Acknowledge host ping, assert RISC-V pong doorbell
            asm volatile("fence rw, rw" ::: "memory");
            SHM_CHANNEL->host_doorbell = 0;
            SHM_CHANNEL->riscv_doorbell = 1;

            // Pulse Hardware Mailbox Doorbell (Channel 0 -> ARM GIC SPI 147) for Linux UIO
            hal::MsgBox::send(hal::MsgBox::Channel::Channel0, seq);

            SHM_CHANNEL->total_pings = SHM_CHANNEL->total_pings + 1;
            SHM_CHANNEL->total_pongs = SHM_CHANNEL->total_pongs + 1;
        }

        // Periodic background trace status (every 10,000 pings or periodic idle)
        uint32_t current_pings = SHM_CHANNEL->total_pings;
        if (current_pings >= last_reported_pings + 10000) {
            last_reported_pings = current_pings;
            hal::Trace::printf("[testPing] Processed %u fast SHM ping-pongs | Active\n", current_pings);
        }

        loop_heartbeat++;
        if ((loop_heartbeat & 0x7FFFFFF) == 0) {
            hal::Trace::printf("[testPing] Idle Waiting... Total Pings: %u\n", SHM_CHANNEL->total_pings);
        }
    }

    return 0;
}
