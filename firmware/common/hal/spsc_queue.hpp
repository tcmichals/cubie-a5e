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
 * ============================================================================
 * AbstractX Architectural Model: Performance & Concurrency Principles
 * ============================================================================
 *
 * 1. ZERO-MUTEX / ZERO-CRITICAL-SECTION DESIGN:
 *    - SPSC queues are strictly point-to-point (one writer thread, one reader thread).
 *    - Never wrap this queue in global mutexes or disable interrupts (`taskENTER_CRITICAL`).
 *    - To handle Multiple Producers (MPSC):
 *      Use the "AbstractX Dispatcher / Serializer" pattern: each worker task maintains
 *      its own private, lock-free SPSC queue feeding a central Router/Gateway task.
 *      This completely eliminates lock contention, priority inversion, and deadlocks.
 *
 * 2. EVENT-DRIVEN DOORBELL / WAKE-UP PIPELINE:
 *    - Decouple the Data Plane from the Signaling Plane:
 *        * Data Plane: Carried lock-free inside this SPSC queue (or ipc::SpscRing).
 *        * Signal Plane: Light "doorbell kick" via Hardware MsgBox or Task Notification.
 *    - In FreeRTOS:
 *        * Consumer sleeps on zero-overhead Direct Task Notifications:
 *            ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // 0% CPU burn
 *        * Producer or MsgBox PLIC ISR wakes consumer without OS locks:
 *            vTaskNotifyGiveFromISR(consumer_task_handle, &xHigherPriorityTaskWoken);
 *        * Consumer wakes up and drains all available items in a tight loop:
 *            while (queue.pop(item)) { process(item); }
 *
 * 3. HARDWARE & CACHE EFFICIENCY:
 *    - Power-of-two capacity: single-cycle bitwise masking (`head & (Capacity - 1)`).
 *    - Monotonic 32-bit wrap-around: arithmetic naturally wraps cleanly at 2^32.
 *    - Shadow pointer caching: producer caches tail, consumer caches head to eliminate
 *      redundant atomic bus loads when the queue is neither full nor empty.
 *    - No cache-line padding: both ends run on one core (task <-> task or ISR -> task),
 *      so there is no inter-core false sharing to avoid and padding would waste SRAM.
 *      (Cross-core links between Linux ARM64 and RV32 use ipc::SpscRing instead).
 * ============================================================================
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
