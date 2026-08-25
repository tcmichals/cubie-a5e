#ifndef IOPROCESSOR_HAL_ISR_DISPATCHER_HPP
#define IOPROCESSOR_HAL_ISR_DISPATCHER_HPP

#include <coroutine>
#include <cstdint>
#include <atomic>

namespace fc::hal {

/*
 * Lock-Free, Wait-Free SPSC Coroutine Resume Dispatcher
 * 
 * Top-Half ISR (Producer) -> posts coroutine handles without locking or heap allocation.
 * Main Thread Loop (Consumer) -> drains the queue and resumes tasks safely on the main stack.
 */
class IsrDispatcher {
public:
    static constexpr size_t QUEUE_CAPACITY = 32; // Power of 2
    static constexpr size_t QUEUE_MASK = QUEUE_CAPACITY - 1;

    // Called strictly from ISR (Top-Half)
    static inline bool isr_post_resume(std::coroutine_handle<> handle) noexcept {
        if (!handle || handle.address() == nullptr) {
            return false;
        }

        uint32_t head = head_.load(std::memory_order_relaxed);
        uint32_t tail = tail_.load(std::memory_order_acquire);

        if ((head - tail) >= QUEUE_CAPACITY) {
            return false; // Queue full - dropped to protect memory
        }

        buffer_[head & QUEUE_MASK] = handle;
        head_.store(head + 1, std::memory_order_release);

        // Trigger Machine Software Interrupt (MSIP) to wake CPU from 'wfi' if sleeping
#if defined(__riscv)
        __asm__ volatile("fence rw, rw" ::: "memory");
        __asm__ volatile("csrs mip, %0" :: "r"(1 << 3));
#endif
        return true;
    }

    // Called strictly from Main Thread Scheduler Loop (Bottom-Half)
    static inline void process_ready_coroutines() noexcept {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        uint32_t head = head_.load(std::memory_order_acquire);

        while (tail != head) {
            std::coroutine_handle<> handle = buffer_[tail & QUEUE_MASK];
            tail++;
            tail_.store(tail, std::memory_order_release);

            // Safe resume check
            if (handle && handle.address() != nullptr && !handle.done()) {
                handle.resume();
            }

            head = head_.load(std::memory_order_acquire);
        }
    }

    static inline bool has_pending_resumes() noexcept {
        return head_.load(std::memory_order_relaxed) != tail_.load(std::memory_order_relaxed);
    }

    static inline void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    static inline std::coroutine_handle<> buffer_[QUEUE_CAPACITY];
    static inline std::atomic<uint32_t> head_{0};
    static inline std::atomic<uint32_t> tail_{0};
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_ISR_DISPATCHER_HPP
