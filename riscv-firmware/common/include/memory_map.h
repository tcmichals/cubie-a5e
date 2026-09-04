#ifndef IOPROCESSOR_MEMORY_MAP_H
#define IOPROCESSOR_MEMORY_MAP_H

#include <stdint.h>

/*
 * ============================================================================
 *           XuanTie E907 RISC-V Memory Map (Allwinner A523 / A527 / T527)
 * ============================================================================
 */

/* Tightly-Coupled Memories (Zero-Wait-State) */
#define ITCM_BASE                   0x00000000  /* 64 KB Instruction TCM */
#define ITCM_SIZE                   0x00010000
#define DTCM_BASE                   0x00010000  /* 64 KB Data TCM */
#define DTCM_SIZE                   0x00010000

/* Shared System SRAM C (On-Chip High-Speed Interconnect) */
#define SRAM_C_BASE                 0x00020000  /* 128 KB Total SRAM C */
#define SRAM_C_SIZE                 0x00020000

/* IPC & Diagnostics Memory Layout within SRAM C (0x00020000) */
#define IPC_SHARED_MEM_BASE         0x00020000
#define IPC_CRASH_DUMP_OFFSET       0x0000      /* 256 B  - Fatal Trap Dump Area */
#define IPC_CRASH_DUMP_SIZE         0x0100
#define IPC_TX_RING_OFFSET          0x0100      /* 16 KB  - RISC-V -> Linux Queue (128B slots) */
#define IPC_TX_RING_SIZE            0x4000
#define IPC_RX_RING_OFFSET          0x4100      /* 16 KB  - Linux -> RISC-V Queue (128B slots) */
#define IPC_RX_RING_SIZE            0x4000
#define IPC_TRACE_BUFFER_OFFSET     0x8100      /* 32 KB  - Barectf CTF Binary Packet Buffer */
#define IPC_TRACE_BUFFER_SIZE       0x8000

/* Linux-reserved normal DDR, directly addressable by both A5E and A7A E907. */
#define RPROC_TRACE_BUFFER_BASE     0x4E000000
#define RPROC_TRACE_BUFFER_SIZE     0x00008000

/* Core SoC Peripheral Base Addresses (RISC-V Local MMIO View) */
#define PIO_BASE                    0x02000000  /* Main PIO Controller (PB-PK) */
#define CCU_BASE                    0x02001000  /* Main Clock Control Unit */
#define MSGBOX_BASE                 0x03003000  /* Hardware Message Box Doorbell */
#define SPI0_BASE                   0x04025000  /* High-Speed SPI0 Controller (Port C) */
#define UART0_BASE                  0x02500000  /* UART0 Debug Serial Console */
#define UART2_BASE                  0x02500800  /* UART2 Navigation/CRSF Serial (Port B) */

/* MCU / Subsystem Control Blocks */
#define MCU_CCU_BASE                0x07010000  /* MCU Subsystem Clock/Reset Control */
#define R_PIO_BASE                  0x07022000  /* PRCM R_PIO Controller (PL, PM) */

/* Core Frequency Definition */
#define CPU_FREQ_HZ                 200000000ULL /* XuanTie E906/E907 Core Frequency: Up to 200 MHz */

#endif /* IOPROCESSOR_MEMORY_MAP_H */

