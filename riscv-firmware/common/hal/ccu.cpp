#include "ccu.hpp"
#include "../../include/memory_map.h"

namespace fc::hal {

/* CCU Register Offsets */
#define CCU_MSGBOX_BGR_REG      (*(volatile uint32_t *)(CCU_BASE + 0x071C))
#define CCU_UART2_BGR_REG       (*(volatile uint32_t *)(CCU_BASE + 0x0908))
#define CCU_SPI0_BGR_REG        (*(volatile uint32_t *)(CCU_BASE + 0x0940))
#define CCU_SPI0_CLK_REG        (*(volatile uint32_t *)(CCU_BASE + 0x0948))

void Ccu::init() {
    enable_spi0();
    enable_uart2();
    enable_msgbox();
}

void Ccu::enable_spi0() {
    // 1. Deassert reset (bit 16) and enable bus clock gate (bit 0)
    CCU_SPI0_BGR_REG |= (1 << 16) | (1 << 0);

    // 2. Configure SPI0 module clock (bit 31=enable, clock source=24MHz OSC or PLL_PERIPH0)
    // 0x80000000 = Gate ON, clock source = 24MHz (divider = 1)
    CCU_SPI0_CLK_REG = (1U << 31);
}

void Ccu::enable_uart2() {
    // Deassert reset (bit 16) and enable bus clock gate (bit 0)
    CCU_UART2_BGR_REG |= (1 << 16) | (1 << 0);
}

void Ccu::enable_msgbox() {
    // Deassert reset (bit 16) and enable bus clock gate (bit 0)
    CCU_MSGBOX_BGR_REG |= (1 << 16) | (1 << 0);
}

} // namespace fc::hal
