#ifndef FLIGHT_BRIDGE_IPC_BRIDGE_HPP
#define FLIGHT_BRIDGE_IPC_BRIDGE_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../include/ipc_protocol.hpp"

namespace bridge {

class IpcBridge {
public:
    IpcBridge();
    ~IpcBridge();

    bool open_shm(uintptr_t phys_addr = 0x07130000, size_t size = 0x20000);
    void close_shm();

    /* Message Ingestion: Read from RISC-V TX Queue */
    bool poll_rx_packet(fc::ipc::IpcPacket &packet);

    /* Message Transmission: Write to RISC-V RX Queue */
    bool send_tx_packet(const fc::ipc::IpcPacket &packet);

    /* Direct Crash Dump Check */
    bool check_crash_dump(uint32_t &mepc, uint32_t &mcause, uint32_t &mtval);

private:
    int mem_fd_;
    void *mapped_base_;
    size_t mapped_size_;
    volatile uint8_t *rx_ring_ptr_; // RISC-V TX ring (Linux RX)
    volatile uint8_t *tx_ring_ptr_; // Linux TX ring (RISC-V RX)
};

} // namespace bridge

#endif // FLIGHT_BRIDGE_IPC_BRIDGE_HPP
