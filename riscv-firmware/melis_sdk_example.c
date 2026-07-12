/*
 * melis_sdk_example.c - Allwinner Sunxi-Melis SDK Reference Example
 *
 * This file serves as an educational reference demonstrating how Allwinner's
 * official Melis SDK programs the hardware registers of the XuanTie E906/E907
 * co-processor complex, including clocks, the PLIC, mailboxes, timers, and DMA.
 */

#include <stdint.h>

/* ========================================================================== */
/* 1. Hardware Register Map Definitions                                       */
/* ========================================================================== */

/* Clock & Reset Control (Host View: 0x07010000, DSP/MCU View: 0x40010000) */
#define CCU_BASE             0x40010000
#define CCU_DSP_CLK_REG      (CCU_BASE + 0x0020)
#define CCU_DSP_RST_REG      (CCU_BASE + 0x0100)

/* PLIC (Platform-Level Interrupt Controller) MCU View Base */
#define PLIC_BASE            0x40020000
#define PLIC_PRIO_BASE       (PLIC_BASE + 0x0000)       /* Interrupt source priorities */
#define PLIC_PEND_BASE       (PLIC_BASE + 0x1000)       /* Interrupt pending bits */
#define PLIC_IE_BASE         (PLIC_BASE + 0x2000)       /* Interrupt enable bits */
#define PLIC_THRES_REG       (PLIC_BASE + 0x200000)     /* Priority threshold */
#define PLIC_CLAIM_REG       (PLIC_BASE + 0x200004)     /* Claim / Complete register */

/* CLINT (Core Local Interruptor) Base - Standard RISC-V Timer registers */
#define CLINT_BASE           0x40050000
#define CLINT_MSIP           (CLINT_BASE + 0x0000)      /* Software Interrupt Register */
#define CLINT_MTIMECMP_LOW   (CLINT_BASE + 0x4000)      /* Timer Compare Low */
#define CLINT_MTIMECMP_HIGH  (CLINT_BASE + 0x4004)      /* Timer Compare High */
#define CLINT_MTIME_LOW      (CLINT_BASE + 0xBFF8)      /* 64-bit Real-time Counter Low */
#define CLINT_MTIME_HIGH     (CLINT_BASE + 0xBFFC)      /* 64-bit Real-time Counter High */

/* Hardware Mailbox IPC Base (DSP/MCU View) */
#define MAILBOX_BASE         0x40030000
#define MBOX_CTRL_REG        (MAILBOX_BASE + 0x0000)    /* Mailbox Control */
#define MBOX_DSP_IRQ_EN      (MAILBOX_BASE + 0x0010)    /* MCU/DSP Interrupt Enable */
#define MBOX_DSP_IRQ_STA     (MAILBOX_BASE + 0x0014)    /* MCU/DSP Interrupt Status */
#define MBOX_ARM_IRQ_EN      (MAILBOX_BASE + 0x0020)    /* ARM Interrupt Enable */
#define MBOX_MSG_FIFO(x)     (MAILBOX_BASE + 0x0100 + ((x) * 4)) /* Message FIFOs */

/* MCU Direct Memory Access (DMA) Base Address */
#define MCU_DMA_BASE         0x40040000
#define DMA_IRQ_EN_REG       (MCU_DMA_BASE + 0x0000)    /* DMA IRQ Enable */
#define DMA_IRQ_STA_REG       (MCU_DMA_BASE + 0x0004)    /* DMA IRQ Status */
#define DMA_CHAN_DESC(n)     (MCU_DMA_BASE + 0x0100 + ((n) * 0x40)) /* Channel descriptor address */
#define DMA_CHAN_EN(n)       (MCU_DMA_BASE + 0x0100 + ((n) * 0x40) + 0x00) /* Channel Enable register */

/* ========================================================================== */
/* 2. Core Clock & Reset Management                                           */
/* ========================================================================== */

void melis_clock_init(void) {
    /* Write to the Clock Gating Control Register
     * Bit 0: MCU Core clock gate enable
     * Bit 1: DSP/MCU system bus clock gate enable
     */
    *(volatile uint32_t *)CCU_DSP_CLK_REG |= 0x00000003;
    
    /* Ensure the reset register has released the MCU clock gates
     * Bit 16: Debug Reset release
     * Bit 17: Core Reset release
     */
    *(volatile uint32_t *)CCU_DSP_RST_REG |= (1 << 17) | (1 << 16);
}

/* ========================================================================== */
/* 3. PLIC Interrupt Handling Setup                                           */
/* ========================================================================== */

#define SPI0_IRQ_NUM  14   /* SPI0 Hardware Interrupt source index */

void melis_plic_init(void) {
    /* 1. Set global threshold to 0 (allow all interrupts with priority > 0) */
    *(volatile uint32_t *)PLIC_THRES_REG = 0;
    
    /* 2. Enable SPI0 Interrupt channel (Source 14)
     * The enable bits are packed 32 per register.
     * Index = IRQ_Num / 32 = 0
     * Offset = IRQ_Num % 32 = 14
     */
    uint32_t reg_idx = SPI0_IRQ_NUM / 32;
    uint32_t bit_offset = SPI0_IRQ_NUM % 32;
    *(volatile uint32_t *)(PLIC_IE_BASE + (reg_idx * 4)) |= (1 << bit_offset);
    
    /* 3. Set priority level for SPI0 interrupt channel (Source 14)
     * Priority registers are 4 bytes each, indexed by interrupt source number.
     * Higher value = higher priority.
     */
    *(volatile uint32_t *)(PLIC_PRIO_BASE + (SPI0_IRQ_NUM * 4)) = 5; /* Priority = 5 */
}

/* Interrupt Service Routine Vector hook */
void melis_spi0_isr(void) {
    /* Implement SPI telemetry ingestion logic here */
}

/* Trap/Interrupt dispatcher called by startup.S */
void melis_trap_handler(void) {
    /* 1. Claim the interrupt source from PLIC */
    uint32_t active_irq = *(volatile uint32_t *)PLIC_CLAIM_REG;
    
    if (active_irq == SPI0_IRQ_NUM) {
        melis_spi0_isr();
    }
    
    /* 2. Signal Completion back to PLIC to clear execution latch */
    *(volatile uint32_t *)PLIC_CLAIM_REG = active_irq;
}

/* ========================================================================== */
/* 4. Inter-Processor Communication (IPC) Mailbox Setup                       */
/* ========================================================================== */

void melis_mbox_init(void) {
    /* Enable incoming doorbell interrupt interrupts from ARM host */
    *(volatile uint32_t *)MBOX_DSP_IRQ_EN |= 0x00000001; /* Channel 0 enable */
}

/* Trigger a doorbell to the ARM host CPU with a payload pointer */
void melis_mbox_send_msg(uint32_t sram_address) {
    /* 1. Write the target pointer to FIFO 0 */
    *(volatile uint32_t *)MBOX_MSG_FIFO(0) = sram_address;
    
    /* 2. Trigger the doorbell interrupt line routing to the ARM GIC */
    *(volatile uint32_t *)MBOX_CTRL_REG |= 0x00000001; /* Assert IRQ Channel 0 */
}

/* ========================================================================== */
/* 5. CLINT Machine Timer Setup (Standard RISC-V mtime)                       */
/* ========================================================================== */

void melis_timer_init(uint64_t ticks_delta) {
    /* 1. Read current 64-bit timer value from CLINT registers */
    uint32_t low = *(volatile uint32_t *)CLINT_MTIME_LOW;
    uint32_t high = *(volatile uint32_t *)CLINT_MTIME_HIGH;
    uint64_t current_time = ((uint64_t)high << 32) | low;
    
    /* 2. Calculate next interrupt threshold */
    uint64_t target_compare = current_time + ticks_delta;
    
    /* 3. Write compare threshold to mtimecmp register
     * Standard rule: prevent transient triggers by writing low address last
     */
    *(volatile uint32_t *)CLINT_MTIMECMP_HIGH = 0xFFFFFFFF; /* Temporary maximum */
    *(volatile uint32_t *)CLINT_MTIMECMP_LOW = (uint32_t)(target_compare & 0xFFFFFFFF);
    *(volatile uint32_t *)CLINT_MTIMECMP_HIGH = (uint32_t)(target_compare >> 32);
    
    /* 4. Enable machine timer interrupts in the `mie` CSR register */
    asm volatile("csrs mie, %0" :: "r"(1 << 7)); /* Set MTIE (Machine Timer Interrupt Enable) bit */
}

/* ========================================================================== */
/* 6. MCU Direct Memory Access (DMA) Setup                                    */
/* ========================================================================== */

typedef struct {
    uint32_t config;      /* Transfer parameters, source/dest width & burst */
    uint32_t src_addr;    /* Source address */
    uint32_t dst_addr;    /* Destination address */
    uint32_t len;         /* Transfer size in bytes */
    uint32_t parameter;   /* Reserved/extended options */
    uint32_t next_desc;   /* Linked-list pointer for descriptor chaining */
} dma_desc_t;

void melis_dma_transfer(dma_desc_t *descriptor_tcm_addr) {
    /* 1. Write descriptor structure address into channel 0 description register */
    *(volatile uint32_t *)DMA_CHAN_DESC(0) = (uint32_t)descriptor_tcm_addr;
    
    /* 2. Start the DMA transfer on channel 0 */
    *(volatile uint32_t *)DMA_CHAN_EN(0) = 0x00000001; /* Enable channel 0 */
}
