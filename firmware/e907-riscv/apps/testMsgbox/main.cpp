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
    hal::Trace::puts("  Allwinner T527 XuanTie E907 + DSP Mailbox Test (testMsgbox)   \n");
    hal::Trace::puts("  E907 Port 2: Local 0x07136000 (Rx Ch 0 / Linux Ch 8)         \n");
    hal::Trace::puts("               Remote 0x03003000 (Tx Ch 1 / Linux Ch 9)         \n");
    hal::Trace::puts("  DSP  Port 0: Local 0x07094000 (Rx Ch 0 / Linux Ch 4)         \n");
    hal::Trace::puts("               Remote 0x03003000 + 0x100 (Tx Ch 1 / Linux Ch 5) \n");
    hal::Trace::puts("  Waiting for Mailbox messages from Host...                     \n");
    hal::Trace::puts("================================================================\n");

    uint32_t rv_rx_count = 0;
    uint32_t dsp_rx_count = 0;

    while (1) {
        /* 1. Service E907 Port 2 (Linux Channels 8 & 9) */
        if (hal::MsgBox::is_rx_pending(hal::MsgBox::Channel::Channel0)) {
            auto val_opt = hal::MsgBox::receive(hal::MsgBox::Channel::Channel0);
            if (val_opt.has_value()) {
                uint32_t rx_val = *val_opt;
                rv_rx_count++;

                /* Echo or send "PONG" (0x504F4E47) back to Host on Channel 1 (RV -> ARM) */
                uint32_t tx_val = (rx_val == 0x50494E47 /* 'PING' */) ? 0x504F4E47 /* 'PONG' */ : (rx_val + 1);
                hal::MsgBox::send(hal::MsgBox::Channel::Channel1, tx_val);
            }
        }

        /* 2. Service DSP Port 0 / 1 (Linux Channels 4 & 5) */
        if (hal::MsgBox::is_dsp_rx_pending(hal::MsgBox::Channel::Channel0)) {
            auto val_opt = hal::MsgBox::receive_dsp(hal::MsgBox::Channel::Channel0);
            if (val_opt.has_value()) {
                uint32_t rx_val = *val_opt;
                dsp_rx_count++;

                /* Echo or send "PONG" (0x504F4E47) back to Host on Channel 1 (DSP -> ARM) */
                uint32_t tx_val = (rx_val == 0x50494E47 /* 'PING' */) ? 0x504F4E47 /* 'PONG' */ : (rx_val + 1);
                hal::MsgBox::send_dsp(hal::MsgBox::Channel::Channel1, tx_val);
            }
        }
    }


    return 0;
}
