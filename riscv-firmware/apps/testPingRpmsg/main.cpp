/*
 * main.cpp - testPingRpmsg: Standard Linux RPMsg over VirtIO Firmware
 *
 * Target: Allwinner T527 XuanTie E907 (RV32IMAFDC @ 200 MHz)
 * Protocol: Standard OpenAMP / Linux virtio_rpmsg_bus using modern C++ hal::Rpmsg
 *
 * Features:
 * 1. Announces Name Service endpoint "rpmsg-ping-channel" (local addr: 1024).
 * 2. Uses zero-allocation C++ hal::Rpmsg endpoint callback engine.
 * 3. Handles RPMsg ping messages and replies with pong messages.
 * 4. Periodic telemetry output to /sys/kernel/debug/remoteproc/remoteproc0/trace0.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <atomic>
#include "hal/trace.hpp"
#include "hal/timer.hpp"
#include "hal/rpmsg.hpp"
#include "include/resource_table.h"

extern "C" const struct rpmsg_resource_table global_resource_table;

namespace {
    std::atomic<uint32_t> g_ping_count{0};

    void handle_ping_message(const hal::RpmsgMessage &msg, void *user_data) {
        (void)user_data;
        g_ping_count.fetch_add(1, std::memory_order_relaxed);

        // Prepare Pong response payload (echo incoming data with PONG prefix)
        uint8_t pong_buf[RPMSG_BUFFER_SIZE];
        uint16_t resp_len = msg.len;
        if (resp_len > sizeof(pong_buf)) {
            resp_len = sizeof(pong_buf);
        }

        if (msg.data && resp_len > 0) {
            memcpy(pong_buf, msg.data, resp_len);
        }

        // Overwrite header with PONG tag if space allows
        if (resp_len >= 4) {
            pong_buf[0] = 'P';
            pong_buf[1] = 'O';
            pong_buf[2] = 'N';
            pong_buf[3] = 'G';
        }

        hal::Rpmsg::reply(msg, pong_buf, resp_len);
    }
}

int main(void) {
    // 1. Initialize HAL
    hal::Trace::init(/*enable_serial_mirror=*/true);
    hal::Timer::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testPingRpmsg (Linux RPMsg)      \n");
    hal::Trace::puts("  Protocol: VirtIO vdev (2 vrings) + Modern C++ hal::Rpmsg    \n");
    hal::Trace::puts("  Channel : \"rpmsg-ping-channel\" (Endpoint Addr: 1024)         \n");
    hal::Trace::puts("================================================================\n");

    // 2. Initialize RPMsg Engine with standard Resource Table
    hal::Rpmsg::init(&global_resource_table);

    // 3. Register endpoint handler for ping channel (1024) and default (0)
    hal::Rpmsg::register_endpoint(RPMSG_PING_EPT_ADDR, handle_ping_message);
    hal::Rpmsg::register_endpoint(0, handle_ping_message);

    bool announced = false;
    uint32_t last_reported_pings = 0;
    uint32_t idle_ticks = 0;

    while (1) {
        // Check if Linux Host VirtIO driver is initialized & online
        if (hal::Rpmsg::is_driver_ready()) {
            if (!announced) {
                hal::Trace::puts("[testPingRpmsg] Linux VirtIO driver ONLINE -> Announcing \"rpmsg-ping-channel\" (addr=1024)\n");
                hal::Rpmsg::announce_service("rpmsg-ping-channel", RPMSG_PING_EPT_ADDR);
                announced = true;
            }

            // Poll incoming VirtIO packets and dispatch to registered callbacks
            hal::Rpmsg::poll();
        }

        // Periodic Trace Logging
        uint32_t current_pings = g_ping_count.load(std::memory_order_relaxed);
        if (current_pings >= last_reported_pings + 5000) {
            last_reported_pings = current_pings;
            hal::Trace::printf("[testPingRpmsg] Processed %u RPMsg ping-pongs (RX: %u, TX: %u)\n",
                               current_pings, hal::Rpmsg::get_rx_count(), hal::Rpmsg::get_tx_count());
        }

        idle_ticks++;
        if ((idle_ticks & 0x7FFFFFF) == 0) {
            hal::Trace::printf("[testPingRpmsg] Status: %s | Total RPMsg Pings: %u\n",
                               hal::Rpmsg::is_driver_ready() ? "READY" : "WAIT_HOST", current_pings);
        }
    }

    return 0;
}

