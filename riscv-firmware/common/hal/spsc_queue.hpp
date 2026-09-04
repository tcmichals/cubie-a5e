#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <type_traits>

namespace hal {

/*
 * Lock-Free Single Producer Single Consumer (SPSC) Ring Queue
 *
 * Utilizes standard C++ std::atomic with acquire/release memory semantics.
 * Safe for cross-core (ARM64 <-> RISC-V) and intra-core lock-free communication.
 */
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "SpscQueue Capacity must be a power of two");

public:
    constexpr SpscQueue() noexcept : head_(0), tail_(0) {}

    // Producer enqueue
    bool push(const T& item) noexcept {
        const uint32_t current_head = head_.load(std::memory_order_relaxed);
        const uint32_t current_tail = tail_.load(std::memory_order_acquire);

        if ((current_head - current_tail) >= Capacity) {
            return false; // Queue is full
        }

        buffer_[current_head & (Capacity - 1)] = item;
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // Consumer dequeue
    bool pop(T& item) noexcept {
        const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
        const uint32_t current_head = head_.load(std::memory_order_acquire);

        if (current_tail == current_head) {
            return false; // Queue is empty
        }

        item = buffer_[current_tail & (Capacity - 1)];
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Peek at next item without consuming
    bool peek(T& item) const noexcept {
        const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
        const uint32_t current_head = head_.load(std::memory_order_acquire);

        if (current_tail == current_head) {
            return false;
        }

        item = buffer_[current_tail & (Capacity - 1)];
        return true;
    }

    bool is_empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    bool is_full() const noexcept {
        return (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed)) >= Capacity;
    }

    size_t size() const noexcept {
        return head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed);
    }

    constexpr size_t capacity() const noexcept {
        return Capacity;
    }

    void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<uint32_t> head_;
    alignas(64) std::atomic<uint32_t> tail_;
    T buffer_[Capacity];
};

} // namespace hal
