#pragma once

#include <stdint.h>
#include <stddef.h>

namespace hal {

/*
 * Standard RISC-V PMP Configuration Constants
 */
namespace PmpFlags {
    constexpr uint8_t None  = 0x00;
    constexpr uint8_t Read  = 0x01; // Bit 0
    constexpr uint8_t Write = 0x02; // Bit 1
    constexpr uint8_t Exec  = 0x04; // Bit 2
    constexpr uint8_t RWX   = 0x07;

    constexpr uint8_t ModeOff   = 0x00;        // Disabled
    constexpr uint8_t ModeTor   = (0x01 << 3); // Top of Range
    constexpr uint8_t ModeNa4   = (0x02 << 3); // Naturally Aligned 4-byte
    constexpr uint8_t ModeNapot = (0x03 << 3); // Naturally Aligned Power-of-Two
    constexpr uint8_t Lock      = (0x01 << 7); // Locked
}

class Pmp {
public:
    // Initialize PMP and Cache Subsystem
    static void init() noexcept;

    // Configure PMP Range (Top of Range)
    static void set_tor_entry(uint32_t entry_idx, uintptr_t base_addr, uintptr_t end_addr, uint8_t flags) noexcept;

    // Configure PMP Range (NAPOT)
    static void set_napot_entry(uint32_t entry_idx, uintptr_t base_addr, size_t size, uint8_t flags) noexcept;

    // Configure DDR DRAM carveout region for zero-copy DMA access
    static void configure_dram_carveout(uintptr_t dram_base, size_t dram_size) noexcept;

    // Memory and Pipeline Barriers
    static inline void memory_fence() noexcept {
        asm volatile ("fence rw, rw" ::: "memory");
    }

    static inline void instruction_fence() noexcept {
#if defined(__riscv)
        asm volatile ("fence.i" ::: "memory");
#endif
    }


    // XuanTie D-Cache Maintenance Primitives
    static void dcache_clean_range(uintptr_t addr, size_t len) noexcept;
    static void dcache_invalidate_range(uintptr_t addr, size_t len) noexcept;
    static void dcache_flush_all() noexcept;
};

} // namespace hal
