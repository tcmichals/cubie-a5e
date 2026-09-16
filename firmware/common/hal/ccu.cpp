#include "ccu.hpp"
#include "memory_map.h"
#include <atomic>

namespace hal {

/* ========================================================================= */
/* SoC CCU Register Offsets (Base: SUNXI_CCU_BASE = 0x02001000)              */
/* ========================================================================= */
#define CCU_UART_BGR_REG            (*(volatile uint32_t *)(SUNXI_CCU_BASE + 0x090C))
#define CCU_TWI_BGR_REG             (*(volatile uint32_t *)(SUNXI_CCU_BASE + 0x091C))
#define CCU_SPI_BGR_REG             (*(volatile uint32_t *)(SUNXI_CCU_BASE + 0x096C))
#define CCU_PWM_BGR_REG             (*(volatile uint32_t *)(SUNXI_CCU_BASE + 0x07AC))
#define CCU_LEDC_BGR_REG            (*(volatile uint32_t *)(SUNXI_CCU_BASE + 0x0BFC))
#define CCU_GPADC_BGR_REG           (*(volatile uint32_t *)(SUNXI_CCU_BASE + 0x09EC))

/* ========================================================================= */
/* MCU Local PRCM Register Offsets (Base: SUNXI_MCU_PRCM_BASE = 0x07102000)  */
/* ========================================================================= */
#define MCU_PRCM_TIMER_BGR_REG      (*(volatile uint32_t *)(SUNXI_MCU_PRCM_BASE + 0x004C))
#define MCU_PRCM_MSGBOX_BGR_REG     (*(volatile uint32_t *)(SUNXI_MCU_PRCM_BASE + 0x005C))
#define MCU_PRCM_DMA_BGR_REG        (*(volatile uint32_t *)(SUNXI_MCU_PRCM_BASE + 0x006C))

void Ccu::init() noexcept
{
    // Ensure all previous MMIO state writes are committed
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void Ccu::enable_module(BusModule mod) noexcept
{
    // Allwinner Rule: De-assert reset bit first (16 + n), then enable gating (n)
    switch (mod) {
    case BusModule::Uart0:
    case BusModule::Uart1:
    case BusModule::Uart2:
    case BusModule::Uart3:
    case BusModule::Uart4:
    case BusModule::Uart5:
    case BusModule::Uart6:
    case BusModule::Uart7: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Uart0);
        CCU_UART_BGR_REG |= (1U << (16 + idx)); // Deassert reset
        CCU_UART_BGR_REG |= (1U << idx);        // Enable clock gating
        break;
    }
    case BusModule::Twi0:
    case BusModule::Twi1:
    case BusModule::Twi2:
    case BusModule::Twi3:
    case BusModule::Twi4:
    case BusModule::Twi5: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Twi0);
        CCU_TWI_BGR_REG |= (1U << (16 + idx));
        CCU_TWI_BGR_REG |= (1U << idx);
        break;
    }
    case BusModule::Spi0:
    case BusModule::Spi1:
    case BusModule::Spi2: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Spi0);
        CCU_SPI_BGR_REG |= (1U << (16 + idx));
        CCU_SPI_BGR_REG |= (1U << idx);
        break;
    }
    case BusModule::Pwm:
        CCU_PWM_BGR_REG |= (1U << 16) | (1U << 17); // PWM0/PWM1 Resets
        CCU_PWM_BGR_REG |= (1U << 0)  | (1U << 1);  // PWM0/PWM1 Gating
        break;
    case BusModule::Ledc:
        CCU_LEDC_BGR_REG |= (1U << 16);
        CCU_LEDC_BGR_REG |= (1U << 0);
        break;
    case BusModule::Gpadc:
        CCU_GPADC_BGR_REG |= (1U << 16) | (1U << 17);
        CCU_GPADC_BGR_REG |= (1U << 0)  | (1U << 1);
        break;
    case BusModule::McuTimer:
        MCU_PRCM_TIMER_BGR_REG |= (1U << 16);
        MCU_PRCM_TIMER_BGR_REG |= (1U << 0);
        break;
    case BusModule::McuMsgBox:
        MCU_PRCM_MSGBOX_BGR_REG |= (1U << 16);
        MCU_PRCM_MSGBOX_BGR_REG |= (1U << 0);
        break;
    case BusModule::McuDma:
        MCU_PRCM_DMA_BGR_REG |= (1U << 16);
        MCU_PRCM_DMA_BGR_REG |= (1U << 0);
        break;
    }

    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void Ccu::disable_module(BusModule mod) noexcept
{
    // Order: Mask clock gating first, then assert reset
    switch (mod) {
    case BusModule::Uart0:
    case BusModule::Uart1:
    case BusModule::Uart2:
    case BusModule::Uart3:
    case BusModule::Uart4:
    case BusModule::Uart5:
    case BusModule::Uart6:
    case BusModule::Uart7: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Uart0);
        CCU_UART_BGR_REG &= ~(1U << idx);
        CCU_UART_BGR_REG &= ~(1U << (16 + idx));
        break;
    }
    case BusModule::Twi0:
    case BusModule::Twi1:
    case BusModule::Twi2:
    case BusModule::Twi3:
    case BusModule::Twi4:
    case BusModule::Twi5: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Twi0);
        CCU_TWI_BGR_REG &= ~(1U << idx);
        CCU_TWI_BGR_REG &= ~(1U << (16 + idx));
        break;
    }
    case BusModule::Spi0:
    case BusModule::Spi1:
    case BusModule::Spi2: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Spi0);
        CCU_SPI_BGR_REG &= ~(1U << idx);
        CCU_SPI_BGR_REG &= ~(1U << (16 + idx));
        break;
    }
    case BusModule::Pwm:
        CCU_PWM_BGR_REG &= ~((1U << 0)  | (1U << 1));
        CCU_PWM_BGR_REG &= ~((1U << 16) | (1U << 17));
        break;
    case BusModule::Ledc:
        CCU_LEDC_BGR_REG &= ~(1U << 0);
        CCU_LEDC_BGR_REG &= ~(1U << 16);
        break;
    case BusModule::Gpadc:
        CCU_GPADC_BGR_REG &= ~((1U << 0)  | (1U << 1));
        CCU_GPADC_BGR_REG &= ~((1U << 16) | (1U << 17));
        break;
    case BusModule::McuTimer:
        MCU_PRCM_TIMER_BGR_REG &= ~(1U << 0);
        MCU_PRCM_TIMER_BGR_REG &= ~(1U << 16);
        break;
    case BusModule::McuMsgBox:
        MCU_PRCM_MSGBOX_BGR_REG &= ~(1U << 0);
        MCU_PRCM_MSGBOX_BGR_REG &= ~(1U << 16);
        break;
    case BusModule::McuDma:
        MCU_PRCM_DMA_BGR_REG &= ~(1U << 0);
        MCU_PRCM_DMA_BGR_REG &= ~(1U << 16);
        break;
    }

    std::atomic_thread_fence(std::memory_order_seq_cst);
}

bool Ccu::is_module_enabled(BusModule mod) noexcept
{
    switch (mod) {
    case BusModule::Uart0:
    case BusModule::Uart1:
    case BusModule::Uart2:
    case BusModule::Uart3:
    case BusModule::Uart4:
    case BusModule::Uart5:
    case BusModule::Uart6:
    case BusModule::Uart7: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Uart0);
        return (CCU_UART_BGR_REG & (1U << idx)) != 0;
    }
    case BusModule::Twi0:
    case BusModule::Twi1:
    case BusModule::Twi2:
    case BusModule::Twi3:
    case BusModule::Twi4:
    case BusModule::Twi5: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Twi0);
        return (CCU_TWI_BGR_REG & (1U << idx)) != 0;
    }
    case BusModule::Spi0:
    case BusModule::Spi1:
    case BusModule::Spi2: {
        const uint32_t idx = static_cast<uint32_t>(mod) - static_cast<uint32_t>(BusModule::Spi0);
        return (CCU_SPI_BGR_REG & (1U << idx)) != 0;
    }
    case BusModule::Pwm:
        return (CCU_PWM_BGR_REG & (1U << 0)) != 0;
    case BusModule::Ledc:
        return (CCU_LEDC_BGR_REG & (1U << 0)) != 0;
    case BusModule::Gpadc:
        return (CCU_GPADC_BGR_REG & (1U << 0)) != 0;
    case BusModule::McuTimer:
        return (MCU_PRCM_TIMER_BGR_REG & (1U << 0)) != 0;
    case BusModule::McuMsgBox:
        return (MCU_PRCM_MSGBOX_BGR_REG & (1U << 0)) != 0;
    case BusModule::McuDma:
        return (MCU_PRCM_DMA_BGR_REG & (1U << 0)) != 0;
    default:
        return false;
    }
}

} // namespace hal