/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Hardware Mailbox Driver Implementation for Allwinner T527 Co-Processors
 * Shared across XuanTie E907 (RISC-V) and Cadence Tensilica HiFi4 (DSP).
 */

#include "msgbox.h"

void sunxi_msgbox_init(void)
{
    /* Enable Receive IRQ on local port for channel 0 (ARM -> Co-processor) */
    SUNXI_MSGBOX_RD_IRQ_EN_REG |= 0x00000001U;

    /* Clear any pending status */
    SUNXI_MSGBOX_RD_IRQ_STA_REG = 0xFFFFFFFFU;

    /* Drain any stale FIFO entries */
    for (uint32_t c = 0; c < SUNXI_MSGBOX_MAX_CHANNEL; ++c) {
        while ((SUNXI_MSGBOX_RX_STA_REG(c) & SUNXI_MSGBOX_MSG_NUM_MASK) > 0) {
            (void)SUNXI_MSGBOX_RX_FIFO_REG(c);
        }
    }
}

bool sunxi_msgbox_has_data(uint32_t ch)
{
    if (ch >= SUNXI_MSGBOX_MAX_CHANNEL)
        return false;

    return (SUNXI_MSGBOX_RX_STA_REG(ch) & SUNXI_MSGBOX_MSG_NUM_MASK) > 0;
}

uint32_t sunxi_msgbox_recv(uint32_t ch)
{
    if (ch >= SUNXI_MSGBOX_MAX_CHANNEL)
        return 0;

    uint32_t data = SUNXI_MSGBOX_RX_FIFO_REG(ch);
    SUNXI_MSGBOX_RD_IRQ_STA_REG = (1U << (ch * 2));
    return data;
}

void sunxi_msgbox_send(uint32_t ch, uint32_t data)
{
    if (ch >= SUNXI_MSGBOX_MAX_CHANNEL)
        return;

    /* Wait until remote ARM Tx FIFO has capacity */
    while ((SUNXI_MSGBOX_TX_STA_REG(ch) & SUNXI_MSGBOX_MSG_NUM_MASK) >= SUNXI_MSGBOX_MAX_QUEUE) {
        #if defined(__riscv)
        __asm__ volatile ("pause");
        #elif defined(__XTENSA__)
        __asm__ volatile ("nop");
        #endif
    }

    SUNXI_MSGBOX_TX_FIFO_REG(ch) = data;
}

bool sunxi_msgbox_try_send(uint32_t ch, uint32_t data)
{
    if (ch >= SUNXI_MSGBOX_MAX_CHANNEL)
        return false;

    if ((SUNXI_MSGBOX_TX_STA_REG(ch) & SUNXI_MSGBOX_MSG_NUM_MASK) >= SUNXI_MSGBOX_MAX_QUEUE)
        return false;

    SUNXI_MSGBOX_TX_FIFO_REG(ch) = data;
    return true;
}

void sunxi_msgbox_send_bytes(uint32_t ch, const uint8_t *buf, uint32_t len)
{
    uint32_t data = 0;
    for (uint32_t i = 0; i < len; i++) {
        if ((i % 4) == 0) {
            data = 0;
        }

        data |= ((uint32_t)buf[i]) << ((i % 4) * 8);

        if ((i % 4) == 3 || i == (len - 1)) {
            sunxi_msgbox_send(ch, data);
        }
    }
}
