/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * testMsgbox: Cadence Tensilica HiFi4 Audio DSP Mailbox & IPC Test
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 * Leveraging YuzukiHD FreeRTOS-HIFI4-DSP msgbox driver
 *
 * Validates:
 *   1. Hardware msgbox reception from ARM A55.
 *   2. Hardware msgbox transmission back to ARM A55.
 *   3. Real-time tracing of mailbox traffic visible in trace0.
 */

#include "resource_table.h"
#include "sunxi_msgbox.h"


void _start(void)
{
    dsp_trace_init();
    dsp_trace_puts("========================================\n");
    dsp_trace_puts("[DSP-HIFI4] testMsgbox: Initialized!\n");
    dsp_trace_puts("[DSP-HIFI4] Mailbox Driver: YuzukiHD sunxi-msgbox\n");
    dsp_trace_puts("[DSP-HIFI4] Waiting for Mailbox message from Host...\n");
    dsp_trace_puts("========================================\n");

    dsp_msgbox_init();

    while (1) {
        /* Check if data arrived on Channel 0 (ARM -> DSP) */
        if (dsp_msgbox_has_data(0)) {
            uint32_t rx_val = dsp_msgbox_recv(0);

            dsp_trace_puts("[DSP-HIFI4] MsgBox RX Triggered!\n");

            /* Echo or send "PONG" (0x504F4E47) back to Host on Channel 1 (DSP -> ARM) */
            uint32_t tx_val = (rx_val == 0x50494E47 /* 'PING' */) ? 0x504F4E47 /* 'PONG' */ : (rx_val + 1);
            dsp_msgbox_send(1, tx_val);

            dsp_trace_puts("[DSP-HIFI4] MsgBox TX Replied to Host.\n");
        }
    }
}
