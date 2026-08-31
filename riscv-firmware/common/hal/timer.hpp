#pragma once

#include <stdint.h>

namespace hal {

class Timer {
public:
    static constexpr uint32_t TICKS_PER_US = 24; // 24MHz counter
    static constexpr uintptr_t MTIME_REG = 0x07090000;
    static constexpr uintptr_t MTIMECMP_REG = 0x07090008;

    static void init() noexcept;
    static uint64_t get_ticks() noexcept;
    static void set_compare(uint64_t target_ticks) noexcept;
    static void set_alarm_us(uint32_t us_from_now) noexcept;
    static void delay_ticks(uint64_t ticks) noexcept;
    static void delay_us(uint32_t us) noexcept;
    static void delay_ms(uint32_t ms) noexcept;
};

} // namespace hal