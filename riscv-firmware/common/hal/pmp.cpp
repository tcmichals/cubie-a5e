#include "pmp.hpp"

namespace hal {

void Pmp::init() noexcept {
#if defined(__riscv)
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
    // Setup PMP permission for the DDR carveout
    set_napot_entry(1, dram_base, dram_size, PmpFlags::Read | PmpFlags::Write);

    // Flush any stale cache lines for the DRAM range
    dcache_invalidate_range(dram_base, dram_size);
    memory_fence();
#else
    (void)dram_base; (void)dram_size;
#endif
}

void Pmp::dcache_clean_range(uintptr_t addr, size_t len) noexcept {
#if defined(__riscv)
    // XuanTie custom cache maintenance or line-by-line flush (32-byte cache line)
    uintptr_t line_addr = addr & ~0x1FUL;
    uintptr_t end_addr  = addr + len;
    while (line_addr < end_addr) {
        // XuanTie dcache.cpa: clean by physical address (opcode: .insn r 0x0b, 0, 0x19, x0, rs1, x0)
        asm volatile (
            ".insn r 0x0b, 0, 0x19, x0, %0, x0\n"
            :: "r"(line_addr) : "memory"
        );
        line_addr += 32;
    }
    memory_fence();
#else
    (void)addr; (void)len;
#endif
}

void Pmp::dcache_invalidate_range(uintptr_t addr, size_t len) noexcept {
#if defined(__riscv)
    // XuanTie custom cache maintenance: invalidate by physical address
    uintptr_t line_addr = addr & ~0x1FUL;
    uintptr_t end_addr  = addr + len;
    while (line_addr < end_addr) {
        // XuanTie dcache.iva: invalidate by physical address (opcode: .insn r 0x0b, 0, 0x18, x0, rs1, x0)
        asm volatile (
            ".insn r 0x0b, 0, 0x18, x0, %0, x0\n"
            :: "r"(line_addr) : "memory"
        );
        line_addr += 32;
    }
    memory_fence();
#else
    (void)addr; (void)len;
#endif
}

void Pmp::dcache_flush_all() noexcept {
#if defined(__riscv)
    // XuanTie mcor CSR (0x7C2): Bit 6 = Clean & Invalidate all D-Cache
    asm volatile (
        "csrw 0x7C2, %0\n"
        :: "r"(1 << 6) : "memory"
    );
    memory_fence();
#endif
}

} // namespace hal
