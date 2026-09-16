#include "msgbox.hpp"
#include "memory_map.h"

namespace hal {

/*
 * Allwinner T527 4-Port Hardware Message Box
 * RISC-V Local Port Base: 0x07136000
 * ARM Host Port Base:    0x03003000
 * Port Index for RV <-> ARM communication: n = 2 (Offset: 0x200)
 */
#define RV_MSGBOX_LOCAL_BASE        0x07136000U
#define ARM_MSGBOX_REMOTE_BASE      0x03003000U
#define MSGBOX_PORT_OFFSET          0x00000200U

#define RV_READ_IRQ_EN_REG          (*(volatile uint32_t *)(RV_MSGBOX_LOCAL_BASE + 0x020 + MSGBOX_PORT_OFFSET))
#define RV_READ_IRQ_STA_REG         (*(volatile uint32_t *)(RV_MSGBOX_LOCAL_BASE + 0x024 + MSGBOX_PORT_OFFSET))
#define RV_MSG_STA_REG(ch)          (*(volatile uint32_t *)(RV_MSGBOX_LOCAL_BASE + 0x060 + MSGBOX_PORT_OFFSET + ((ch) * 4)))
#define RV_MSG_FIFO_REG(ch)         (*(volatile uint32_t *)(RV_MSGBOX_LOCAL_BASE + 0x070 + MSGBOX_PORT_OFFSET + ((ch) * 4)))

#define ARM_MSG_STA_REG(ch)         (*(volatile uint32_t *)(ARM_MSGBOX_REMOTE_BASE + 0x060 + MSGBOX_PORT_OFFSET + ((ch) * 4)))
#define ARM_MSG_FIFO_REG(ch)        (*(volatile uint32_t *)(ARM_MSGBOX_REMOTE_BASE + 0x070 + MSGBOX_PORT_OFFSET + ((ch) * 4)))

inline constexpr uint32_t MSG_NUM_MASK   = 0x0FU;
inline constexpr uint32_t FIFO_DEPTH_MAX = 8U;

void MsgBox::init() noexcept
{
    // Enable Read IRQs on local port for channels 0..3 (n=2, RV <-> ARM)
    RV_READ_IRQ_EN_REG = 0x00000055U; // Bits 0, 2, 4, 6 enable RD IRQ for ch 0..3
    RV_READ_IRQ_STA_REG = 0xFFFFFFFFU; // W1C clear pending

    // Flush any stale words in receive FIFOs
    for (uint8_t c = 0; c < 4; ++c) {
        while ((RV_MSG_STA_REG(c) & MSG_NUM_MASK) > 0) {
            (void)RV_MSG_FIFO_REG(c);
        }
    }

    s_doorbell_token.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

bool MsgBox::send(Channel ch, uint32_t data) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return false;

    // Check remote ARM Tx FIFO capacity (msg count < 8)
    if ((ARM_MSG_STA_REG(c) & MSG_NUM_MASK) >= FIFO_DEPTH_MAX) {
        return false;
    }

    std::atomic_thread_fence(std::memory_order_release);
    ARM_MSG_FIFO_REG(c) = data;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    return true;
}

void MsgBox::send_blocking(Channel ch, uint32_t data) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return;

    while ((ARM_MSG_STA_REG(c) & MSG_NUM_MASK) >= FIFO_DEPTH_MAX) {
        __asm__ volatile ("pause");
    }

    std::atomic_thread_fence(std::memory_order_release);
    ARM_MSG_FIFO_REG(c) = data;
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

std::optional<uint32_t> MsgBox::receive(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return std::nullopt;

    if ((RV_MSG_STA_REG(c) & MSG_NUM_MASK) == 0) {
        return std::nullopt;
    }

    uint32_t val = RV_MSG_FIFO_REG(c);
    // Clear pending IRQ status bit (W1C)
    RV_READ_IRQ_STA_REG = (1U << (c * 2));
    std::atomic_thread_fence(std::memory_order_acquire);

    return val;
}

bool MsgBox::is_rx_pending(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    return (c <= 3) && ((RV_MSG_STA_REG(c) & MSG_NUM_MASK) > 0);
}

bool MsgBox::is_tx_ready(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    return (c <= 3) && ((ARM_MSG_STA_REG(c) & MSG_NUM_MASK) < FIFO_DEPTH_MAX);
}

void MsgBox::enable_rx_irq(Channel ch, bool enable) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return;

    const uint32_t mask = (1U << (c * 2));
    if (enable) {
        RV_READ_IRQ_EN_REG |= mask;
    } else {
        RV_READ_IRQ_EN_REG &= ~mask;
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void MsgBox::clear_irq_status(Channel ch) noexcept
{
    const auto c = static_cast<uint8_t>(ch);
    if (c > 3) return;

    RV_READ_IRQ_STA_REG = (1U << (c * 2)); // W1C
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