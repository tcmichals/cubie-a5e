# 🚀 Platform Guide: Radxa Cubie A7A (Allwinner A733 / `sun60iw2`)

> [!NOTE]
> **Active Bring-Up Debug Log**: For complete step-by-step forensic traces, serial output logs, and hardware discoveries, see [`CUBIE_A7A_DEBUG_LOG.md`](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/platforms/CUBIE_A7A_DEBUG_LOG.md).
> **Hardware Reference Library**: Full vendor kernel DTBs, firmware binaries, and the official datasheet are archived in [`vendor-a733-reference/`](file:///home/tcmichals/projects/cubie/vendor-a733-reference/) (Datasheet PDF: [`A733_Datasheet_V0.93.pdf`](file:///home/tcmichals/projects/cubie/vendor-a733-reference/A733_Datasheet_V0.93.pdf)).

This document is the authoritative hardware, bootloader, firmware provenance, and bring-up specification for the **Radxa Cubie A7A** flight controller. It details the SoC architecture, the 2-stage hybrid boot architecture, firmware provenance (TF-A, U-Boot, Boot0, Linux), storage geometry, memory map, GICv3 interrupt controller configuration, and verified physical hardware registers.

---

## 1. Hardware Architecture Specification

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner A733 (`sun60iw2p1`) | 2× Cortex-A76 (Big @ 2.0 GHz) + 6× Cortex-A55 (LITTLE @ 1.8 GHz) DynamIQ Cluster |
| **RAM** | 6 GiB LPDDR5 (400 MHz to 1800 MHz) | 4-PState dynamic PHY training executed by `boot0` in SRAM |
| **Co-Processor** | **XuanTie E902 RISC-V** (RV32IMC @ up to 200 MHz, per Linux-Sunxi) | No DSP; managed via Linux `remoteproc` framework (`sunxi_rproc.c`) executing from SRAM A2 |
| **Interrupt Controller** | ARM GIC-600 (GICv3) | Distributor: `0x03400000`, Redistributors: `0x03460000`, ITS: `0x03440000` |
| **Clock Controller (CCU)** | Main CCU: `0x02002000`, PRCM R_CCU: `0x07010000` | Verified against A733 silicon memory map |
| **Mainline Kernel** | **Linux 7.1 PREEMPT_RT** | Ingests CCU, RTC, Pinctrl, and GICv3 patches |
| **Mainline U-Boot** | **U-Boot 2026.01 (`allwinner/A733/boot-2026.01`)** | Maintained by Yixun Lan (dlan17); `radxa-cubie-a7a_defconfig` |
| **TF-A (BL31)** | **ARM Trusted Firmware (`plat/allwinner/sun60i_a733`)** | Secure EL3 monitor; sets `ICC_SRE_EL3 = 0x7`, `ICC_SRE_EL2 = 0x7`, PSCI 1.1 |
| **Vendor DRAM Blob** | **`boot0_sdcard_sun60iw2p1_lpddr5.bin`** | Runs in internal SRAM at Sector 256 (128 KB offset) |
| **TOC1 Pack Tool** | **`dragonsecboot`** | Packages Mainline U-Boot SPL + FIT into `boot_package.fex` |
| **Ethernet Subsystem** | **Dual Gigabit Ethernet (DWMAC 5.20 / GMAC-210)** | GMAC0 (`0x04500000`) & GMAC1 (`0x04510000`) |
| **USB Subsystem** | **Dual USB 2.0 (EHCI/OHCI) + USB 3.1 DWC3** | USB0 (`0x04101000`), USB1 (`0x04200000`), DWC3 XHCI (`0x06a00000`) |
| **Wi-Fi / BT Transport** | **AIC8800 Wi-Fi 6 + BT 5.2 (SDIO / USB)** | SDIO on MMC2 (`0x04022000`) or USB (`0xA69C:0x8800`) |
| **Storage Geometry** | Sector 256 (128 KB) / Sector 24576 (12.0 MB) | Strictly block-aligned to 512-byte raw sectors |

---

## 2. Allwinner A733 Datasheet & TRM Hardware Map

From Chapter 4 of the **Allwinner A733 Datasheet V0.93** and vendor kernel register verification:

### A. Memory-Mapped Base Registers

```
+───────────────────────+──────────────────────────+─────────────────────────────────────────+
| Hardware Subsystem    | Physical Address Window  | Silicon Functional Description          |
+───────────────────────+──────────────────────────+─────────────────────────────────────────+
| Main CCU              | 0x02002000 - 0x02003FFF  | Main Clock Control Unit (Gates/Resets)  |
| Main PIO (Pinctrl)    | 0x02000000 - 0x020007FF  | Ports B, C, D, E, F, G, H, J, K         |
| UART0 Serial Console  | 0x02500000 - 0x025003FF  | Synopsys DW APB UART (115200 8N1)       |
| Mailbox / Msgbox      | 0x03004000 & 0x07094000  | Inter-Processor Communication Mailbox   |
| ARM GICv3 Distributor | 0x03400000 - 0x0340FFFF  | GIC-600 Distributor                     |
| ARM GICv3 Redistrib.  | 0x03460000 - 0x0355FFFF  | Per-Core Redistributor Window           |
| MMC0 (MicroSD)        | 0x04020000 - 0x04020FFF  | SD/MMC 4-bit High-Speed Host Controller |
| MMC2 (SDIO Wi-Fi)     | 0x04022000 - 0x04022FFF  | SDIO 4-bit High-Speed Wireless Interface|
| USB 2.0 Host 0 (EHCI) | 0x04101000 - 0x04101FFF  | USB 2.0 High-Speed Host 0               |
| USB 2.0 Host 0 (OHCI) | 0x04101400 - 0x041017FF  | USB 1.1 Full/Low-Speed Host 0           |
| USB 2.0 Host 1 (EHCI) | 0x04200000 - 0x04200FFF  | USB 2.0 High-Speed Host 1               |
| USB 2.0 Host 1 (OHCI) | 0x04200400 - 0x042007FF  | USB 1.1 Full/Low-Speed Host 1           |
| GMAC0 (Ethernet 0)    | 0x04500000 - 0x04507FFF  | Synopsys DWMAC 5.20 Gigabit MAC 0       |
| GMAC0 Syscon / MDIO   | 0x04508000 - 0x04508FFF  | GMAC0 PHY Interface & MDIO Control      |
| GMAC1 (Ethernet 1)    | 0x04510000 - 0x04517FFF  | Synopsys DWMAC 5.20 Gigabit MAC 1       |
| GMAC1 Syscon / MDIO   | 0x04518000 - 0x04518FFF  | GMAC1 PHY Interface & MDIO Control      |
| USB 3.1 Gen2 (DWC3)   | 0x06A00000 - 0x06AFFFFF  | Synopsys DWC3 SuperSpeed Controller     |
| Cadence SerDes SerDes | 0x06C00000 - 0x06C05FFF  | SerDes Top Configuration                |
| Combo PHY 0 (USB3/DP) | 0x06C01000 - 0x06C019FF  | Combo PHY 0 (USB3 / DisplayPort Mux)    |
| Combo PHY 1 (PCIe/USB)| 0x06C02000 - 0x06C029FF  | Combo PHY 1 (PCIe 3.0 / USB3 Mux)       |
| PRCM R_CCU            | 0x07010000 - 0x070103FF  | Deep Sleep & PRCM Power Management CCU  |
| PRCM R_PIO            | 0x07025000 - 0x0702540F  | Port L (14 pins) and Port M (6 pins)     |
| PMIC I2C (R_I2C0)     | 0x07083000 - 0x070833FF  | AXP318 / AXP8191 PMIC Communication Bus |
| RISC-V ITCM Window    | 0x07110000 - 0x0711FFFF  | 64 KiB Instruction Tightly-Coupled Memory|
| RISC-V SRAM Window    | 0x07130000 - 0x0717FFFF  | 320 KiB Real-Time SRAM Execution Pool   |
+───────────────────────+──────────────────────────+─────────────────────────────────────────+
```

---

### B. GIC SPI Interrupt Lines Table

| Peripheral | GIC Interrupt Type | SPI Number | DTS Notation | Description |
| :--- | :--- | :--- | :--- | :--- |
| **UART0 Console** | `GIC_SPI` | **2** | `<GIC_SPI 2 IRQ_TYPE_LEVEL_HIGH>` | Serial console byte RX/TX interrupt |
| **Main PIO Bank 0–9** | `GIC_SPI` | **69 – 87** (odd) | `<GIC_SPI 69 ... 87>` | GPIO external interrupt lines (PB–PK) |
| **GMAC0 Ethernet** | `GIC_SPI` | **172** (`0xac`) | `<GIC_SPI 172 IRQ_TYPE_LEVEL_HIGH>` | MAC DMA transmit/receive interrupt (`macirq`) |
| **GMAC1 Ethernet** | `GIC_SPI` | **173** (`0xad`) | `<GIC_SPI 173 IRQ_TYPE_LEVEL_HIGH>` | MAC DMA transmit/receive interrupt (`macirq`) |
| **USB 2.0 Host 0 (EHCI)** | `GIC_SPI` | **157** (`0x9d`) | `<GIC_SPI 157 IRQ_TYPE_LEVEL_HIGH>` | USB2 EHCI 0 interrupt |
| **USB 2.0 Host 0 (OHCI)** | `GIC_SPI` | **158** (`0x9e`) | `<GIC_SPI 158 IRQ_TYPE_LEVEL_HIGH>` | USB1.1 OHCI 0 interrupt |
| **USB 2.0 Host 1 (EHCI)** | `GIC_SPI` | **159** (`0x9f`) | `<GIC_SPI 159 IRQ_TYPE_LEVEL_HIGH>` | USB2 EHCI 1 interrupt |
| **USB 2.0 Host 1 (OHCI)** | `GIC_SPI` | **160** (`0xa0`) | `<GIC_SPI 160 IRQ_TYPE_LEVEL_HIGH>` | USB1.1 OHCI 1 interrupt |
| **MMC0 (MicroSD)** | `GIC_SPI` | **161** (`0xa1`) | `<GIC_SPI 161 IRQ_TYPE_LEVEL_HIGH>` | SD card command/data transfer done |
| **MMC2 (Wi-Fi SDIO)** | `GIC_SPI` | **163** (`0xa3`) | `<GIC_SPI 163 IRQ_TYPE_LEVEL_HIGH>` | Wi-Fi SDIO bus transfer done |
| **USB 3.1 DWC3** | `GIC_SPI` | **155** (`0x9b`) | `<GIC_SPI 155 IRQ_TYPE_LEVEL_HIGH>` | DWC3 XHCI controller core interrupt |
| **PRCM R-PIO Bank 0 (PL)**| `GIC_SPI` | **198** (`0xc6`) | `<GIC_SPI 198 IRQ_TYPE_LEVEL_HIGH>` | Port L external GPIO interrupts |
| **PRCM R-PIO Bank 1 (PM)**| `GIC_SPI` | **200** (`0xc8`) | `<GIC_SPI 200 IRQ_TYPE_LEVEL_HIGH>` | Port M external GPIO interrupts |
| **PMIC I2C (R_I2C0)** | `GIC_SPI` | **203** (`0xcb`) | `<GIC_SPI 203 IRQ_TYPE_LEVEL_HIGH>` | I2C transfer completion & error |
| **Hardware Mailbox** | `GIC_SPI` | **211** (`0xd3`) | `<GIC_SPI 211 IRQ_TYPE_LEVEL_HIGH>` | RISC-V E907 IPC mailbox interrupt |

---

### C. Pinmux Functions (Chapter 4 Datasheet Reference)

| Port & Pins | Mux Value | Pinmux Function Name | Board Connection / Target Peripheral |
| :--- | :--- | :--- | :--- |
| **`PB9`, `PB10`** | `allwinner,pinmux = <2>` | `uart0` | UART0 Serial Debug Console |
| **`PF0` – `PF5`** | `allwinner,pinmux = <2>` | `mmc0` | MicroSD Slot (`SDC0`) |
| **`PC6`, `PC8`–`PC11`, `PC13`–`PC16`** | `allwinner,pinmux = <3>` | `sdc2` | AIC8800 Wi-Fi 6 SDIO Bus (`SDC2`) |
| **`PH0` – `PH15`** | `allwinner,pinmux = <5>` | `rgmii0` | Gigabit Ethernet 0 RGMII Bus (`PH10` = `RGMII0-RXD3`) |
| **`PH16`** | `GPIO` (Bank 7, Pin 16) | `GPIO_ACTIVE_LOW` | Motorcomm Gigabit Ethernet PHY Reset (`GMAC1_RSTn_L` via `R185` 0Ω) |
| **`PL0`, `PL1`** | `allwinner,pinmux = <2>` | `r_i2c0` (`s_twi0`) | AXP318 / AXP8191 PMIC Power Bus |
| **`PL2`** | `GPIO` (Bank 0, Pin 2) | `GPIO_ACTIVE_HIGH` | USB 2.0 VBUS 0 Enable (`usb0-vbus`) |
| **`PM0`** | `GPIO` (Bank 1, Pin 0) | `GPIO_ACTIVE_HIGH` | Wi-Fi Module 3.3V Power Enable (`wifi_power_en`) |
| **`PM1`** | `GPIO` (Bank 1, Pin 1) | `GPIO_ACTIVE_HIGH` | Wi-Fi Chip Reset / Enable (`wifi_chip_en`) |
| **`PM5`** | `GPIO` (Bank 1, Pin 5) | `GPIO_ACTIVE_HIGH` | USB 2.0 VBUS 1 Enable (`usb1-vbus`) |

---

## 3. Firmware Provenance & Repository Master Matrix

| Component | Build Source | Repo URL | Branch / Version | Config / Output Target | Critical Technical Detail |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Vendor DRAM `boot0`** | Binary Blob | [radxa/allwinner-device](https://github.com/radxa/allwinner-device) | `device-a733-v1.4.6` | `bin/boot0_sdcard_sun60iw2p1_lpddr5.bin` | Written directly to **Sector 256 (128 KB)**; trains LPDDR5 PHY across 4 clock tiers (400–2400 MHz) in SRAM. |
| **TF-A (BL31)** | Source Build | [dlan17/trusted-firmware-a](https://github.com/dlan17/trusted-firmware-a) | `A733` | `PLAT=sun60i_a733` (`bl31.bin`) | Secure EL3 handler; configures GICv3 system registers (`ICC_SRE_EL3=0x7`, `ICC_SRE_EL2=0x7`) and handles PSCI 1.1. |
| **U-Boot Mainline** | Source Build | [dlan17/u-boot](https://github.com/dlan17/u-boot) | `allwinner/A733/boot-2026.01` | `configs/radxa-cubie-a7a_defconfig` | Linked at `0x4a001000` (4KB page aligned) with `b +0x1000` (`0x14000400`) header branch. |
| **TOC1 Tool** | Host Binary | Local board tools | `v1.4.6` | `dragonsecboot` | Packages 4KB page-aligned U-Boot + BL31 + OP-TEE + ARISC + DTB into `boot_package.fex`. |
| **Linux Kernel** | Mainline + Patches | [git.kernel.org / sunxi](https://git.kernel.org/pub/scm/linux/kernel/git/sunxi/linux.git) | `sunxi-clk-for-7.3` / `7.1` | `sun60i-a733-cubie-a7a.dts` + CCU/RTC/PRCM | Pure `PREEMPT_RT` baseline with native GICv3 and `sunxi_rproc.c`. |
| **Wi-Fi Driver** | Out-of-tree Driver | Local `aic8800-upstream` | Unified v3.0 | `BR2_PACKAGE_AIC8800_DRIVER_USB=y` | High-throughput USB 2.0 (`0xA69C:0x8800`) Wi-Fi 6 interface. |
| **RISC-V Driver** | In-tree Driver | `patches/linux/0002` | Linux 7.1 | `CONFIG_SUNXI_REMOTEPROC=y` | RemoteProc driver for XuanTie E907 avionics co-processor. |

---

## 4. The 2-Stage Hybrid Boot Architecture

```
┌───────────────────────────────────────────────────────────┐
│ 1. BootROM (BROM in Silicon)                              │
│    • Reads boot0 from Sector 256 (128 KB offset)          │
└─────────────────────────────┬─────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────┐
│ 2. Vendor boot0 (executes in SRAM @ 0x47000)              │
│    • Calibrates dynamic LPDDR5 PHY (400 -> 1800 MHz)      │
│    • Sets PMIC voltage rails and stores 6 GiB DRAM geometry│
│    • Reads boot_package.fex from Sector 24576 (12.0 MB)   │
│    • Loads Mainline SPL to 0x48000000                     │
│    • Loads Mainline FIT container to 0x48200000           │
│    • Jumps to Mainline U-Boot SPL at 0x480006a0           │
└─────────────────────────────┬─────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────┐
│ 3. Mainline U-Boot SPL (executes in DDR @ 0x480006a0)     │
│    • Retrieves trained DDR size (6 GiB) from boot0 header │
│    • Unpacks TF-A BL31 (to 0x4D000000)                    │
│    • Unpacks Mainline U-Boot Proper (to 0x4A000000)       │
│    • Jumps to TF-A BL31 at Secure EL3                     │
└─────────────────────────────┬─────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────┐
│ 4. ARM Trusted Firmware BL31 (Secure EL3)                 │
│    • Configures GICv3 System Registers (ICC_SRE_EL3/EL2=7)│
│    • Initializes PSCI 1.1 CPU management                  │
│    • Drops to Non-Secure EL2                              │
└─────────────────────────────┬─────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────┐
│ 5. Mainline U-Boot 2026.01 Proper (Non-Secure EL2)        │
│    • Executes boot.scr -> booti 0x40200000 - 0x4FA00000   │
└─────────────────────────────┬─────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────┐
│ 6. Mainline Linux 7.1 PREEMPT_RT                          │
└───────────────────────────────────────────────────────────┘
```

---

## 5. Storage Layout Blueprint

| **Sector Range** | **Byte Offset** | **Size** | **Identifier / Content** | **Purpose** |
| :--- | :--- | :--- | :--- | :--- |
| **Sectors 0 – 255** | `0x00000000` (0 B) | 128 KB | `0xAA55` (MBR) | Master Boot Record & Partition Table |
| **Sector 256** | **`0x00020000` (128 KB)** | **296 KB** (303,104 B) | **`eGON.BT0`** | **`boot0_sdcard.bin`** (LPDDR5 dynamic PHY training; Checksum `0x316fb06b`) |
| **Sector 2048** | **`0x00100000` (1.0 MB)** | ~1.6 MB | U-Boot / ATF Stage | U-Boot proper execution binary and firmware stage |
| **Sector 32768** | **`0x01000000` (16.0 MB)** | 64 MB | FAT32 (`0x0C`) | **Partition 1: `boot.vfat`** (`Image`, `sun60i-a733-cubie-a7a.dtb`, `boot.scr`, `uboot.env`) |
| **Sector 163840** | **`0x05000000` (80.0 MB)** | 512 MB | Ext4 (`0x83`) | **Partition 2: `rootfs.ext4`** (Full Buildroot userspace rootfs) |

---

## 6. DRAM Memory & Address Map

```text
0x40000000 ┌────────────────────────────────────────────────────────┐ (DRAM Base - 6 GiB)
           │ Linux DRAM Base (mem 0x40000000 - 0x1C0000000)         │
0x40200000 ├────────────────────────────────────────────────────────┤
           │ Linux Kernel Image (kernel_addr_r)                     │
0x44000000 ├────────────────────────────────────────────────────────┤
           │ Kernel Decompression Scratch Space (kernel_comp_addr_r)│
0x48000000 ├────────────────────────────────────────────────────────┤
           │ Mainline U-Boot SPL Load Base                          │
0x480006A0 ├────────────────────────────────────────────────────────┤
           │ Mainline U-Boot SPL Execution Entry (SPL_TEXT_BASE)    │
0x4805A000 ├────────────────────────────────────────────────────────┤
           │ SPL System Stack Pointer (SPL_STACK)                   │
0x48200000 ├────────────────────────────────────────────────────────┤
           │ Mainline U-Boot FIT Container (SPL_LOAD_FIT_ADDRESS)   │
0x4A000000 ├────────────────────────────────────────────────────────┤
           │ Mainline U-Boot Proper Execution Address (AArch64 EL2) │
0x4D000000 ├────────────────────────────────────────────────────────┤
           │ ARM Trusted Firmware BL31 Runtime (Secure EL3)         │
0x4FA00000 ├────────────────────────────────────────────────────────┤
           │ Device Tree Blob Load Address (fdt_addr_r)             │
0x4FC00000 ├────────────────────────────────────────────────────────┤
           │ U-Boot Boot Script (scriptaddr)                        │
0x1C0000000└────────────────────────────────────────────────────────┘ (Top of 6 GiB RAM)
```

---

## 7. Packaging Pipeline & Build Commands

### Full Build & Flash Commands
```bash
# Build complete image in bld.a7a
make -C /home/tcmichals/projects/cubie/bld.a7a

# Automated image audit verification
python3 project-cubie-a5e/board/radxa/cubie_a7a/tools/verify_sdcard_image.py bld.a7a/images/sdcard.img

# Flash to MicroSD card
sudo dd if=/home/tcmichals/projects/cubie/bld.a7a/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```
