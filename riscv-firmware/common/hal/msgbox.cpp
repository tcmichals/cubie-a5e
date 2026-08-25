#include "msgbox.hpp"
#include "../../include/memory_map.h"

namespace fc::hal {

/* Message Box Register Offsets */
#define MBOX_RD_IRQ_EN      (*(volatile uint32_t *)(MSGBOX_BASE + 0x00))
#define MBOX_RD_IRQ_STATUS  (*(volatile uint32_t *)(MSGBOX_BASE + 0x04))
#define MBOX_MSG_STATUS(ch) (*(volatile uint32_t *)(MSGBOX_BASE + 0x100 + ((ch) * 4)))
#define MBOX_MSG_DATA(ch)   (*(volatile uint32_t *)(MSGBOX_BASE + 0x140 + ((ch) * 4)))

void MsgBox::init() {
    // Enable Read IRQ for Channel 1 (ARM -> RISC-V)
    MBOX_RD_IRQ_EN |= (1 << 1);
}

void MsgBox::notify_host(uint32_t message_id) {
    // Channel 0: RISC-V -> ARM64
    // Wait if FIFO is full (max 8 entries)
    while (MBOX_MSG_STATUS(0) >= 8);
    MBOX_MSG_DATA(0) = message_id;
}

bool MsgBox::has_host_notification(uint32_t *message_id) {
    // Channel 1: ARM64 -> RISC-V
    if (MBOX_MSG_STATUS(1) > 0) {
        uint32_t val = MBOX_MSG_DATA(1);
        if (message_id) *message_id = val;
        // Clear IRQ status
        MBOX_RD_IRQ_STATUS = (1 << 1);
        return true;
    }
    return false;
}

} // namespace fc::hal
