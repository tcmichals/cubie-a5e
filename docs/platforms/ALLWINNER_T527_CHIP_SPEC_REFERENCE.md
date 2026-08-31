# Allwinner T527 / A527 (sun55i) SoC Hardware Specification & Architecture Reference

> **Document Status**: Official Reference Compiled from *Allwinner T527 User Manual V0.92* (1,823 pages), *T527 Datasheet V0.91*, and Vendor BSP (AW2501 / Linux 5.15).  
> **Target Board**: Radxa Cubie A5E (Allwinner A527 / T527)  
> **Local User Manual Path**: `/run/media/tcmichals/projects/radxa/docs/T527/Hardware硬件类文档/芯片手册/T527_User_Manual_V0.92.pdf`

---

## 1. SoC Overview & Architecture

The Allwinner T527 / A527 (`sun55i`) is an octa-core ARM Cortex-A55 application processor designed for commercial, industrial, automotive, and avionics edge computing.

```
+-----------------------------------------------------------------------------------------+
|                                    ALLWINNER T527 (sun55i)                              |
|                                                                                         |
|  +-------------------------------------+   +-----------------------------------------+  |
|  |             CPUX Cluster            |   |               Co-Processors             |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | 4x ARM Cortex-A55 (Big Cluster)|  |   |  | Cadence Tensilica HiFi4 Audio DSP |  |  |
|  |  | L1: 32KB I / 32KB D, L2: 128KB|  |   |  | (Audio, DSP algorithms)           |  |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | 4x ARM Cortex-A55 (Little Cl.)|  |   |  | XuanTie E907 / E902 RISC-V Core   |  |  |
|  |  | L1: 32KB I / 32KB D, L2: 64KB |  |   |  | (Real-time avionics / ARISC / SCP)|  |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | DynamIQ Shared Unit (DSU)     |  |   |  +-----------------------------------+  |  |
|  |  | L3 Cache: 512 KB              |  |   |  | 2.0 TOPS Neural Processing Unit   |  |  |
|  +-------------------------------------+   +-----------------------------------------+  |
|                                                                                         |
|  +-----------------------------------------------------------------------------------+  |
|  |                                System Interconnect & Memory                       |  |
|  |  - ARM GIC-600 Interrupt Controller (GICv3 architecture, 256 SPIs)                |  |
|  |  - Memory Controller: DDR3/3L, DDR4, LPDDR3, LPDDR4/4x (Up to 4 GiB space)        |  |
|  |  - ARM Mali-G57 MC1 GPU                                                           |  |
|  +-----------------------------------------------------------------------------------+  |
|                                                                                         |
|  +-----------------------------------+   +-------------------------------------------+  |
|  |       High-Speed Connectivity     |   |            Peripherals & IO               |  |
|  |  - 1x PCIe 2.1 (1 Lane)           |   |  - 8x High-Speed UART (UART0 debug)       |  |
|  |  - 1x USB 3.1 Gen1 / PCIe Combo   |   |  - 6x TWI / I2C Buses + 3x S_TWI          |  |
|  |  - 1x USB 2.0 DRD (OTG0)          |   |  - 3x SPI Controllers + 1x S_SPI          |  |
|  |  - 1x USB 2.0 Host (EHCI1/OHCI1)  |   |  - 2x CAN-FD Controllers (CAN0, S_CAN)    |  |
|  |  - 2x GMAC (Dual 1 Gbps Ethernet) |   |  - PIO: PA, PB, PC, PD, PE, PF, PG, PH,  |  |
|  |  - 3x SD/MMC (SMHC0 SD, SMHC2 eMMC|   |         PI, PL, PM Banks                  |  |
|  +-----------------------------------+   +-------------------------------------------+  |
+-----------------------------------------------------------------------------------------+
```

---

## 2. Complete Physical Memory Map (T527 CPUX View)

| Module Name | Base Address | End Address | Size | Description / Notes |
| :--- | :--- | :--- | :--- | :--- |
| **BROM & SRAM** | | | | |
| `BROM` | `0x00000000` | `0x0001FFFF` | 128 KB | On-chip Boot ROM |
| `SRAM_A1` | `0x00020000` | `0x00027FFF` | 32 KB | SRAM A1 (Boot0 execution) |
| `SRAM_A2` | `0x00040000` | `0x00057FFF` | 96 KB | SRAM A2 (CPUS / Secure execution) |
| `SRAM_A3` | `0x07280000` | `0x0737FFFF` | 1024 KB | DSP / NPU / MCU SRAM window |
| **System & CCU** | | | | |
| `CCU` | `0x02001000` | `0x02001FFF` | 4 KB | Clock Controller Unit & System Resets |
| `GPIO (PIO)` | `0x02000000` | `0x020007FF` | 2 KB | Main Pin Controller (`PA` - `PI`) |
| `SYSCTRL` | `0x03000000` | `0x03000FFF` | 4 KB | System Control Registers |
| `DMAC` | `0x03002000` | `0x03002FFF` | 4 KB | 16-Channel Central DMA Controller |
| `CPUX_MSGBOX` | `0x03003000` | `0x03003FFF` | 4 KB | Hardware Mailbox (CPUX <-> DSP / RISC-V) |
| `SPINLOCK` | `0x03005000` | `0x03005FFF` | 4 KB | Hardware Spinlocks |
| `SID (eFuse)` | `0x03006000` | `0x03006FFF` | 4 KB | eFuse & Chip ID Controller |
| `CPUX_TIMER` | `0x03008000` | `0x030083FF` | 1 KB | Standard CPUX Timers |
| `GIC-600` | `0x03400000` | `0x034EFFFF` | 15*64 KB | ARM GICv3 Distributor & Redistributor |
| **Low-Speed IO** | | | | |
| `UART0` | `0x02500000` | `0x025003FF` | 1 KB | **Debug Serial Console** (PB9/PB10) |
| `UART1`..`UART7` | `0x02500400` | `0x02501FFF` | 1 KB each | High-Speed UARTs (1 to 7) |
| `TWI0`..`TWI5` | `0x02502000` | `0x025037FF` | 1 KB each | I2C / TWI Buses (0 to 5) |
| `SPI0` | `0x04025000` | `0x04025FFF` | 4 KB | SPI0 Controller (SPI NOR / Boot) |
| `SPI1` / `SPI2` | `0x04026000` | `0x04027FFF` | 4 KB each | SPI Controllers 1 and 2 |
| **Storage & High-Speed IO** | | | | |
| `SMHC0` | `0x04020000` | `0x04020FFF` | 4 KB | **SD Card Controller** (SD 3.0) |
| `SMHC1` | `0x04021000` | `0x04021FFF` | 4 KB | SDIO Wi-Fi Controller |
| `SMHC2` | `0x04022000` | `0x04022FFF` | 4 KB | **eMMC 5.1 Controller** (HS400) |
| `USB0 (OTG/EHCI0)`| `0x04100000`| `0x041FFFFF` | 1 MB | USB 2.0 DRD / EHCI0 / OHCI0 |
| `USB1 (Host/EHCI1)`| `0x04200000`| `0x042FFFFF` | 1 MB | USB 2.0 Host / EHCI1 / OHCI1 |
| `GMAC0` | `0x04500000` | `0x0450FFFF` | 64 KB | **1 Gbps Ethernet MAC 0** (RGMII / RMII) |
| `GMAC1` | `0x04510000` | `0x0451FFFF` | 64 KB | **1 Gbps Ethernet MAC 1** (Dual Ethernet) |
| `PCIE` | `0x04800000` | `0x04CFFFFF` | 5 MB | PCIe 2.1 Controller |
| `USB3` | `0x04D00000` | `0x04EFFFFF` | 2 MB | USB 3.1 Gen1 Controller |
| **CPUS & Co-Processor Domain** | | | | |
| `STBY_PRCM` | `0x07010000` | `0x0701FFFF` | 64 KB | Always-On Power Reset Clock Mgmt |
| `S_GPIO (R-PIO)` | `0x07022000` | `0x070227FF` | 2 KB | Power Domain GPIO (`PL`, `PM` banks) |
| `S_UART0`/`S_UART1`| `0x07080000`| `0x070807FF` | 1 KB each | CPUS Dedicated UARTs |
| `RTC` | `0x07090000` | `0x070903FF` | 1 KB | Real-Time Clock & Alarm |
| `DSP_CFG` | `0x07100000` | `0x071003FF` | 1 KB | HiFi4 DSP Subsystem Config |
| `MCU_PRCM` | `0x07102000` | `0x07102FFF` | 4 KB | DSP / MCU Clock & Reset Control |
| `RISCV_CFG` | `0x07130000` | `0x07130FFF` | 4 KB | **XuanTie E907 RISC-V Configuration** |
| `RISCV_MSGBOX` | `0x07136000` | `0x07136FFF` | 4 KB | RISC-V Dedicated Mailbox |
| **System DRAM** | | | | |
| **DRAM Space** | `0x40000000` | `0x13FFFFFFF` | 4 GiB Max | Physical RAM (`0x40000000` base) |

---

## 3. Key Differences Between T527 (sun55i) and A733 (sun60i)

| Subsystem / Feature | Allwinner T527 / A527 (`sun55i`) | Allwinner A733 (`sun60i`) |
| :--- | :--- | :--- |
| **CPUX Cores** | 8x Cortex-A55 @ 1.8 GHz | 2x Cortex-A76 @ 2.0 GHz + 6x Cortex-A55 @ 1.8 GHz |
| **DSP Co-Processor** | Cadence Tensilica HiFi4 DSP | None (Replaced by dual RISC-V / NPU architecture) |
| **RISC-V Co-Processor** | XuanTie E907 / E902 (RV32IMAC) | XuanTie E907 / E902 (RV32IMAFDCP with FPU/Double) |
| **Ethernet Interfaces** | **Dual GMAC** (GMAC0 @ `0x04500000`, GMAC1 @ `0x04510000`) | **Single GMAC** (GMAC0 @ `0x04508000`) |
| **CCU Base Address** | `0x02001000` (Size: 4 KB) | `0x02002000` (Size: 16 KB) |
| **DMAC Base Address** | `0x03002000` (Size: 4 KB) | `0x04601000` / `0x04024000` (Dual DMAC) |
| **DRAM Limit** | Up to 4 GiB | Up to 16 GiB (Cubie A7A has 6 GiB LPDDR5) |
| **GPU** | ARM Mali-G57 MC1 | Imagination BXM-4-64 MC1 |
| **PCIe Interface** | PCIe 2.1 (Gen2, 5 Gbps) | PCIe 3.0 (Gen3, 8 Gbps) |

---

## 4. GPIO & Pinmux Breakdown (T527)

| Port Bank | Base Address | Pin Count | Primary Function Multiplexing |
| :--- | :--- | :--- | :--- |
| **Port A (PA)** | `0x02000000` | PA0 – PA16 | PWM, TWI0, Audio Codec |
| **Port B (PB)** | `0x02000030` | PB0 – PB12 | UART0 (PB9/PB10), TWI1, SPI1 |
| **Port C (PC)** | `0x02000060` | PC0 – PC16 | SMHC0 (SD Card), SPI0 (NOR Flash), NAND |
| **Port D (PD)** | `0x02000090` | PD0 – PD22 | Parallel CSI, RGB Display, LVDS |
| **Port E (PE)** | `0x020000C0` | PE0 – PE17 | GMAC0 RGMII / RMII (PE0–PE15) |
| **Port F (PF)** | `0x020000F0` | PF0 – PF6 | SMHC0 Boot Detect / Fast JTAG |
| **Port G (PG)** | `0x02000120` | PG0 – PG15 | SMHC1 (SDIO Wi-Fi), UART1, GMAC1 (RMII) |
| **Port H (PH)** | `0x02000150` | PH0 – PH19 | I2S0, TWI2, TWI3, GMAC PHY Power |
| **Port I (PI)** | `0x02000180` | PI0 – PI16 | High-Speed UARTs, CAN-FD |
| **Port L (PL)** | `0x07022000` | PL0 – PL12 | S_UART0, S_TWI0, PMIC IRQ, S_CAN |
| **Port M (PM)** | `0x07022030` | PM0 – PM5 | S_PWM, Power Key, Wakeup |

---

## 5. Storage Partitioning & Boot Rules (Radxa Cubie A5E)

- **Bootloader Image**: `radxa_a527_bootloader.bin` (or `radxa_t527_bootloader.bin`) placed at sector 0 (with holes `(440; 512)`).
- **Boot Partition (`boot.vfat`)**: Starts at offset **16 MB** (`16384 KiB`). Contains `boot.scr`, `sun55i-a527-cubie-a5e.dtb`, and `Image`.
- **Rootfs Partition (`rootfs.ext4`)**: Starts immediately following Partition 1.
