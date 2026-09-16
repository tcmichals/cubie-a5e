/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Hardware Mailbox Driver for Allwinner T527 / A527 Co-Processors
 * Shared across XuanTie E907 (RISC-V) and Cadence Tensilica HiFi4 (DSP).
 */

#ifndef SUNXI_MSGBOX_H
#define SUNXI_MSGBOX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allwinner T527 Message Box Base Addresses */
#define SUNXI_MSGBOX_ARM_BASE       0x03003000U
#define SUNXI_MSGBOX_DSP_BASE       0x07094000U
#define SUNXI_MSGBOX_RV_BASE        0x07136000U

/* Automatic core port selection based on compiler target */
#if defined(__XTENSA__) || defined(CONFIG_CORE_HIFI4)
  #define SUNXI_MSGBOX_LOCAL_BASE       SUNXI_MSGBOX_DSP_BASE
  #define SUNXI_MSGBOX_PORT_OFFSET      0x00000000U /* DSP Local Port 0 */
  #define SUNXI_MSGBOX_ARM_PORT_OFFSET  0x00000100U /* ARM Host Port 1 (Channels 4..7) */
#elif defined(__riscv) || defined(CONFIG_CORE_E907)
  #define SUNXI_MSGBOX_LOCAL_BASE       SUNXI_MSGBOX_RV_BASE
  #define SUNXI_MSGBOX_PORT_OFFSET      0x00000200U /* RV Local Port 2 */
  #define SUNXI_MSGBOX_ARM_PORT_OFFSET  0x00000200U /* ARM Host Port 2 (Channels 8..11) */
#else
  #define SUNXI_MSGBOX_LOCAL_BASE       SUNXI_MSGBOX_RV_BASE
  #define SUNXI_MSGBOX_PORT_OFFSET      0x00000200U
  #define SUNXI_MSGBOX_ARM_PORT_OFFSET  0x00000200U
#endif

#define SUNXI_MSGBOX_MAX_CHANNEL    4
#define SUNXI_MSGBOX_MAX_QUEUE      8
#define SUNXI_MSGBOX_MSG_NUM_MASK   0x0FU

#define read32(reg)         (*((volatile uint32_t *)(reg)))
#define write32(reg, value) (*((volatile uint32_t *)(reg)) = (value))

/* Register access definitions */
#define SUNXI_MSGBOX_RD_IRQ_EN_REG \
    (*(volatile uint32_t *)(SUNXI_MSGBOX_LOCAL_BASE + 0x020 + SUNXI_MSGBOX_PORT_OFFSET))
#define SUNXI_MSGBOX_RD_IRQ_STA_REG \
    (*(volatile uint32_t *)(SUNXI_MSGBOX_LOCAL_BASE + 0x024 + SUNXI_MSGBOX_PORT_OFFSET))

#define SUNXI_MSGBOX_RX_STA_REG(ch) \
    (*(volatile uint32_t *)(SUNXI_MSGBOX_LOCAL_BASE + 0x060 + SUNXI_MSGBOX_PORT_OFFSET + ((ch) * 4)))
#define SUNXI_MSGBOX_RX_FIFO_REG(ch) \
    (*(volatile uint32_t *)(SUNXI_MSGBOX_LOCAL_BASE + 0x070 + SUNXI_MSGBOX_PORT_OFFSET + ((ch) * 4)))

#define SUNXI_MSGBOX_TX_STA_REG(ch) \
    (*(volatile uint32_t *)(SUNXI_MSGBOX_ARM_BASE + 0x060 + SUNXI_MSGBOX_ARM_PORT_OFFSET + ((ch) * 4)))
#define SUNXI_MSGBOX_TX_FIFO_REG(ch) \
    (*(volatile uint32_t *)(SUNXI_MSGBOX_ARM_BASE + 0x070 + SUNXI_MSGBOX_ARM_PORT_OFFSET + ((ch) * 4)))

/* Unified C API */
void     sunxi_msgbox_init(void);
void     sunxi_msgbox_init_ex(bool enable_irq);
bool     sunxi_msgbox_has_data(uint32_t ch);
uint32_t sunxi_msgbox_recv(uint32_t ch);
void     sunxi_msgbox_send(uint32_t ch, uint32_t data);
bool     sunxi_msgbox_try_send(uint32_t ch, uint32_t data);
void     sunxi_msgbox_send_bytes(uint32_t ch, const uint8_t *buf, uint32_t len);

/* Backward compatibility aliases */
#define dsp_msgbox_init         sunxi_msgbox_init
#define dsp_msgbox_has_data     sunxi_msgbox_has_data
#define dsp_msgbox_recv         sunxi_msgbox_recv
#define dsp_msgbox_send         sunxi_msgbox_send
#define dsp_msgbox_send_bytes   sunxi_msgbox_send_bytes

#ifdef __cplusplus
}
#endif

#endif /* SUNXI_MSGBOX_H */
