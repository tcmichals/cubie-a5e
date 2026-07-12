#pragma once
#include <stdint.h>
#include <stdbool.h>

namespace hardware {

// Zero-cost hardware mapping using a volatile struct
// This explicitly prevents the compiler from optimizing out memory accesses
// and gives us strict type safety without macro soup.
struct MailboxRegs {
    volatile uint32_t CTRL[4];        // 0x0000 - 0x000C
    uint32_t _reserved0[12];          // 0x0010 - 0x003C
    volatile uint32_t REMOTE_IRQ_EN;  // 0x0040
    uint32_t _reserved1[3];           // 0x0044 - 0x004C
    volatile uint32_t REMOTE_IRQ_STAT;// 0x0050
    uint32_t _reserved2[3];           // 0x0054 - 0x005C
    volatile uint32_t LOCAL_IRQ_EN;   // 0x0060
    uint32_t _reserved3[3];           // 0x0064 - 0x006C
    volatile uint32_t LOCAL_IRQ_STAT; // 0x0070
    uint32_t _reserved4[51];          // 0x0074 - 0x013C
    volatile uint32_t MSG_STAT[16];   // 0x0140 - 0x017C
    volatile uint32_t MSG_DATA[16];   // 0x0180 - 0x01BC
};

class Mailbox {
public:
    static constexpr uintptr_t BASE_ADDR = 0x03003000;
    
    // Inlined accessor compiles down to a single constant address load
    static inline MailboxRegs* regs() {
        return reinterpret_cast<MailboxRegs*>(BASE_ADDR);
    }

    static inline void init() {
        // Clear any pending local interrupts
        regs()->LOCAL_IRQ_STAT = 0xFFFFFFFF;
    }

    static inline bool has_new_msg(uint32_t channel) {
        if (channel >= 16) return false;
        // Check the message status register for pending words (0-8 in FIFO)
        return (regs()->MSG_STAT[channel] & 0x7) > 0;
    }

    static inline uint32_t read_msg(uint32_t channel) {
        if (!has_new_msg(channel)) return 0;
        // Read raw data word from register
        return regs()->MSG_DATA[channel];
    }

    static inline void send_msg(uint32_t channel, uint32_t payload) {
        if (channel >= 16) return;
        // Write data to MSG_DATA which automatically asserts remote core IRQ
        regs()->MSG_DATA[channel] = payload;
    }
};

} // namespace hardware
