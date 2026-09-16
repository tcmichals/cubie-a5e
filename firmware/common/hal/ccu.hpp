#pragma once

#include <cstdint>
#include <stdbool.h>

namespace hal {

/**
 * @brief Allwinner T527 Clock Control Unit (CCU) & MCU_PRCM HAL
 */
class Ccu {
public:
    enum class BusModule : uint8_t {
        // Main SoC APB/AHB Peripheral Clocks & Resets
        Uart0,
        Uart1,
        Uart2,
        Uart3,
        Uart4,
        Uart5,
        Uart6,
        Uart7,
        Twi0,
        Twi1,
        Twi2,
        Twi3,
        Twi4,
        Twi5,
        Spi0,
        Spi1,
        Spi2,
        Pwm,
        Ledc,
        Gpadc,
        
        // MCU Local Domain Clocks & Resets (MCU_PRCM)
        McuTimer,
        McuMsgBox,
        McuDma
    };

    /**
     * @brief Initialize default peripheral gating and power domains
     */
    static void init() noexcept;

    /**
     * @brief Enable module bus clock gating and deassert software reset
     */
    static void enable_module(BusModule mod) noexcept;

    /**
     * @brief Disable module bus clock and assert software reset
     */
    static void disable_module(BusModule mod) noexcept;

    /**
     * @brief Check if module bus clock gating is active
     */
    [[nodiscard]] static bool is_module_enabled(BusModule mod) noexcept;
};

} // namespace hal