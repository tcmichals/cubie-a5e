#ifndef IOPROCESSOR_IPC_RINGBUFFER_HPP
#define IOPROCESSOR_IPC_RINGBUFFER_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ipc_protocol.hpp"
#include "../hal/msgbox.hpp"

namespace fc::ipc {

constexpr size_t RING_CAPACITY = 120; // 120 * 128B = 15,360 Bytes

struct __attribute__((packed, aligned(4))) RingBufferLayout {
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t capacity;
    volatile uint32_t dropped;
    IpcPacket slots[RING_CAPACITY];
};

class SpscRingBuffer {
public:
    explicit SpscRingBuffer(uintptr_t base_address)
        : ring_(reinterpret_cast<volatile RingBufferLayout *>(base_address)) {}

    void init() {
        ring_->head = 0;
        ring_->tail = 0;
        ring_->capacity = RING_CAPACITY;
        ring_->dropped = 0;
    }

    bool is_full() const {
        uint32_t head = ring_->head;
        uint32_t next_head = (head + 1) % RING_CAPACITY;
        return (next_head == ring_->tail);
    }

    bool is_empty() const {
        return (ring_->head == ring_->tail);
    }

    bool push(const IpcPacket &packet, bool notify_doorbell = true) {
        uint32_t head = ring_->head;
        uint32_t next_head = (head + 1) % RING_CAPACITY;

        if (next_head == ring_->tail) {
            ring_->dropped = ring_->dropped + 1;
            return false; // Queue Full
        }

        // Copy packet into slot
        volatile uint32_t *dst = reinterpret_cast<volatile uint32_t *>(&ring_->slots[head]);
        const uint32_t *src = reinterpret_cast<const uint32_t *>(&packet);
        for (size_t i = 0; i < sizeof(IpcPacket) / 4; ++i) {
            dst[i] = src[i];
        }

        // Memory fence before updating head pointer
#if defined(__riscv)
        __asm__ volatile("fence rw, rw" ::: "memory");
#elif defined(__aarch64__)
        __asm__ volatile("dmb ish" ::: "memory");
#else
        __asm__ volatile("" ::: "memory");
#endif
        ring_->head = next_head;

        if (notify_doorbell) {
            hal::MsgBox::notify_host(0x01);
        }
        return true;
    }

    bool pop(IpcPacket &packet) {
        uint32_t tail = ring_->tail;
        if (tail == ring_->head) {
            return false; // Queue Empty
        }

        // Copy packet out of slot
        const volatile uint32_t *src = reinterpret_cast<const volatile uint32_t *>(&ring_->slots[tail]);
        uint32_t *dst = reinterpret_cast<uint32_t *>(&packet);
        for (size_t i = 0; i < sizeof(IpcPacket) / 4; ++i) {
            dst[i] = src[i];
        }

        // Memory fence before updating tail pointer
#if defined(__riscv)
        __asm__ volatile("fence rw, rw" ::: "memory");
#elif defined(__aarch64__)
        __asm__ volatile("dmb ish" ::: "memory");
#else
        __asm__ volatile("" ::: "memory");
#endif
        ring_->tail = (tail + 1) % RING_CAPACITY;
        return true;
    }

private:
    volatile RingBufferLayout *ring_;
};

} // namespace fc::ipc

#endif // IOPROCESSOR_IPC_RINGBUFFER_HPP
