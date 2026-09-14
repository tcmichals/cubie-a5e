/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Hardware Mailbox Driver for Allwinner T527 / A527 Cadence HiFi4 DSP
 * Adapted from YuzukiHD FreeRTOS-HIFI4-DSP
 */

#ifndef MSGBOX_H
#define MSGBOX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define read32(reg)         (*((volatile uint32_t *)(reg)))
#define write32(reg, value) (*((volatile uint32_t *)(reg)) = (value))

/* Allwinner T527 Message Box Base Addresses */
#define SUNXI_MSGBOX_ARM_BASE 0x03003000
#define SUNXI_MSGBOX_DSP_BASE 0x07094000

#define SUNXI_MSGBOX_MAX_CHANNEL 4
#define SUNXI_MSGBOX_MAX_QUEUE   8

#define SUNXI_MSGBOX_RD_IRQ_ENABLE_REG(proc) \
    (SUNXI_MSGBOX_DSP_BASE + 0x20 + ((proc) * 0x100))
#define SUNXI_MSGBOX_RD_IRQ_STATUS_REG(proc) \
    (SUNXI_MSGBOX_DSP_BASE + 0x24 + ((proc) * 0x100))
#define SUNXI_MSGBOX_WR_IRQ_ENABLE_REG(proc) \
    (SUNXI_MSGBOX_DSP_BASE + 0x30 + ((proc) * 0x100))
#define SUNXI_MSGBOX_WR_IRQ_STATUS_REG(proc) \
    (SUNXI_MSGBOX_DSP_BASE + 0x34 + ((proc) * 0x100))

#define SUNXI_MSGBOX_RD_IRQ_ENABLE(proc, ch) \
    write32(SUNXI_MSGBOX_RD_IRQ_ENABLE_REG(proc), \
            read32(SUNXI_MSGBOX_RD_IRQ_ENABLE_REG(proc)) | (1 << (2 * (ch))))

#define SUNXI_MSGBOX_RD_IRQ_DISABLE(proc, ch) \
    write32(SUNXI_MSGBOX_RD_IRQ_ENABLE_REG(proc), \
            read32(SUNXI_MSGBOX_RD_IRQ_ENABLE_REG(proc)) & (~(1 << (2 * (ch)))))

#define SUNXI_MSGBOX_RD_IRQ_IS_PENDING(proc, ch) \
    (read32(SUNXI_MSGBOX_RD_IRQ_STATUS_REG(proc)) & (1 << (2 * (ch))))

#define SUNXI_MSGBOX_RD_IRQ_CLR_PENDING(proc, ch) \
    write32(SUNXI_MSGBOX_RD_IRQ_STATUS_REG(proc), (1 << (2 * (ch))))

#define SUNXI_MSGBOX_ARM_MSG_STATUS_REG(proc, ch) \
    (SUNXI_MSGBOX_ARM_BASE + 0x60 + ((proc) * 0x100) + ((ch) * 0x4))
#define SUNXI_MSGBOX_DSP_MSG_STATUS_REG(proc, ch) \
    (SUNXI_MSGBOX_DSP_BASE + 0x60 + ((proc) * 0x100) + ((ch) * 0x4))

#define SUNXI_MSGBOX_ARM_MSG_REG(proc, ch) \
    (SUNXI_MSGBOX_ARM_BASE + 0x70 + ((proc) * 0x100) + ((ch) * 0x4))
#define SUNXI_MSGBOX_DSP_MSG_REG(proc, ch) \
    (SUNXI_MSGBOX_DSP_BASE + 0x70 + ((proc) * 0x100) + ((ch) * 0x4))

/* Function prototypes */
void dsp_msgbox_init(void);
bool dsp_msgbox_has_data(uint32_t ch);
uint32_t dsp_msgbox_recv(uint32_t ch);
void dsp_msgbox_send(uint32_t ch, uint32_t data);
void dsp_msgbox_send_bytes(uint32_t ch, const uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* MSGBOX_H */
