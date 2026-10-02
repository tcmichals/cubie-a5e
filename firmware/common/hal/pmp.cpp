#include "pmp.hpp"

namespace hal {

void Pmp::init() noexcept {
#if defined(__riscv)
    // NOTE: PMP only configures basic R/W/X permissions in Machine mode.
    // It CANNOT configure cacheability or carve out non-cacheable regions.
    // D-Cache is intentionally kept DISABLED (mhcr.DE = 0) so all memory
    // accesses bypass cache and remain coherent with the Linux host.
    // AbstractX will implement proper architecture-level cache management.

    // 1. Initial barrier
    memory_fence();

    // 2. Default: Enable all physical memory access for Machine mode
    // Entry 0: Map all 4GB space as RWX using NAPOT
    uint32_t pmpaddr_all = 0x3FFFFFFF; // Covers 0x00000000 to 0xFFFFFFFF
    asm volatile ("csrw pmpaddr0, %0" :: "r"(pmpaddr_all));

    // pmpcfg0: Entry 0 = NAPOT (0x18) | RWX (0x07) = 0x1F
    uint32_t pmpcfg = (PmpFlags::ModeNapot | PmpFlags::RWX);
    asm volatile ("csrw pmpcfg0, %0" :: "r"(pmpcfg));

    instruction_fence();
    memory_fence();
#endif
}

void Pmp::set_tor_entry(uint32_t entry_idx, uintptr_t base_addr, uintptr_t end_addr, uint8_t flags) noexcept {
#if defined(__riscv)
    if (entry_idx == 1) {
        uint32_t addr0 = static_cast<uint32_t>(base_addr >> 2);
        uint32_t addr1 = static_cast<uint32_t>(end_addr >> 2);
        asm volatile ("csrw pmpaddr0, %0" :: "r"(addr0));
        asm volatile ("csrw pmpaddr1, %0" :: "r"(addr1));

        uint32_t cfg = (PmpFlags::ModeTor | (flags & PmpFlags::RWX)) << 8;
        asm volatile ("csrw pmpcfg0, %0" :: "r"(cfg));
    }
    instruction_fence();
    memory_fence();
#else
    (void)entry_idx; (void)base_addr; (void)end_addr; (void)flags;
#endif
}

void Pmp::set_napot_entry(uint32_t entry_idx, uintptr_t base_addr, size_t size, uint8_t flags) noexcept {
#if defined(__riscv)
    if (size >= 8 && (size & (size - 1)) == 0) {
        uintptr_t napot_addr = (base_addr >> 2) | ((size >> 3) - 1);
        if (entry_idx == 0) {
            asm volatile ("csrw pmpaddr0, %0" :: "r"(napot_addr));
            uint32_t cfg = (PmpFlags::ModeNapot | (flags & PmpFlags::RWX));
            asm volatile ("csrw pmpcfg0, %0" :: "r"(cfg));
        } else if (entry_idx == 1) {
            asm volatile ("csrw pmpaddr1, %0" :: "r"(napot_addr));
            uint32_t cfg;
            asm volatile ("csrr %0, pmpcfg0" : "=r"(cfg));
            cfg = (cfg & 0x00FF) | ((PmpFlags::ModeNapot | (flags & PmpFlags::RWX)) << 8);
            asm volatile ("csrw pmpcfg0, %0" :: "r"(cfg));
        }
    }
    instruction_fence();
    memory_fence();
#else
    (void)entry_idx; (void)base_addr; (void)size; (void)flags;
#endif
}

void Pmp::configure_dram_carveout(uintptr_t dram_base, size_t dram_size) noexcept {
#if defined(__riscv)
    // NOTE: This PMP entry sets Read/Write permissions only.
    // It CANNOT make the DRAM region non-cacheable. Memory is coherent
    // only because D-Cache is kept disabled (mhcr.DE = 0).
    // AbstractX will implement proper architecture-level cache maintenance.
    set_napot_entry(1, dram_base, dram_size, PmpFlags::Read | PmpFlags::Write);

    // No-op while D-Cache is disabled; preserved for API compatibility
    dcache_invalidate_range(dram_base, dram_size);
    memory_fence();
#else
    (void)dram_base; (void)dram_size;
#endif
}

/*
 * NOTE on Cache Operations:
 * In this bare-metal firmware, D-Cache is intentionally disabled (DE = 0),
 * so dcache_clean_range and dcache_invalidate_range are no-ops (fences only).
 * If D-Cache were enabled (DE = 1), custom T-Head cache instructions
 * (dcache.cpa, dcache.iva) would be required, which additionally require
 * setting CSR_MXSTATUS bit 22 (THEADISAEE = 1) to avoid illegal instructions.
 * Do not remove these functions; AbstractX will implement the complete
 * high-performance cache architecture.
 */
void Pmp::dcache_clean_range(uintptr_t addr, size_t len) noexcept {
    (void)addr; (void)len;
    memory_fence();
}

void Pmp::dcache_invalidate_range(uintptr_t addr, size_t len) noexcept {
    (void)addr; (void)len;
    memory_fence();
}

void Pmp::dcache_flush_all() noexcept {
#if defined(__riscv)
    // XuanTie mcor CSR (0x7C2): Bit 6 = Clean & Invalidate all D-Cache
    // (Operates when D-Cache is enabled; harmless fence when disabled)
    asm volatile (
        "csrw 0x7C2, %0\n"
        :: "r"(1 << 6) : "memory"
    );
    memory_fence();
#endif
}

} // namespace hal
