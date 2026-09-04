#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include "include/resource_table.h"

namespace hal {

/*
 * RPMsg Packet View & Callback Definition
 */
struct RpmsgMessage {
    uint32_t src;
    uint32_t dst;
    uint16_t len;
    const uint8_t *data;
    uint16_t desc_idx;
};

using EndpointCallback = void (*)(const RpmsgMessage &msg, void *user_data);

/*
 * VirtIO RPMsg HAL Engine (Lite-libmetal style)
 *
 * Provides a zero-dynamic-allocation, C++ std::atomic-synchronized VirtIO
 * RPMsg endpoint manager for XuanTie RISC-V co-processors.
 */
class Rpmsg {
public:
    static constexpr size_t MAX_ENDPOINTS = 8;

    // Initialize RPMsg engine with pointer to resource table
    static void init(const struct rpmsg_resource_table *rsc) noexcept;

    // Check if Linux Host VirtIO driver is initialized & online
    static bool is_driver_ready() noexcept;

    // Register a local endpoint handler
    static bool register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data = nullptr) noexcept;

    // Announce Name Service endpoint to Linux Host (e.g. "rpmsg-ping-channel")
    static bool announce_service(const char *name, uint32_t addr) noexcept;

    // Poll for incoming RPMsg packets and dispatch to registered callbacks
    static bool poll() noexcept;

    // Reply to an incoming RPMsg packet (Echo / Pong)
    static bool reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept;

    // Telemetry Statistics
    static uint32_t get_rx_count() noexcept;
    static uint32_t get_tx_count() noexcept;

    struct EndpointEntry {
        uint32_t addr;
        EndpointCallback cb;
        void *user_data;
    };

private:
    struct VirtQueueState {
        uint32_t da;
        uint32_t num;
        uint32_t align;
        volatile struct fw_rsc_vdev_vring *rsc_vring;
        uint16_t last_avail_idx;
    };

    static void init_vqueues() noexcept;
};

} // namespace hal

