#include <cstdint>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Panic & Default Fallback Handlers (ITCM)
// -----------------------------------------------------------------------------
__attribute__((section(".fastcode"), noinline))
static void fatal_exception_panic(uint32_t mcause, uint32_t mepc, uint32_t mtval) noexcept {
    (void)mcause; (void)mepc; (void)mtval;
    while (true) {
        __asm__ volatile("wfi");
    }
}

__attribute__((section(".fastcode"), noinline))
static void fatal_unhandled_irq_panic(uint32_t irq_id) noexcept {
    (void)irq_id;
    while (true) {
        __asm__ volatile("wfi");
    }
}

// Target function with explicit noexcept
extern "C" __attribute__((section(".fastcode")))
void default_isr_ignore() noexcept {
    // No-op return
}

// -----------------------------------------------------------------------------
// Weak C-Linkage ISR Declarations (Matching noexcept / nothrow attributes)
// -----------------------------------------------------------------------------
extern "C" {
    void fc_msgbox_doorbell_isr() noexcept __attribute__((weak, alias("default_isr_ignore")));
    void fc_spi1_isr()            noexcept __attribute__((weak, alias("default_isr_ignore")));
    void fc_uart0_isr()           noexcept __attribute__((weak, alias("default_isr_ignore")));
    void fc_dma_isr()             noexcept __attribute__((weak, alias("default_isr_ignore")));
    void fc_gpio_drdy_isr()       noexcept __attribute__((weak, alias("default_isr_ignore")));
    void fc_timer_tick_isr()      noexcept __attribute__((weak, alias("default_isr_ignore")));
}

// -----------------------------------------------------------------------------
// Hardware IRQ IDs & PLIC Registers
// -----------------------------------------------------------------------------
namespace irq_id {
    constexpr uint32_t MSGBOX_E907 = 48; // Hardware Doorbell
    constexpr uint32_t SPI1        = 54; // Primary Sensor SPI
    constexpr uint32_t UART0       = 34; // Debug Console
    constexpr uint32_t DMA_E907    = 64; // Dedicated DMA Engine
    constexpr uint32_t GPIO_DRDY   = 82; // IMU Data-Ready Pin
}

namespace plic {
    // E907 Core Context 0 Claim/Complete Register on T527
    inline volatile uint32_t& claim_complete() noexcept {
        return *reinterpret_cast<volatile uint32_t*>(0x10000000 + 0x200004);
    }
}

// -----------------------------------------------------------------------------
// Hardcoded PLIC External Dispatcher (ITCM)
// -----------------------------------------------------------------------------
__attribute__((section(".fastcode")))
static inline void dispatch_external_plic() noexcept {
    volatile uint32_t& plic_reg = plic::claim_complete();
    uint32_t irq = plic_reg;

    while (irq != 0) {
        switch (irq) {
            case irq_id::MSGBOX_E907: fc_msgbox_doorbell_isr(); break;
            case irq_id::SPI1:        fc_spi1_isr();            break;
            case irq_id::UART0:       fc_uart0_isr();           break;
            case irq_id::DMA_E907:    fc_dma_isr();             break;
            case irq_id::GPIO_DRDY:   fc_gpio_drdy_isr();       break;

            default:
                fatal_unhandled_irq_panic(irq);
                break;
        }

        plic_reg = irq; // Signal completion to hardware
        irq = plic_reg; // Check for next pending interrupt
    }
}

// -----------------------------------------------------------------------------
// Top-Level Trap Entry (Called directly by default_trap_entry in startup.S)
// -----------------------------------------------------------------------------
extern "C" __attribute__((section(".fastcode")))
void riscv_trap_dispatcher(uint32_t mcause, uint32_t mepc) noexcept {
    constexpr uint32_t INTERRUPT_FLAG = 0x80000000;

    // Asynchronous Interrupts (Bit 31 set)
    if (__builtin_expect((mcause & INTERRUPT_FLAG) != 0, 1)) {
        uint32_t irq_type = mcause & 0x7FFFFFFF;

        switch (irq_type) {
            case 3:  // Machine Software Interrupt (MSIP Doorbell fallback)
                fc_msgbox_doorbell_isr();
                break;

            case 7:  // Machine Timer Interrupt (MTIP / Core Timer)
                fc_timer_tick_isr();
                break;

            case 11: // Machine External Interrupt (MEIP / PLIC)
                dispatch_external_plic();
                break;

            default:
                fatal_unhandled_irq_panic(irq_type);
                break;
        }
    } 
    // Synchronous Traps / CPU Exceptions (Bit 31 clear)
    else {
        uint32_t mtval;
        __asm__ volatile("csrr %0, mtval" : "=r"(mtval));
        fatal_exception_panic(mcause, mepc, mtval);
    }
}