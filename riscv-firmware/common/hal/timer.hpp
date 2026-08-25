#ifndef IOPROCESSOR_HAL_TIMER_HPP
#define IOPROCESSOR_HAL_TIMER_HPP

#include <stdint.h>
#include "../../include/memory_map.h"

namespace fc::hal {

class Timer {
public:
    static inline uint64_t get_cycles() {
        uint32_t high0, low, high1;
        do {
            __asm__ volatile("rdcycleh %0" : "=r"(high0));
            __asm__ volatile("rdcycle %0"  : "=r"(low));
            __asm__ volatile("rdcycleh %0" : "=r"(high1));
        } while (high0 != high1);
        return ((uint64_t)high0 << 32) | low;
    }

    static inline uint64_t get_time_us() {
        // 600 MHz clock -> 600 cycles per microsecond
        return get_cycles() / (CPU_FREQ_HZ / 1000000ULL);
    }

    static inline uint64_t get_time_ns() {
        // (cycles * 1000) / 600 -> (cycles * 5) / 3
        return (get_cycles() * 5ULL) / 3ULL;
    }

    static inline void delay_us(uint32_t us) {
        uint64_t target = get_cycles() + (uint64_t)us * (CPU_FREQ_HZ / 1000000ULL);
        while (get_cycles() < target) {
            __asm__ volatile("nop");
        }
    }

    static inline void delay_ms(uint32_t ms) {
        delay_us(ms * 1000);
    }
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_TIMER_HPP
