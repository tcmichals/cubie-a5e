#ifndef IOPROCESSOR_HAL_TIMER_HPP
#define IOPROCESSOR_HAL_TIMER_HPP

#include <stdint.h>
#include <coroutine>
#include <atomic>
#include <utility>
#include "memory_map.h"
#include "isr_dispatcher.hpp"
#include <etl/callback_timer_atomic.h>

#if !defined(__riscv)
#include <chrono>
#endif

namespace fc::hal {

class Timer {
public:
    static constexpr size_t MAX_TIMERS = 32;

    static inline uint64_t get_cycles() {
#if defined(__riscv)
        uint32_t high0, low, high1;
        do {
            __asm__ volatile("rdcycleh %0" : "=r"(high0));
            __asm__ volatile("rdcycle %0"  : "=r"(low));
            __asm__ volatile("rdcycleh %0" : "=r"(high1));
        } while (high0 != high1);
        return ((uint64_t)high0 << 32) | low;
#else
        static auto start = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count() * (CPU_FREQ_HZ / 1000000000ULL);
#endif
    }

    static inline uint64_t get_time_us() {
        return (get_cycles() * 1000000ULL) / CPU_FREQ_HZ;
    }

    static inline uint64_t get_time_ns() {
        return (get_cycles() * 1000000000ULL) / CPU_FREQ_HZ;
    }

    static inline void delay_us(uint32_t us) {
        uint64_t target = get_cycles() + (uint64_t)us * (CPU_FREQ_HZ / 1000000ULL);
        while (get_cycles() < target);
    }

    static inline void delay_ms(uint32_t ms) {
        delay_us(ms * 1000);
    }

    /* ETL Timer Framework Initialization & Tick Dispatcher */
    static inline void init() {
        timer_service_.enable(true);
    }

    // Called strictly from hardware timer ISR (e.g. 1 kHz / 1 ms tick in startup.S)
    static inline void handle_tick_irq(uint32_t count = 1) {
        timer_service_.tick(count);
    }

    /* Asynchronous Coroutine Sleep Awaiter (Zero-polling, ETL-timer-driven) */
    struct AsyncSleepAwaiter {
        uint32_t period_ms;
        etl::timer::id::type timer_id{etl::timer::id::NO_TIMER};
        std::coroutine_handle<> handle{nullptr};
        int8_t allocated_slot{-1};

        explicit AsyncSleepAwaiter(uint32_t ms) : period_ms(ms) {}

        bool await_ready() const noexcept {
            return (period_ms == 0);
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle = h;
            for (size_t i = 0; i < MAX_TIMERS; ++i) {
                if (!slot_active_[i]) {
                    slot_active_[i] = true;
                    slot_handles_[i] = handle;
                    allocated_slot = static_cast<int8_t>(i);
                    auto cb = get_callback(i);
                    timer_id = timer_service_.register_timer(cb, period_ms, etl::timer::mode::SINGLE_SHOT);
                    if (timer_id != etl::timer::id::NO_TIMER) {
                        timer_service_.start(timer_id);
                    }
                    break;
                }
            }
        }

        void await_resume() noexcept {
            if (timer_id != etl::timer::id::NO_TIMER) {
                timer_service_.unregister_timer(timer_id);
                timer_id = etl::timer::id::NO_TIMER;
            }
            if (allocated_slot >= 0 && allocated_slot < static_cast<int8_t>(MAX_TIMERS)) {
                slot_active_[allocated_slot] = false;
                allocated_slot = -1;
            }
        }
    };

    static inline AsyncSleepAwaiter async_sleep_ms(uint32_t ms) {
        return AsyncSleepAwaiter(ms);
    }

private:
    using TimerService = etl::callback_timer_atomic<MAX_TIMERS, std::atomic<int32_t>>;
    static inline TimerService timer_service_;
    static inline std::coroutine_handle<> slot_handles_[MAX_TIMERS];
    static inline bool slot_active_[MAX_TIMERS]{false};

    template <size_t Slot>
    static void timer_expired_slot() {
        std::coroutine_handle<> h = slot_handles_[Slot];
        slot_active_[Slot] = false;
        if (h) {
            IsrDispatcher::isr_post_resume(h);
        }
    }

    template <size_t... Is>
    static TimerService::callback_type get_callback_helper(size_t index, std::index_sequence<Is...>) {
        using CallbackFn = TimerService::callback_type;
        static const CallbackFn table[] = {
            CallbackFn::template create<timer_expired_slot<Is>>()...
        };
        return (index < sizeof...(Is)) ? table[index] : CallbackFn();
    }

    static TimerService::callback_type get_callback(size_t index) {
        return get_callback_helper(index, std::make_index_sequence<MAX_TIMERS>{});
    }
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_TIMER_HPP
