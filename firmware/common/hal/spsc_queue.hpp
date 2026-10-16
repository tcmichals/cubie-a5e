#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <type_traits>
#include <utility>

namespace hal {

/*
 * Lock-Free Single Producer Single Consumer (SPSC) Ring Queue
 *
 * Designed for intra-core and RTOS task/ISR communication (FreeRTOS, coroutines, drivers).
 * Utilizes standard C++ std::atomic with acquire/release memory semantics.
 *
 * Performance features:
 *  - Power-of-two capacity: single-cycle bitwise masking without division.
 *  - Shadow pointer caching: producer caches tail, consumer caches head to eliminate
 *    redundant atomic loads when the queue is neither full nor empty.
 *  - No cache-line padding: both ends run on one core (task <-> task or
 *    ISR -> task), so there is no false sharing to avoid and padding would
 *    only cost SRAM. Cross-core links use ipc::SpscRing instead.
 */
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "SpscQueue Capacity must be a power of two >= 2");

public:
    constexpr SpscQueue() noexcept : head_(0), cached_tail_(0), tail_(0), cached_head_(0) {}

    // Producer enqueue (copy or move)
    template <typename U>
    bool push(U&& item) noexcept {
        const uint32_t current_head = head_.load(std::memory_order_relaxed);
        if ((current_head - cached_tail_) >= Capacity) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if ((current_head - cached_tail_) >= Capacity) {
                return false; // Queue is full
            }
        }

        buffer_[current_head & (Capacity - 1)] = std::forward<U>(item);
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // Consumer dequeue
    bool pop(T& item) noexcept {
        const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return false; // Queue is empty
            }
        }

        item = std::move(buffer_[current_tail & (Capacity - 1)]);
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Peek at next item without consuming
    bool peek(T& item) const noexcept {
        const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return false;
            }
        }

        item = buffer_[current_tail & (Capacity - 1)];
        return true;
    }

    bool is_empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    bool empty() const noexcept {
        return is_empty();
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
        cached_head_ = 0;
        cached_tail_ = 0;
    }

    void clear() noexcept {
        reset();
    }

private:
    std::atomic<uint32_t> head_;     // written by the producer
    uint32_t cached_tail_;           // producer's copy of tail_
    std::atomic<uint32_t> tail_;     // written by the consumer
    mutable uint32_t cached_head_;   // consumer's copy of head_
    T buffer_[Capacity];
};

} // namespace hal
