#include "ipc_bridge.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <iostream>
#include <cstring>

namespace bridge {

constexpr size_t RING_CAPACITY = 120;

struct __attribute__((packed, aligned(4))) RingLayout {
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t capacity;
    volatile uint32_t dropped;
    fc::ipc::IpcPacket slots[RING_CAPACITY];
};

IpcBridge::IpcBridge()
    : mem_fd_(-1), mapped_base_(nullptr), mapped_size_(0), rx_ring_ptr_(nullptr), tx_ring_ptr_(nullptr) {}

IpcBridge::~IpcBridge() {
    close_shm();
}

bool IpcBridge::open_shm(uintptr_t phys_addr, size_t size) {
    mem_fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd_ < 0) {
        std::cerr << "[IPC Bridge] Failed to open /dev/mem (requires root/sudo)\n";
        return false;
    }

    mapped_size_ = size;
    mapped_base_ = mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd_, phys_addr);
    if (mapped_base_ == MAP_FAILED) {
        std::cerr << "[IPC Bridge] Failed to mmap physical SRAM address " << std::hex << phys_addr << "\n";
        close(mem_fd_);
        mem_fd_ = -1;
        return false;
    }

    uint8_t *base = static_cast<uint8_t *>(mapped_base_);
    rx_ring_ptr_ = base + 0x0100; // RISC-V TX Ring
    tx_ring_ptr_ = base + 0x4100; // Linux TX Ring

    std::cout << "[IPC Bridge] Successfully mapped Shared SRAM C @ 0x" << std::hex << phys_addr << std::dec << "\n";
    return true;
}

void IpcBridge::close_shm() {
    if (mapped_base_ && mapped_base_ != MAP_FAILED) {
        munmap(mapped_base_, mapped_size_);
        mapped_base_ = nullptr;
    }
    if (mem_fd_ >= 0) {
        close(mem_fd_);
        mem_fd_ = -1;
    }
}

bool IpcBridge::poll_rx_packet(fc::ipc::IpcPacket &packet) {
    if (!rx_ring_ptr_) return false;

    volatile RingLayout *ring = reinterpret_cast<volatile RingLayout *>(rx_ring_ptr_);
    uint32_t tail = ring->tail;
    if (tail == ring->head) {
        return false; // Empty
    }

    // Read packet from slot
    const volatile uint32_t *src = reinterpret_cast<const volatile uint32_t *>(&ring->slots[tail]);
    uint32_t *dst = reinterpret_cast<uint32_t *>(&packet);
    for (size_t i = 0; i < sizeof(fc::ipc::IpcPacket) / 4; ++i) {
        dst[i] = src[i];
    }

    __sync_synchronize();
    ring->tail = (tail + 1) % RING_CAPACITY;
    return true;
}

bool IpcBridge::send_tx_packet(const fc::ipc::IpcPacket &packet) {
    if (!tx_ring_ptr_) return false;

    volatile RingLayout *ring = reinterpret_cast<volatile RingLayout *>(tx_ring_ptr_);
    uint32_t head = ring->head;
    uint32_t next_head = (head + 1) % RING_CAPACITY;
    if (next_head == ring->tail) {
        ring->dropped = ring->dropped + 1;
        return false; // Queue Full
    }

    volatile uint32_t *dst = reinterpret_cast<volatile uint32_t *>(&ring->slots[head]);
    const uint32_t *src = reinterpret_cast<const uint32_t *>(&packet);
    for (size_t i = 0; i < sizeof(fc::ipc::IpcPacket) / 4; ++i) {
        dst[i] = src[i];
    }

    __sync_synchronize();
    ring->head = next_head;
    return true;
}

bool IpcBridge::check_crash_dump(uint32_t &mepc, uint32_t &mcause, uint32_t &mtval) {
    if (!mapped_base_) return false;

    const volatile uint32_t *dump = static_cast<const volatile uint32_t *>(mapped_base_);
    if (dump[0] == 0x48535243) { // Magic: 'CRSH'
        mepc = dump[1];
        mcause = dump[2];
        mtval = dump[3];
        return true;
    }
    return false;
}

} // namespace bridge
