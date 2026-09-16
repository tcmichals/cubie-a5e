#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <optional>

namespace hal {

/**
 * @brief Allwinner T527 Hardware Message Box (MSGBOX) IPC Driver (C++20)
 *
 * Provides atomic FIFO flow control, lock-free status monitoring,
 * and wait/notify integration between the XuanTie E907 and Cortex-A55 host.
 */
class MsgBox {
public:
    enum class Channel : uint8_t {
        Channel0 = 0,
        Channel1 = 1,
        Channel2 = 2,
        Channel3 = 3
    };

    /**
     * @brief Initialize local RISC-V hardware message box
     */
    static void init() noexcept;

    /**
     * @brief Non-blocking message send to Cortex-A55 (CPUX)
     * @param ch Target channel
     * @param data 32-bit payload (e.g. virtqueue ID or token)
     * @return true if written to Tx FIFO, false if FIFO was full
     */
    static bool send(Channel ch, uint32_t data) noexcept;

    /**
     * @brief Blocking message send with C++20 atomic backoff/spin
     * @param ch Target channel
     * @param data 32-bit payload
     */
    static void send_blocking(Channel ch, uint32_t data) noexcept;

    /**
     * @brief Non-blocking message receive from Cortex-A55 (CPUX)
     * @param ch Source channel
     * @return std::optional containing the 32-bit value, or std::nullopt if empty
     */
    static std::optional<uint32_t> receive(Channel ch) noexcept;

    /**
     * @brief Check if receive FIFO has pending messages
     */
    [[nodiscard]] static bool is_rx_pending(Channel ch) noexcept;

    /**
     * @brief Check if transmit FIFO has available capacity
     */
    [[nodiscard]] static bool is_tx_ready(Channel ch) noexcept;

    /**
     * @brief Enable/Disable Receive Interrupt for channel
     */
    static void enable_rx_irq(Channel ch, bool enable) noexcept;

    /**
     * @brief Clear pending interrupt flag for channel
     */
    static void clear_irq_status(Channel ch) noexcept;

    /**
     * @brief C++20 atomic doorbell notification token
     */
    static void notify_doorbell(uint32_t token) noexcept;

    /**
     * @brief C++20 wait for host doorbell update
     */
    static uint32_t wait_for_doorbell(uint32_t old_token) noexcept;

private:
    static inline std::atomic<uint32_t> s_doorbell_token{0};
};

} // namespace hal