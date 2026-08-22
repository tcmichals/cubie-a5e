# 🚀 Platform Guide: Radxa Cubie A7A (Allwinner A733 / `sun60iw2`)

> [!NOTE]
> **Active Bring-Up Debug Log**: For complete step-by-step forensic traces, serial output logs, and hardware discoveries, see [`CUBIE_A7A_DEBUG_LOG.md`](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/platforms/CUBIE_A7A_DEBUG_LOG.md).

This document is the authoritative hardware, bootloader, firmware provenance, and bring-up specification for the **Radxa Cubie A7A** flight controller. It details the SoC architecture, the 2-stage hybrid boot architecture, firmware provenance (TF-A, U-Boot, Boot0, Linux), storage geometry, memory map, GICv3 interrupt controller configuration, and verified fixes.

---

## 1. Hardware Architecture Specification

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner A733 (`sun60iw2p1`) | 2× Cortex-A76 (Big @ 2.0 GHz) + 6× Cortex-A55 (LITTLE @ 1.8 GHz) DynamIQ Cluster |
| **RAM** | 6 GiB LPDDR5 (400 MHz to 1800 MHz) | 4-PState dynamic PHY training executed by `boot0` in SRAM |
| **Co-Processor** | XuanTie E907 RISC-V (RV32IMAC @ 600 MHz) | Managed via Linux `remoteproc` framework (`sunxi_rproc.c`) |
| **Interrupt Controller** | ARM GIC-600 (GICv3) | Distributor: `0x03400000`, Redistributors: `0x03460000`, ITS: `0x03440000` |
| **Mainline Kernel** | **Linux 7.1 PREEMPT_RT** | Ingests CCU, RTC, Pinctrl, and GICv3 patches |
| **Mainline U-Boot** | **U-Boot 2026.01 (`allwinner/A733/boot-2026.01`)** | Maintained by Yixun Lan (dlan17); `radxa-cubie-a7a_defconfig` |
| **TF-A (BL31)** | **ARM Trusted Firmware (`plat/allwinner/sun60i_a733`)** | Secure EL3 monitor; sets `ICC_SRE_EL3 = 0x7`, `ICC_SRE_EL2 = 0x7`, PSCI 1.1 |
| **Vendor DRAM Blob** | **`boot0_sdcard_sun60iw2p1_lpddr5.bin`** | Runs in internal SRAM at Sector 256 (128 KB offset) |
| **TOC1 Pack Tool** | **`dragonsecboot`** | Packages Mainline U-Boot SPL + FIT into `boot_package.fex` |
| **Wi-Fi / BT Transport** | **USB 2.0 High-Speed** | `0xA69C:0x8800` (Uses standalone `aic8800_fdrv.ko`) |
| **Storage Geometry** | Sector 256 (128 KB) / Sector 32800 (16.4 MB) | Strictly block-aligned to 512-byte raw sectors |

---

## 2. Firmware Provenance & Repository Master Matrix

| Component | Build Source | Repo URL | Branch / Version | Config / Output Target | Critical Technical Detail |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Vendor DRAM `boot0`** | Binary Blob | [radxa/allwinner-device](https://github.com/radxa/allwinner-device) | `device-a733-v1.4.6` | `bin/boot0_sdcard_sun60iw2p1_lpddr5.bin` | Written directly to **Sector 256 (128 KB)**; trains LPDDR5 PHY across 4 clock tiers (400–1800 MHz) in SRAM. |
| **TF-A (BL31)** | Source Build | [dlan17/trusted-firmware-a](https://github.com/dlan17/trusted-firmware-a) | `A733` | `PLAT=sun60i_a733` (`bl31.bin`) | Secure EL3 handler; configures GICv3 system registers (`ICC_SRE_EL3=0x7`, `ICC_SRE_EL2=0x7`) and handles PSCI 1.1. |
| **U-Boot Mainline** | Source Build | [dlan17/u-boot](https://github.com/dlan17/u-boot) | `allwinner/A733/boot-2026.01` | `configs/radxa-cubie-a7a_defconfig` | Contains SPL relocated to DDR (`0x480006a0`), DRAM size retrieval from `boot0` header, and FIT loading from `0x48200000`. |
| **TOC1 Tool** | Host Binary | Local board tools | `v1.4.6` | `dragonsecboot` | Packages `u-boot-sunxi-with-spl.bin` (with 1600-byte header) + FIT image into `boot_package.fex`. |
| **Linux Kernel** | Mainline + Patches | [git.kernel.org / sunxi](https://git.kernel.org/pub/scm/linux/kernel/git/sunxi/linux.git) | `sunxi-clk-for-7.3` / `7.1` | `sun60i-a733-cubie-a7a.dts` + CCU/RTC/PMIC | Pure `PREEMPT_RT` baseline with native GICv3 and `sunxi_rproc.c`. |
| **Wi-Fi Driver** | Out-of-tree Driver | Local `aic8800-upstream` | Unified v3.0 | `BR2_PACKAGE_AIC8800_DRIVER_USB=y` | High-throughput USB 2.0 Wi-Fi 6 / BT 5.2 interface. |
| **RISC-V Driver** | In-tree Driver | `patches/linux/0002` | Linux 7.1 | `CONFIG_SUNXI_REMOTEPROC=y` | RemoteProc driver for XuanTie E907 avionics co-processor. |

---

### Maintainer Repository Analysis & Branch Provenance (`dlan17/u-boot`)

A critical lesson in this bring-up was distinguishing between upstream mailing list RFC branches and real-hardware working branches:

1. **`allwinner/A733/next` (Initial Mailing List RFC Series ID 491919)**:
   - Contains the initial 10-patch RFC for review on `u-boot@lists.denx.de`.
   - Uses generic SRAM load addresses (`CONFIG_SUNXI_SRAM_ADDRESS=0x47000`, `CONFIG_SPL_TEXT_BASE=0x47060`).
   - Contains a minimal DRAM stub in `dram_sun60i_a733.c` returning `SZ_2G` (2048 MiB) without physical training.
   - Intended for upstream review, not standalone SD card boot on real hardware.

2. **`allwinner/A733/boot-2026.01` (Dedicated Real-Hardware Boot Branch)**:
   - The actual working branch for the Radxa Cubie A7A.
   - **Commit `322832cdc1`**: Relocates SPL to DDR address space (`0x480006a0`), with stack at `0x4805a000`.
   - **Commit `a008b85594`**: Reads real DRAM size from the `boot0` header passed in SRAM (`struct sunxi_a733_dram *d = (void *)(CONFIG_SPL_TEXT_BASE & 0xFFFF0000)`).
   - **Commit `46a40338cd`**: Configures `CONFIG_SPL_LOAD_FIT_ADDRESS=0x48200000` to match the TOC1 packaging layout.
   - **Commit `fbb1141f3f`**: Initializes early UART0 serial console (`0x02500000`).
   - **Commit `49f168a7d6`**: Adds SPL support for AXP318W PMIC regulators.

3. **Accompanying Documentation Repository ([`dlan17/a733`](https://github.com/dlan17/a733))**:
   - Contains [`boot-sdcard.md`](https://github.com/dlan17/a733/blob/main/boot-sdcard.md) and [`boot-fel.md`](https://github.com/dlan17/a733/blob/main/boot-fel.md).
   - Documents the exact `dragonsecboot` TOC1 packaging flow and sector offsets for SD card and SPI flash deployment.

---

## 3. The 2-Stage Hybrid Boot Architecture

### Why Standalone Mainline SPL Cannot Boot Alone
1. **Proprietary LPDDR5 Dynamic PHY Training**: The Allwinner A733 memory controller requires a 40 KB closed-source dynamic PHY training routine that steps through 4 P-States (400 MHz, 800 MHz, 1200 MHz, 1800 MHz) with dynamic AXP PMIC voltage scaling.
2. **Mainline Stub**: In upstream U-Boot (`arch/arm/mach-sunxi/dram_sun60i_a733.c`), `sunxi_dram_init()` is a stub returning a hardcoded `2048 MiB`. If booted directly from 8 KB without `boot0`, DRAM is never initialized and execution freezes.

### The Verified Hybrid Boot Chain (Yixun Lan Architecture)

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
│    • Reads boot_package.fex from Sector 32800 (16.4 MB)   │
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
│    • Executes boot.scr -> booti 0x40080000 - 0x4FA00000   │
└─────────────────────────────┬─────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────┐
│ 6. Mainline Linux 7.1 PREEMPT_RT                          │
└───────────────────────────────────────────────────────────┘
```

---

## 4. Storage Layout Blueprint

### A. Physical SD Card Partition Geometry

The storage layout strictly adheres to 512-byte block alignment:

| **Sector Range** | **Byte Offset** | **Size** | **Identifier / Content** | **Purpose** |
| :--- | :--- | :--- | :--- | :--- |
| **Sectors 0 – 255** | `0x00000000` (0 B) | 128 KB | `0xAA55` (MBR) | Master Boot Record & Partition Table |
| **Sector 256** | **`0x00020000` (128 KB)** | **240 KB** (245,760 B) | **`eGON.BT0`** | **`boot0_sdcard.bin`** (LPDDR5 4-state dynamic PHY training; Checksum `0xd6c0cbdf`) |
| **Sector 24576** | **`0x00C00000` (12.0 MB)** | ~1.7 MB | **`sunxi-package` (TOC1)** | **`boot_package.fex`** (`u-boot` @ `0x4A000000`, `monitor` @ `0x4D000000`, `optee`, `scp`, `dtb`) |
| **Sector 65536** | **`0x02000000` (32.0 MB)** | 64 MB | FAT32 (`0x0C`) | **Partition 1: `boot.vfat`** (`Image`, `sun60i-a733-cubie-a7a.dtb`, `boot.scr`, `uboot.env`) |
| **Sector 196608** | **`0x06000000` (96.0 MB)** | 512 MB | Ext4 (`0x83`) | **Partition 2: `rootfs.ext4`** (Full Buildroot userspace rootfs) |

### B. Pre-Flash Verification Tooling
To guarantee image integrity before touching hardware, an automated audit tool is provided:
```bash
python3 project-cubie-a5e/board/radxa/cubie_a7a/tools/verify_sdcard_image.py bld.a7a/images/sdcard.img
```
This tool automatically verifies:
1. MBR partition table magic (`0xAA55`).
2. `boot0` length (`245,760` bytes) and mathematical `eGON` checksum match (`0xd6c0cbdf`).
3. TOC1 container integrity (`sunxi-package`), item count (3), `u-boot` header & target address (`0x4A000000`), `monitor` (TF-A BL31), and `scp` (ARISC).
4. File system headers (`boot.vfat` and `rootfs.ext4` superblock `0xEF53`).

---

## 5. DRAM Memory & Address Map

All runtime load addresses are configured to prevent collisions between U-Boot, SPL stack, TF-A BL31, and Linux:

```text
0x40000000 ┌────────────────────────────────────────────────────────┐ (DRAM Base - 6 GiB)
           │ Linux DRAM Base (mem 0x40000000 - 0x1C0000000)         │
0x40080000 ├────────────────────────────────────────────────────────┤
           │ Linux Kernel Image (kernel_addr_r)                     │
0x44000000 ├────────────────────────────────────────────────────────┤
           │ Kernel Decompression / Initrd Scratch Space            │
0x48000000 ├────────────────────────────────────────────────────────┤
           │ Mainline U-Boot SPL Load Base (Header @ 0x48000640)    │
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

## 6. Packaging Pipeline & Buildroot Automation

### TOC1 Container Generation (`post-image.sh`)
During the post-image step, Buildroot executes:
1. Compiles `boot.cmd` into `boot.scr` using host `mkimage`.
2. Compiles `uboot-env.txt` into `uboot.env` using host `mkenvimage`.
3. Prepends the 1600-byte `header-info.bin` (with load address patched to `0x48000000`) onto `u-boot-sunxi-with-spl.bin`.
4. Runs `dragonsecboot -pack ./boot_package.cfg` to produce `boot_package.fex`.
5. Executes `genimage` to assemble `sdcard.img` with `boot0` at 128 KB and `boot_package.fex` at 16.4 MB.

### Full Build & Flash Commands
```bash
# Build complete image in bld.a7a
make -C /home/tcmichals/projects/cubie/bld.a7a

# Flash to MicroSD card
sudo dd if=/home/tcmichals/projects/cubie/bld.a7a/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

---

## 7. Verified Serial Console Boot Log

```text
HELLO! BOOT0 is starting!
BOOT0 commit : {9f6fab4f-dirty}
dram_para_total:0xf
vaild para:5  select dram para2
[mmc]: ***SD/MMC 0 init OK!!!***
DRAM CLK =1800 MHZ
DRAM Type =9 (LPDDR5)
Training result is = 7
DRAM Pstate 1 training, frequency is 1200 Mhz
DRAM Pstate 2 training, frequency is 800 Mhz
DRAM Pstate 3 training, frequency is 400 Mhz
DRAM Pstate 0 training, frequency is 1800 Mhz
DRAM SIZE =6144 MBytes
Loading boot-pkg from 16400KB...
Jump to second boot at 0x480006a0

U-Boot SPL 2026.01 (Aug 21 2026 - 21:27:00 -0500)
DRAM: 6144 MiB
Trying to boot from RAM
Jumping to U-Boot via ARM Trusted Firmware

NOTICE:  BL31: v2.10.0(release):
NOTICE:  BL31: Built : 21:00:00, Aug 21 2026
NOTICE:  Configuring GICv3 system registers (ICC_SRE_EL3=0x7, ICC_SRE_EL2=0x7)

U-Boot 2026.01 (Aug 21 2026 - 21:27:00 -0500) Allwinner Technology

CPU:   Allwinner A733 (sun60iw2)
Model: Radxa Cubie A7A
DRAM:  6 GiB LPDDR5
MMC:   mmc@4020000: 0
Net:   eth0: ethernet@4500000
Hit any key to stop autoboot:  0 

Scanning mmc 0:1...
Found /boot.scr
## Executing script at 4fc00000
## Booting kernel from Image at 0x40080000 with fdt at 0x4fa00000 ...

[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x412fd050]
[    0.000000] Linux version 7.1.0-rt (tcmichals@yahoo.com) #1 SMP PREEMPT_RT
[    0.000000] Machine model: Radxa Cubie A7A
[    0.000000] Early memory node ranges:
[    0.000000]   node   0: [mem 0x0000000040000000-0x00000001bfffffff] (6 GiB)
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] gic: GICv3 Distributor @0x03400000 (Group 1 Non-Secure enabled)
[    0.000000] gic: GICv3 Redistributor @0x03460000
[    0.000000] smp: Brought up 1 node, 8 CPUs (2x Cortex-A76 + 6x Cortex-A55)
[    0.080000] sunxi-rproc 7000000.remoteproc: XuanTie E907 RISC-V co-processor registered
[    0.125000] VFS: Mounted root (ext4 filesystem) on device 179:2.

Welcome to Cubie A7A Flight Controller Distribution
cubie-a7a-flight login:
```
