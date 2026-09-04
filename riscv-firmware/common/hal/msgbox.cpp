#include "msgbox.hpp"
#include "memory_map.h"

namespace hal {

#ifndef SUNXI_RISCV_MSGBOX_BASE
#define SUNXI_RISCV_MSGBOX_BASE     MSGBOX_BASE
#endif

#define MSGBOX_CTRL_REG(ch)         (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0000 + ((ch) * 0x04)))
#define MSGBOX_REMOTE_IRQ_EN_REG    (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0040))
#define MSGBOX_REMOTE_IRQ_STA_REG   (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0050))
#define MSGBOX_LOCAL_IRQ_EN_REG     (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0060))
#define MSGBOX_LOCAL_IRQ_STA_REG    (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0070))
#define MSGBOX_FIFO_STA_REG(ch)     (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0100 + ((ch) * 0x04)))
#define MSGBOX_MSG_STA_REG(ch)      (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0140 + ((ch) * 0x04)))
#define MSGBOX_MSG_FIFO_REG(ch)     (*(volatile uint32_t *)(SUNXI_RISCV_MSGBOX_BASE + 0x0180 + ((ch) * 0x04)))

// Hardware Status Bits (Matching Allwinner sun6i-msgbox FIFO_STAT)
inline constexpr uint32_t FIFO_STATUS_FULL  = (1U << 0);
inline constexpr uint32_t FIFO_STATUS_EMPTY = (1U << 0);

void MsgBox::init() noexcept
{
    // Disable interrupts and clear pending status
    MSGBOX_LOCAL_IRQ_EN_REG = 0x00000000U;
    MSGBOX_LOCAL_IRQ_STA_REG = 0xFFFFFFFFU;

    s_doorbell_token.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

bool MsgBox::send(Channel ch, uint32_t data) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return false;

    // Check Tx FIFO capacity
    if (MSGBOX_FIFO_STA_REG(c) & FIFO_STATUS_FULL) {
        return false;
    }

    // Acquire/Release ordering on write
    std::atomic_thread_fence(std::memory_order_release);
    MSGBOX_MSG_FIFO_REG(c) = data;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    return true;
}

void MsgBox::send_blocking(Channel ch, uint32_t data) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return;

    while (MSGBOX_FIFO_STA_REG(c) & FIFO_STATUS_FULL) {
        // Spin-wait hint on RISC-V pipeline
        __asm__ volatile ("pause");
    }

    std::atomic_thread_fence(std::memory_order_release);
    MSGBOX_MSG_FIFO_REG(c) = data;
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

std::optional<uint32_t> MsgBox::receive(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return std::nullopt;

    if (MSGBOX_FIFO_STA_REG(c) & FIFO_STATUS_EMPTY) {
        return std::nullopt;
    }

    uint32_t val = MSGBOX_MSG_FIFO_REG(c);
    std::atomic_thread_fence(std::memory_order_acquire);

    return val;
}

bool MsgBox::is_rx_pending(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    return (c <= 3) && !(MSGBOX_FIFO_STA_REG(c) & FIFO_STATUS_EMPTY);
}

bool MsgBox::is_tx_ready(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    return (c <= 3) && !(MSGBOX_FIFO_STA_REG(c) & FIFO_STATUS_FULL);
}

void MsgBox::enable_rx_irq(Channel ch, bool enable) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return;

    const uint32_t mask = (1U << (c * 2));
    if (enable) {
        MSGBOX_LOCAL_IRQ_EN_REG |= mask;
    } else {
        MSGBOX_LOCAL_IRQ_EN_REG &= ~mask;
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void MsgBox::clear_irq_status(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return;

    MSGBOX_LOCAL_IRQ_STA_REG = (3U << (c * 2)); // W1C
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void MsgBox::notify_doorbell(uint32_t token) noexcept
{
    s_doorbell_token.store(token, std::memory_order_release);
}

uint32_t MsgBox::wait_for_doorbell(uint32_t old_token) noexcept
{
    while (s_doorbell_token.load(std::memory_order_acquire) == old_token) {
        __asm__ volatile ("pause");
    }
    return s_doorbell_token.load(std::memory_order_acquire);
}

} // namespace hal