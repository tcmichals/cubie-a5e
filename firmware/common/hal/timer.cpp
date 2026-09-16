#include "timer.hpp"

namespace hal {

void Timer::init() noexcept {
    // E907 Machine-mode hardware counters (mcycle/minstret) run automatically.
    // Avoid writing optional CSRs (0x320 mcountinhibit, 0x306 mcounteren)
    // which trigger Illegal Instruction exceptions on XuanTie cores.
}

uint64_t Timer::get_ticks() noexcept {
#if defined(__riscv)
    // Atomic 64-bit cycle counter read using hardware mcycle CSRs
    uint32_t hi0 = 0, lo = 0, hi1 = 0;
    do {
        asm volatile (
            "csrr %0, mcycleh\n"
            "csrr %1, mcycle\n"
            "csrr %2, mcycleh\n"
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