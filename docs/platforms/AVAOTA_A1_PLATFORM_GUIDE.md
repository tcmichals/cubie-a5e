# 🚀 Platform Guide: Yuzuki / Pine64 Avaota A1 (Allwinner T527 / `sun55i`)

This document details hardware specifications, bootloader architecture, and peripheral mappings for the **Yuzuki / Pine64 Avaota A1** single-board computer.

---

## 1. Hardware Architecture Specification

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner T527 / A527 (`sun55iw3`) | 8× ARM Cortex-A55 @ 1.80 GHz |
| **RAM** | 1 GiB / 2 GiB / 4 GiB LPDDR4 / LPDDR4X | Auto-probed via U-Boot `dram_init` |
| **Co-Processors** | **XuanTie E906/E907 RISC-V** (up to 200 MHz) + **HiFi4 Audio DSP** | Managed via `sunxi_rproc.c` and SRAM C (`0x00020000`) |
| **NPU** | **2.0 TOPS VIP9000** | Direct Etnaviv DRM / Teflon support (`/dev/dri/card0`) |
| **Ethernet** | Dual Gigabit Ethernet (DWMAC 5.20) | `sun55i-a523.dtsi` GMAC0 & GMAC1 |
| **Wi-Fi / BT** | Wi-Fi 6 + BT 5.4 | Onboard SDIO / UART transport |
| **Mainline Kernel** | **Linux 7.1 PREEMPT_RT** | In-tree `arch/arm64/boot/dts/allwinner/sun55i-t527-avaota-a1.dts` |
| **Mainline TF-A** | ARM Trusted Firmware | `PLAT=sun55i_a523` (`bl31.bin`) |
| **Mainline U-Boot** | U-Boot 2026.01 (`sun55i`) | Generates `u-boot-sunxi-with-spl.bin` |

---

## 2. Quick Start: Building for Avaota A1

```bash
# Configure for Avaota A1
make -C buildroot O=$PWD/bld.avaota BR2_EXTERNAL=$PWD/project-cubie-a5e avaota_a1_defconfig

# Compile complete OS and SD card image
make -C bld.avaota

# Flash to MicroSD card
sudo dd if=bld.avaota/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

---

## 3. XuanTie E907 Real-Time Coprocessor Support

The Avaota A1 shares the identical **XuanTie E907 RISC-V co-processor architecture** as the Radxa Cubie A5E:
* **Lifecycle Management**: Controlled by Linux via `/sys/class/remoteproc/remoteproc0/state`.
* **Execution Memory**: Runs bare-metal firmware in zero-wait-state MCU SRAM C (`0x00020000`) or ITCM (`0x07110000`).
* **IPC**: Lock-free SPSC queues and hardware mailbox doorbells (`0x03003000`).
