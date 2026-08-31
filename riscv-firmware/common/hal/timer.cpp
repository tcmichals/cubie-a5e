#include "timer.hpp"

namespace hal {

void Timer::init() noexcept {
    // Disable timer interrupts at boot by setting compare to maximum value
    set_compare(UINT64_MAX);
}

uint64_t Timer::get_ticks() noexcept {
#if defined(__riscv)
    // Atomic 64-bit counter read for 32-bit RISC-V using hardware time CSRs
    uint32_t hi0 = 0, lo = 0, hi1 = 0;
    do {
        asm volatile (
            "rdtimeh %0\n"
            "rdtime  %1\n"
            "rdtimeh %2\n"
            : "=r"(hi0), "=r"(lo), "=r"(hi1)
        );
    } while (hi0 != hi1);

    return (static_cast<uint64_t>(hi0) << 32) | lo;
#else
    // Fallback: Direct MMIO read from CLINT MTIME register
    auto *mtime_lo = reinterpret_cast<volatile uint32_t *>(MTIME_REG);
    auto *mtime_hi = reinterpret_cast<volatile uint32_t *>(MTIME_REG + 4);
    uint32_t hi0 = 0, lo = 0, hi1 = 0;
    do {
        hi0 = *mtime_hi;
        lo  = *mtime_lo;
        hi1 = *mtime_hi;
    } while (hi0 != hi1);

    return (static_cast<uint64_t>(hi0) << 32) | lo;
#endif
}

void Timer::set_compare(uint64_t target_ticks) noexcept {
    auto *mtimecmp_lo = reinterpret_cast<volatile uint32_t *>(MTIMECMP_REG);
    auto *mtimecmp_hi = reinterpret_cast<volatile uint32_t *>(MTIMECMP_REG + 4);

    // Guard against spurious match during 32-bit word split writes:
    // 1. Invalidate high word by setting to maximum
    // 2. Write lower 32 bits
    // 3. Write actual upper 32 bits
    *mtimecmp_hi = 0xFFFFFFFFUL;
    *mtimecmp_lo = static_cast<uint32_t>(target_ticks & 0xFFFFFFFFUL);
    *mtimecmp_hi = static_cast<uint32_t>(target_ticks >> 32);
}

void Timer::set_alarm_us(uint32_t us_from_now) noexcept {
    set_compare(get_ticks() + (static_cast<uint64_t>(us_from_now) * TICKS_PER_US));
}

void Timer::delay_ticks(uint64_t ticks) noexcept {
    uint64_t start = get_ticks();
    while ((get_ticks() - start) < ticks) {
#if defined(__riscv)
        asm volatile ("nop");
#endif
    }
}

void Timer::delay_us(uint32_t us) noexcept {
    delay_ticks(static_cast<uint64_t>(us) * TICKS_PER_US);
}

void Timer::delay_ms(uint32_t ms) noexcept {
    delay_us(ms * 1000);
}

} // namespace hal