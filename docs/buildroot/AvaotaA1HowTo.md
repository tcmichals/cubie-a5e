# 🚀 Yuzuki / Pine64 Avaota A1 (Allwinner T527) Build & Bring-Up Guide

This guide details how to configure, compile, flash, and operate the mainline Linux 7.1 `PREEMPT_RT` flight distribution for the **Yuzuki / Pine64 Avaota A1** single-board computer.

---

## 1. Board Specifications Overview

The **Avaota A1** is an open-hardware single-board computer powered by the **Allwinner T527 (`sun55i`)** SoC:

* **CPU**: 8× ARM Cortex-A55 @ 1.80 GHz (DynamIQ Cluster)
* **Co-Processor**: XuanTie E906/E907 32-bit RISC-V (up to 200 MHz) with hardware FPU & DSP
* **RAM**: 1 GiB / 2 GiB / 4 GiB LPDDR4 / LPDDR4X
* **Storage**: MicroSD Slot + eMMC 5.1 + SPI NOR Flash
* **Networking**: Dual Gigabit Ethernet (DWMAC 5.20) + On-board Wi-Fi 6 / BT 5.4
* **Expansion**: Standard 40-Pin GPIO Header (UART, SPI, I2C, PWM, EINT)
* **Linux Baseline**: Pure Mainline Linux 7.1 with `PREEMPT_RT` hard real-time scheduling

---

## 2. Quick Start Build Instructions

### Step 1: Clone Buildroot (if not already cloned)
```bash
git clone https://github.com/buildroot/buildroot.git
```

### Step 2: Configure for Avaota A1
```bash
mkdir -p bld.avaota
PATH=$PWD/bld.avaota/bin:$PATH make -C buildroot O=$PWD/bld.avaota BR2_EXTERNAL=$PWD/project-cubie-a5e avaota_a1_defconfig
```

### Step 3: Compile Complete OS & SD Card Image
```bash
PATH=$PWD/bld.avaota/bin:$PATH make -C bld.avaota
```

The resulting bootable disk image is generated at:
`bld.avaota/images/sdcard.img`

---

## 3. Flashing to MicroSD Card

Identify your SD card device node (`lsblk` or `dmesg`), then flash the image:

```bash
sudo dd if=$PWD/bld.avaota/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

> [!WARNING]
> Ensure `/dev/sdX` corresponds to your SD card to avoid overwriting host system drives.

---

## 4. Serial Debug Console & Boot Verification

Connect a 3.3V USB-to-UART adapter to the Avaota A1 debug pins:
* **Baud Rate**: `115200 8N1`
* **Signals**: `TX` -> `RXD`, `RX` -> `TXD`, `GND` -> `GND`

### Expected Early Boot Output:
```text
=== Initializing Avaota A1 Dynamic Boot Sequence ===
>>> Loading Base Device Tree: sun55i-t527-avaota-a1.dtb...
>>> Loading Linux Kernel Image...
>>> Booting Linux Kernel on Avaota A1...
[    0.000000] Linux version 7.1.0-rt (gcc version 14.2.0)
[    0.000000] Machine model: Yuzuki Avaota A1
[    0.000000] SMP: Total of 8 processors activated
[    0.830701] sunxi-rproc 7102000.remoteproc: Allwinner XuanTie RISC-V remoteproc registered
```

---

## 5. XuanTie E907 Real-Time Coprocessor Control

The on-chip XuanTie E907 core is managed dynamically by the `sunxi_rproc.c` driver:

### Start Coprocessor:
```bash
# Place your bare-metal ELF binary in /lib/firmware/
cp my_firmware.elf /lib/firmware/riscv-firmware.elf

# Start execution in SRAM C (0x00020000)
echo start > /sys/class/remoteproc/remoteproc0/state
```

### Stop Coprocessor:
```bash
echo stop > /sys/class/remoteproc/remoteproc0/state
```

---

## 6. Real-Time Latency Verification (`PREEMPT_RT`)

To verify sub-20 microsecond real-time scheduling determinism on the 8-core CPU:

```bash
# Run cyclictest across all 8 cores with priority 98
cyclictest -p 98 -m -t 8 -n -l 100000
```
