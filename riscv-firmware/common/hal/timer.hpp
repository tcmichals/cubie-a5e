#ifndef IOPROCESSOR_HAL_TIMER_HPP
#define IOPROCESSOR_HAL_TIMER_HPP

#include <stdint.h>
#include <coroutine>
#include "memory_map.h"
#include <abstractx/coro.hpp>

#if !defined(__riscv)
#include <chrono>
#endif

namespace fc::hal {

class Timer {
public:
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

    /* Initialize AbstractX Timer Wheel */
    static inline void init() {
        abstractx::TimerService::init();
    }

    // Called from hardware timer ISR (e.g. 1 kHz / 1 ms tick in startup.S)
    static inline void handle_tick_irq(uint32_t count = 1) {
        abstractx::TimerService::tick(count);
    }

    // Asynchronous coroutine sleep delegating to AbstractX
    static inline auto async_sleep_ms(uint32_t ms) {
        return abstractx::sleep_ms(ms);
    }
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_TIMER_HPP
