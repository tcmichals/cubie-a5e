/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Hardware Mailbox Implementation for Allwinner T527 HiFi4 DSP
 * Adapted from YuzukiHD FreeRTOS-HIFI4-DSP
 */

#include "msgbox.h"

void dsp_msgbox_init(void)
{
    /* Enable Receive IRQ on DSP local channel 0 (ARM -> DSP Mailbox) */
    SUNXI_MSGBOX_RD_IRQ_ENABLE(0, 0);
}

bool dsp_msgbox_has_data(uint32_t ch)
{
    /* Check message status FIFO queue count */
    return (read32(SUNXI_MSGBOX_DSP_MSG_STATUS_REG(0, ch)) & 0x0F) > 0;
}

uint32_t dsp_msgbox_recv(uint32_t ch)
{
    uint32_t data = read32(SUNXI_MSGBOX_DSP_MSG_REG(0, ch));
    SUNXI_MSGBOX_RD_IRQ_CLR_PENDING(0, ch);
    return data;
}

void dsp_msgbox_send(uint32_t ch, uint32_t data)
{
    /* Wait until FIFO is not full */
    while (read32(SUNXI_MSGBOX_ARM_MSG_STATUS_REG(0, ch)) >= SUNXI_MSGBOX_MAX_QUEUE) {
        /* Busy wait */
    }
    write32(SUNXI_MSGBOX_ARM_MSG_REG(0, ch), data);
}

void dsp_msgbox_send_bytes(uint32_t ch, const uint8_t *buf, uint32_t len)
{
    uint32_t data = 0;
    for (uint32_t i = 0; i < len; i++) {
        if ((i % 4) == 0) {
            data = 0;
        }

        data |= ((uint32_t)buf[i]) << ((i % 4) * 8);

        if ((i % 4) == 3 || i == (len - 1)) {
            dsp_msgbox_send(ch, data);
        }
    }
}
