/*
 * main.cpp - testMsgbox: XuanTie E907 Hardware Message Box & IPC Test
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 *
 * Validates:
 *   1. Hardware msgbox reception from ARM A55 (Channel 8 on Linux / Channel 0 on E907).
 *   2. Hardware msgbox transmission back to ARM A55 (Channel 9 on Linux / Channel 1 on E907).
 *   3. Real-time tracing of mailbox traffic visible in trace0.
 */

#include <stdint.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"
#include "hal/msgbox.hpp"

int main(void) {
    hal::Trace::init();
    hal::Timer::init();
    hal::MsgBox::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 testMsgbox (Hardware Mailbox)    \n");
    hal::Trace::puts("  Local Port : 0x07136000 (Port 2, Rx Channel 0 = Linux Ch 8)  \n");
    hal::Trace::puts("  Remote Port: 0x03003000 (Port 2, Tx Channel 1 = Linux Ch 9)  \n");
    hal::Trace::puts("  Waiting for Mailbox message from Host...                      \n");
    hal::Trace::puts("================================================================\n");

    uint32_t rx_count = 0;

    while (1) {
        if (hal::MsgBox::is_rx_pending(hal::MsgBox::Channel::Channel0)) {
            auto val_opt = hal::MsgBox::receive(hal::MsgBox::Channel::Channel0);
            if (val_opt.has_value()) {
                uint32_t rx_val = *val_opt;
                rx_count++;

                hal::Trace::printf("[E907-RISCV] MsgBox RX #%u: 0x%08x\n", rx_count, rx_val);

                /* Echo or send "PONG" (0x504F4E47) back to Host on Channel 1 (RV -> ARM) */
                uint32_t tx_val = (rx_val == 0x50494E47 /* 'PING' */) ? 0x504F4E47 /* 'PONG' */ : (rx_val + 1);
                hal::MsgBox::send(hal::MsgBox::Channel::Channel1, tx_val);

                hal::Trace::printf("[E907-RISCV] MsgBox TX Replied: 0x%08x\n", tx_val);
            }
        }
    }

    return 0;
}
