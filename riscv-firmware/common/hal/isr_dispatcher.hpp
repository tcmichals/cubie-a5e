#ifndef IOPROCESSOR_HAL_ISR_DISPATCHER_HPP
#define IOPROCESSOR_HAL_ISR_DISPATCHER_HPP

#include <coroutine>
#include <etl/circular_buffer.h>

namespace fc::hal {

/*
 * Thread-Safe Lock-Free ISR-to-Thread Coroutine Resume Dispatcher
 * 
 * ISR (Producer) pushes coroutine handles into the SPSC queue.
 * Main Thread (Consumer) drains the queue and resumes coroutines in thread context.
 */
class IsrDispatcher {
public:
    // Called strictly from ISR (Top-Half)
    static inline void isr_post_resume(std::coroutine_handle<> handle) {
        if (handle && handle.address() != nullptr) {
            ready_queue_.push(handle);
            // Trigger Machine Software Interrupt (MSIP) to wake CPU from 'wfi' if sleeping
#if defined(__riscv)
            __asm__ volatile("csrs mip, %0" :: "r"(1 << 3));
#endif
        }
    }

    // Called strictly from Main Thread Scheduler Loop (Bottom-Half)
    static inline void process_ready_coroutines() {
        while (!ready_queue_.empty()) {
            std::coroutine_handle<> handle = ready_queue_.front();
            ready_queue_.pop();
            if (handle && handle.address() != nullptr && !handle.done()) {
                handle.resume();
            }
        }
    }

    static inline bool has_pending_resumes() {
        return !ready_queue_.empty();
    }

private:
    static inline etl::circular_buffer<std::coroutine_handle<>, 32> ready_queue_;
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_ISR_DISPATCHER_HPP
