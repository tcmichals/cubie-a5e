# Allwinner A733 (sun60iw2) SoC Hardware Specification & Architecture Reference

> **Document Status**: Official Reference Compiled from *Allwinner A733 User Manual V0.92* (1,982 pages), *A733 Datasheet V0.92*, and Vendor BSP (AW2501 / Linux 5.15).  
> **Target Board**: Radxa Cubie A7A (Allwinner A733)  
> **Local User Manual Path**: `/run/media/tcmichals/projects/radxa/docs/A733/Hardware硬件类文档/芯片手册/A733_User Manual_V0.92.pdf`

---

## 1. SoC Overview & Architecture

The Allwinner A733 is an 8-core, heterogeneous 64-bit application processor targeting high-performance embedded systems, edge computing, robotics, and tablets.

```
+-----------------------------------------------------------------------------------------+
|                                    ALLWINNER A733 (sun60iw2)                            |
|                                                                                         |
|  +-------------------------------------+   +-----------------------------------------+  |
|  |             CPUX Cluster            |   |               Co-Processors             |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | 2x ARM Cortex-A76 @ 2.0 GHz   |  |   |  | XuanTie E907 / E902 RISC-V Core   |  |  |
|  |  | L1: 64KB I / 64KB D, L2: 256KB|  |   |  | (Avionics / ARISC / Power Mgmt)   |  |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | 6x ARM Cortex-A55 @ 1.8 GHz   |  |   |  | 3.0 TOPS Neural Processing Unit   |  |  |
|  |  | L1: 32KB I / 32KB D, L2: 128KB|  |   |  | (NPU Accelerator)                 |  |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | L3 Shared Cache: 1 MB         |  |   |  +-----------------------------------+  |  |
|  |  +-------------------------------+  |   |  | Imagination BXM-4-64 MC1 GPU      |  |  |
|  +-------------------------------------+   +-----------------------------------------+  |
|                                                                                         |
|  +-----------------------------------------------------------------------------------+  |
|  |                                System Interconnect & Memory                       |  |
|  |  - GICv3 Interrupt Controller (GIC-600, 256 SPIs, ITS Support)                    |  |
|  |  - Memory Controller: LPDDR4 / LPDDR4x / LPDDR5 (Up to 16 GiB physical space)     |  |
|  |  - Dual DMAC Engine (DMAC0 @ 0x04601000, DMAC1 @ 0x04024000)                      |  |
|  +-----------------------------------------------------------------------------------+  |
|                                                                                         |
|  +-----------------------------------+   +-------------------------------------------+  |
|  |       High-Speed Connectivity     |   |            Peripherals & IO               |  |
|  |  - 1x PCIe 3.0 Dual Mode (1 Lane) |   |  - 7x High-Speed UART (UART0 debug)       |  |
|  |  - 1x USB 3.1 Gen2 DRD            |   |  - 13x TWI / I2C Controllers              |  |
|  |  - 1x USB 2.0 DRD (OTG0)          |   |  - 4x High-Speed SPI (SPI0 NOR, SPI1..3)  |  |
|  |  - 1x USB 2.0 Host (EHCI1/OHCI1)  |   |  - PIO: PA, PB, PC, PD, PE, PF, PG, PH,  |  |
|  |  - 1x GMAC (1 Gbps RGMII / RMII)  |   |         PJ, PK, PL, PM Banks              |  |
|  |  - 4x SD/MMC (SMHC0 SD, SMHC2 eMMC|   |  - 2x 10-channel PWM                      |  |
|  +-----------------------------------+   +-------------------------------------------+  |
+-----------------------------------------------------------------------------------------+
```

---

## 2. Complete Physical Memory Map (CPUX View)

Allwinner A733 maps peripheral registers into the lower 32-bit physical address space below `0x40000000`, with DRAM extending from `0x40000000` upwards.

| Module Name | Base Address | End Address | Size | Description / Notes |
| :--- | :--- | :--- | :--- | :--- |
| **BROM & SRAM** | | | | |
| `FIXS_BROM` | `0x00000000` | `0x00010FFF` | 68 KB | Fixed Secure Boot ROM |
| `HS_BROM` | `0x00011000` | `0x00016FFF` | 24 KB | High Security Boot ROM |
| `NS_BROM` | `0x00017000` | `0x00021FFF` | 44 KB | Non-Secure Boot ROM (FEL mode) |
| `SRAM_A2` | `0x00040000` | `0x00073FFF` | 208 KB | Internal SRAM (16KB CPUS, 192KB CPUX) |
| `SHARED_SRAM` | `0x00074000` | `0x000F3FFF` | 512 KB | Shared SRAM (NPU / co-processors) |
| **Core System & CCU** | | | | |
| `GPIO (PIO)` | `0x02000000` | `0x02001FFF` | 8 KB | Main Pin Controller (`PA` - `PK`) |
| `CCU` | `0x02002000` | `0x02005FFF` | 16 KB | Clock Controller Unit & System Resets |
| `WDT0` | `0x02050000` | `0x02050FFF` | 4 KB | CPUX Watchdog Timer 0 |
| `TIMER1` | `0x02052000` | `0x02052FFF` | 4 KB | System Timer 1 |
| `SYSCTRL` | `0x03000000` | `0x03000FFF` | 4 KB | System Control Registers |
| `CPUX_MSGBOX` | `0x03004000` | `0x03004FFF` | 4 KB | Hardware Mailbox (CPUX <-> RISC-V) |
| `SPINLOCK` | `0x03005000` | `0x03005FFF` | 4 KB | Hardware Spinlocks |
| `SID (eFuse)` | `0x03006000` | `0x03006FFF` | 4 KB | Security ID & eFuse Controller |
| `TIMER0` | `0x03009000` | `0x03009FFF` | 4 KB | Standard System Timer 0 |
| `GIC-600 Dist`| `0x03400000` | `0x034FFFFF` | 1 MB | ARM GICv3 Distributor |
| `GIC-600 Rdist`| `0x03500000` | `0x0357FFFF` | 512 KB | ARM GICv3 Redistributor (8 Cores) |
| `IOMMU0` | `0x03900000` | `0x0390FFFF` | 64 KB | System IOMMU 0 |
| **Low-Speed Peripherals** | | | | |
| `UART0` | `0x02500000` | `0x02500FFF` | 4 KB | **Debug Serial Console** (115200 8N1) |
| `UART1`..`UART6` | `0x02501000` | `0x02506FFF` | 4 KB each | General Purpose High-Speed UARTs |
| `TWI0`..`TWI12` | `0x02510000` | `0x0251CFFF` | 4 KB each | I2C / TWI Buses (0 to 12) |
| `GPADC0` | `0x02521000` | `0x02521FFF` | 4 KB | 7-Channel 12-bit General Purpose ADC |
| `PWM0` / `PWM1` | `0x02527000` | `0x02528FFF` | 4 KB each | 10-Channel PWM Controllers |
| `SPI0` | `0x02540000` | `0x02540FFF` | 4 KB | SPI Controller 0 (SPI NOR / Boot) |
| `SPI1`..`SPI3` | `0x02541000` | `0x02543FFF` | 4 KB each | SPI Controllers 1, 2, 3 |
| **Storage & High-Speed IO** | | | | |
| `SMHC0` | `0x04020000` | `0x04020FFF` | 4 KB | **SD Card Controller** (SD 3.0, SDR104) |
| `SMHC1` | `0x04021000` | `0x04021FFF` | 4 KB | SDIO Wi-Fi Controller |
| `SMHC2` | `0x04022000` | `0x04022FFF` | 4 KB | **eMMC 5.1 Controller** (HS400) |
| `SMHC3` | `0x04023000` | `0x04023FFF` | 4 KB | Additional SD/MMC Controller |
| `DMAC0` / `DMAC1` | `0x04601000` | `0x04602FFF` | 8 KB | 16-Channel High-Speed DMA Engines |
| `USB0 (OTG/EHCI0)`| `0x04100000`| `0x041FFFFF` | 1 MB | USB 2.0 DRD / EHCI0 / OHCI0 |
| `USB1 (Host/EHCI1)`| `0x04200000`| `0x042FFFFF` | 1 MB | USB 2.0 Host / EHCI1 / OHCI1 |
| `GMAC0` | `0x04508000` | `0x0450FFFF` | 64 KB | **1 Gbps Ethernet MAC** (RGMII / RMII) |
| `PCIE0` | `0x06400000` | `0x064FFFFF` | 5 MB | PCIe 3.0 Controller (Dual Mode) |
| `USB3P1_DRD` | `0x06A00000` | `0x06BFFFFF` | 2 MB | USB 3.1 Gen2 Controller |
| `COMBPHY0/1` | `0x06C80000` | `0x06CBFFFF` | 64 KB each | Combo PHY (PCIe / USB3 / eDP) |
| **CPUS Domain (PRCM & RISC-V)** | | | | |
| `S_PRCM (R-CCU)` | `0x07010000` | `0x0701FFFF` | 64 KB | Power Reset Clock Mgmt (Always-On) |
| `S_GPIO (R-PIO)` | `0x07025000` | `0x07026FFF` | 8 KB | Power Domain GPIO (`PL`, `PM` banks) |
| `S_UART0`/`S_UART1`| `0x07080000`| `0x07081FFF` | 4 KB each | CPUS Dedicated UARTs |
| `RTC` | `0x07090000` | `0x07090FFF` | 4 KB | Real-Time Clock & Alarm |
| **System DRAM** | | | | |
| **DRAM Space** | `0x40000000` | `0x43FFFFFFF` | 16 GiB Max | Physical RAM (`0x40000000` base) |

---

## 3. Clock Controller Unit (CCU & PRCM)

The A733 features two distinct clock controller domains:
1. **Main CCU** (`0x02002000`, 16 KB): Feeds all high-speed buses, CPU clusters, GPU, NPU, DRAM, and standard peripherals.
2. **R-CCU / PRCM** (`0x07010000`, 64 KB): Feeds the Always-On power domain, RTC, PMIC interfaces, and the XuanTie E907 co-processor.

### Key CCU Clock & Reset Offsets
| Subsystem | Register Offset | Clock Enable Bit | Reset Deassert Bit | Gate Type / Notes |
| :--- | :--- | :--- | :--- | :--- |
| `AHB_SYS` | `0x0500` | BIT(0) | BIT(16) | AHB Main Bus Clock |
| `APB0_SYS` | `0x0520` | BIT(0) | BIT(16) | APB0 Bus Clock |
| `APB1_SYS` | `0x0528` | BIT(0) | BIT(16) | APB1 Bus Clock |
| `MMC0 (SD)` | `0x0D00` / `0x0D0C` | `0x0D0C` BIT(0) | `0x0D0C` BIT(16) | MMC0 Mod Clock + AHB Bus Gate |
| `MMC2 (eMMC)` | `0x0D04` / `0x0D0C` | `0x0D0C` BIT(2) | `0x0D0C` BIT(18) | MMC2 Mod Clock + AHB Bus Gate |
| `UART0` | `0x0E00` | BIT(0) | BIT(16) | APB Bus Gate + Reset |
| `UART1`..`UART6` | `0x0E04`..`0x0E18` | BIT(0) | BIT(16) | APB Bus Gate + Reset |
| `USB0 (EHCI0)`| `0x1304` | BIT(0) | BIT(16) | USB0 AHB Bus Gate |
| `USB0 (OHCI0)`| `0x1304` | BIT(4) | BIT(20) | USB0 OHCI 12M Clock Gate |
| `USB1 (EHCI1)`| `0x130C` | BIT(0) | BIT(16) | USB1 AHB Bus Gate |
| `USB1 (OHCI1)`| `0x130C` | BIT(4) | BIT(20) | USB1 OHCI 12M Clock Gate |
| `GMAC0` | `0x141C` | BIT(0) | BIT(16), BIT(17) | GMAC AHB Bus Gate + EPHY Reset |

> [!IMPORTANT]
> **CCF Unique Pointer Rule**: Every entry in the Linux Common Clock Framework array (`hw_clks.hws[]`) must point to an independent `struct clk_hw`. Shared struct pointers will cause the CCF core to zero `hw->init`, triggering a NULL pointer dereference during probe.

---

## 4. GPIO & Pinmux Controller (PIO)

The main PIO controller is located at `0x02000000`, while the low-power R-PIO is located at `0x07025000`.

### Pin Banks Breakdown
| Port Bank | Controller Base | Pin Count | Supported Multiplex Functions |
| :--- | :--- | :--- | :--- |
| **Port B (PB)** | `0x02000030` | PB0 – PB12 | UART0 (PB9/PB10), TWI0, PWM, SPI1 |
| **Port C (PC)** | `0x02000060` | PC0 – PC16 | SMHC0 (SD Card), SPI0 (NOR Flash), NAND |
| **Port D (PD)** | `0x02000090` | PD0 – PD22 | Parallel CSI, RGB Display, LVDS |
| **Port E (PE)** | `0x020000C0` | PE0 – PE17 | GMAC0 RGMII (PE0–PE15), TWI1 |
| **Port F (PF)** | `0x020000F0` | PF0 – PF6 | SMHC0 SD Card Boot Detect / Fast JTAG |
| **Port G (PG)** | `0x02000120` | PG0 – PG15 | SMHC1 (SDIO Wi-Fi), UART1, SPI2 |
| **Port H (PH)** | `0x02000150` | PH0 – PH19 | I2S0, TWI2, TWI3, GMAC PHY Power (`PH16`) |
| **Port J (PJ)** | `0x02000180` | PJ0 – PJ11 | MIPI DSI, Camera Sync |
| **Port K (PK)** | `0x020001B0` | PK0 – PK10 | Audio Codec, DMIC |
| **Port L (PL)** | `0x07025000` | PL0 – PL12 | S_UART0, S_TWI0, PMIC Interrupt |
| **Port M (PM)** | `0x07025030` | PM0 – PM5 | S_PWM, Power Key, Wakeup |

---

## 5. ARM GICv3 Interrupt Architecture

The interrupt controller is a fully compliant ARM GIC-600 supporting GICv3 architecture with 256 Shared Peripheral Interrupts (SPIs), 32 SGIs/PPIs per core, and LPI/ITS message-signaled interrupts.

- **GICD (Distributor Base)**: `0x03400000` (Size: 64 KB / `0x10000`)
- **GICR (Redistributor Base)**: `0x03500000` (Size: 512 KB / `0x80000`, 64KB per CPU core x 8)
- **GITS (ITS Base)**: `0x03440000`

### Selected Hardware Interrupt Numbers (GICv3 SPIs)
| Hardware SPI ID | DTS Interrupt (`GIC_SPI ID`) | Subsystem / IRQ Source |
| :--- | :--- | :--- |
| **SPI 0** | `<GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>` | UART0 (Debug Console) |
| **SPI 1..6** | `<GIC_SPI 1..6 IRQ_TYPE_LEVEL_HIGH>` | UART1 through UART6 |
| **SPI 14** | `<GIC_SPI 14 IRQ_TYPE_LEVEL_HIGH>` | TWI0 |
| **SPI 32** | `<GIC_SPI 32 IRQ_TYPE_LEVEL_HIGH>` | SMHC0 (SD Card) |
| **SPI 34** | `<GIC_SPI 34 IRQ_TYPE_LEVEL_HIGH>` | SMHC2 (eMMC) |
| **SPI 48** | `<GIC_SPI 48 IRQ_TYPE_LEVEL_HIGH>` | USB0 DRD / OTG |
| **SPI 49** | `<GIC_SPI 49 IRQ_TYPE_LEVEL_HIGH>` | USB0 EHCI Host |
| **SPI 50** | `<GIC_SPI 50 IRQ_TYPE_LEVEL_HIGH>` | USB0 OHCI Host |
| **SPI 52** | `<GIC_SPI 52 IRQ_TYPE_LEVEL_HIGH>` | USB1 EHCI Host |
| **SPI 53** | `<GIC_SPI 53 IRQ_TYPE_LEVEL_HIGH>` | USB1 OHCI Host |
| **SPI 62** | `<GIC_SPI 62 IRQ_TYPE_LEVEL_HIGH>` | GMAC0 Ethernet |
| **SPI 89** | `<GIC_SPI 89 IRQ_TYPE_LEVEL_HIGH>` | System Timer 0 |
| **SPI 120** | `<GIC_SPI 120 IRQ_TYPE_LEVEL_HIGH>` | XuanTie E907 Mailbox |

---

## 6. System Boot Sequence & Geometry

The Allwinner A733 BootROM (BROM) executes on **ARM Cortex-A55 Core 0** upon power-on-reset.

```
+-----------------------------------------------------------------------------------------+
|                                  A733 BOOT PIPELINE                                     |
|                                                                                         |
|  [Power On] -> [BootROM (BROM)]                                                         |
|                     |                                                                   |
|                     v                                                                   |
|            Read BOOT_MODE / FEL Pin                                                     |
|                     |                                                                   |
|         +-----------+-----------+                                                       |
|         | (FEL pulled low)      | (Normal Media Boot)                                   |
|         v                       v                                                       |
|    [USB FEL Mode]     Scan Media Priority:                                              |
|                       1. SMHC0 (SD Card @ Sector 0 / 16 Sectors)                        |
|                       2. SMHC2 (eMMC User / Boot Partitions)                            |
|                       3. SPI NOR Flash (Quad Mode -> Single Mode)                       |
|                                 |                                                       |
|                                 v (Load 16MB Bootloader Blob)                           |
|                       [Stage 1: Boot0 / SPL] (Loads into SRAM_A2 @ 0x00040000)         |
|                       - LPDDR4/5 DRAM Init & Training                                   |
|                       - Power PMIC (AXP717 / AXP1530) Setup                             |
|                                 |                                                       |
|                                 v                                                       |
|                       [Stage 2: ARM Trusted Firmware (BL31)]                            |
|                       - Runs at EL3 (PSCI v1.1, SMC Dispatcher)                         |
|                       - Hardcoded Exception Entry @ 0x40200000                          |
|                                 |                                                       |
|                                 v                                                       |
|                       [Stage 3: U-Boot 2018.07 / 2024.x]                                |
|                       - Boots XuanTie E907 RISC-V co-processor (SCP)                    |
|                       - Loads boot.scr + Image + DTB from FAT @ 16MB Offset             |
|                                 |                                                       |
|                                 v                                                       |
|                       [Stage 4: Linux Kernel (7.1 PREEMPT_RT)]                          |
|                       - Entry Address: 0x40200000                                       |
|                       - GICv3 Init -> CCU/PRCM Probe -> Rootfs Mount                    |
+-----------------------------------------------------------------------------------------+
```

### Storage Partitioning Rules (Radxa Cubie A7A)
- **Sector 0 – 16 MB (`0` to `32768` sectors)**: Reserved strictly for `radxa_a733_bootloader.bin` (Holes: `(440; 512)` to preserve MBR partition table without corrupting SPL DRAM training tables).
- **Partition 1 (`boot.vfat`)**: Starts at offset **16 MB** (`16384 KiB`). Contains `boot.scr`, `sun60i-a733-cubie-a7a.dtb`, and `Image`.
- **Partition 2 (`rootfs.ext4`)**: Starts at end of Partition 1. Contains target userspace.
