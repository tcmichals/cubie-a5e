#ifndef IOPROCESSOR_MEMORY_MAP_H
#define IOPROCESSOR_MEMORY_MAP_H

#include <stdint.h>

/*
 * ============================================================================
 *           XuanTie E907 RISC-V Memory Map (Allwinner A523 / A527 / T527)
 * ============================================================================
 */

/*
 * Memory Architecture Truths (Allwinner T527 / A527 XuanTie E907):
 * 1. PURE SRAM & DDR ARCHITECTURE: There is NO ITCM and NO DTCM on E907.
 * 2. 0x00020000 (128 KB) IS HIFI4 DSP MEMORY: Physically wired to the DSP as its local RAM.
 *    E907 must never boot or run from here!
 * 3. 0x00044000 (160 KB) IS OP-TEE / TRUSTZONE MEMORY (SRAM A2): Firewalled for secure boot.
 * 4. STAY OUT OF DSP SECONDARY RAM: 0x00400000 - 0x0044FFFF belongs to HiFi4 DSP.
 * 5. E907 FIRMWARE LIVES IN SRAM:
 *    - SRAM Space 0: 0x3FFC0000 (256 KB, r_sram @ 0x07280000)
 *    - SRAM Space 1: 0x40000000 (256 KB, r_sram1 @ 0x072C0000, enabled via REMAP_CTRL_REG[1]=1)
 *    - Continuous 512 KB on-chip SRAM from 0x3FFC0000 to 0x40040000.
 */

/* Forbidden Non-E907 Regions (DO NOT USE FOR E907!) */
#define DSP_LOCAL_RAM_BASE          0x00020000  /* 128 KB HiFi4 DSP Local Memory */
#define DSP_LOCAL_RAM_SIZE          0x00020000
#define OPTEE_SRAM_A2_BASE          0x00044000  /* 160 KB OP-TEE / TrustZone SRAM A2 */
#define OPTEE_SRAM_A2_SIZE          0x00028000

/* Verified Hardware Memory Windows for E907 (SRAM Pools) */
#define SRAM_A3_BASE                0x3FFC0000  /* Primary E907 Execution Window (Space 0) */
#define SRAM_A3_SPACE0_BASE         0x3FFC0000  /* 256 KB Dedicated SRAM Slice 0 */
#define SRAM_A3_SPACE0_SIZE         0x00040000
#define SRAM_A3_SPACE1_BASE         0x40000000  /* 256 KB Switchable SRAM Slice 1 (r_sram1) */
#define SRAM_A3_SPACE1_SIZE         0x00040000

/* Default E907 SRAM Aliases */
#define SRAM_BASE                   SRAM_A3_BASE
#define SRAM_SIZE                   (SRAM_A3_SPACE0_SIZE + SRAM_A3_SPACE1_SIZE)
#define R_SRAM_BASE                 SRAM_A3_BASE
#define R_SRAM_SIZE                 SRAM_SIZE

/* Hardware REMAP Control Register (Offset 0x364) */
#define SUNXI_DSP_CCU_BASE          0x07140000  /* DSP CCU Base on T527 (sun60iw1) */
#define SUNXI_PRCM_R_CCU_BASE       0x07010000  /* R_CCU / PRCM Base on A527 (sun55iw3) */
#define SUNXI_REMAP_CTRL_OFFSET     0x0364
#define SUNXI_REMAP_CTRL_REG_T527   (SUNXI_DSP_CCU_BASE + SUNXI_REMAP_CTRL_OFFSET)
#define SUNXI_REMAP_CTRL_REG_A527   (SUNXI_PRCM_R_CCU_BASE + SUNXI_REMAP_CTRL_OFFSET)

#define REMAP_CTRL_MCU_RAM_REMAP_BIT      (1U << 0) /* 0: DSP local RAM only for MCU_SYS; 1: share for system */
#define REMAP_CTRL_SRAMA3_2_RAM_REMAP_BIT (1U << 1) /* 0: SRAMA3_2 not shared for MCU_SYS; 1: shares for MCU_SYS */

/* IPC & Diagnostics Memory Layout within SRAM (0x3FFC0000) */
#define IPC_SHARED_MEM_BASE         0x3FFC0000
#define IPC_CRASH_DUMP_OFFSET       0x0003FF00  /* 256 B  - Fatal Trap Dump Area (Top of SRAM Space 0: 0x3FFFFF00) */
#define IPC_CRASH_DUMP_SIZE         0x0100
#define IPC_TX_RING_OFFSET          0x0100      /* 16 KB  - RISC-V -> Linux Queue (128B slots) */
#define IPC_TX_RING_SIZE            0x4000
#define IPC_RX_RING_OFFSET          0x4100      /* 16 KB  - Linux -> RISC-V Queue (128B slots) */
#define IPC_RX_RING_SIZE            0x4000
#define IPC_TRACE_BUFFER_OFFSET     0x8100      /* 32 KB  - Barectf CTF Binary Packet Buffer */
#define IPC_TRACE_BUFFER_SIZE       0x8000

/*
 * RemoteProc Trace Buffer Note:
 * g_rproc_trace_buffer is placed into section .trace_buffer in on-chip SRAM (SRAM_A3).
 * Trace buffers MUST NEVER be in DDR!
 */

/* Core SoC Peripheral Base Addresses (RISC-V Local MMIO View) */
#define PIO_BASE                    0x02000000  /* Main PIO Controller (PB-PK) */
#define CCU_BASE                    0x02001000  /* Main Clock Control Unit */
#define SUNXI_CCU_BASE              CCU_BASE    /* HAL Compatibility Alias */
#define MSGBOX_BASE                 0x03003000  /* Hardware Message Box Doorbell */
#define SPI0_BASE                   0x04025000  /* High-Speed SPI0 Controller (Port C) */
#define UART0_BASE                  0x02500000  /* UART0 Debug Serial Console */
#define UART2_BASE                  0x02500800  /* UART2 Navigation/CRSF Serial (Port B) */

/* MCU / Subsystem Control Blocks */
#define MCU_CCU_BASE                0x07010000  /* MCU Subsystem Clock/Reset Control */
#define SUNXI_MCU_PRCM_BASE         0x07102000  /* MCU Local PRCM Base */
#define R_PIO_BASE                  0x07022000  /* PRCM R_PIO Controller (PL, PM) */

/* Core Frequency Definition */
#define CPU_FREQ_HZ                 200000000ULL /* XuanTie E906/E907 Core Frequency: Up to 200 MHz */

#endif /* IOPROCESSOR_MEMORY_MAP_H */

