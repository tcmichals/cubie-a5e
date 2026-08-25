#ifndef IOPROCESSOR_HAL_TIMER_HPP
#define IOPROCESSOR_HAL_TIMER_HPP

#include <stdint.h>
#include "memory_map.h"

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
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_TIMER_HPP
