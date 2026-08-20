# 🚀 Platform Guide: Radxa Cubie A7A (Allwinner A733 / `sun60iw2`)

This document is the dedicated hardware, bootloader, and bring-up specification for the **Radxa Cubie A7A** flight controller. It details SoC architecture, out-of-tree status, LPDDR5 DRAM training, GICv3 interrupt controller configuration, and all verified fixes.

---

## 1. Hardware Architecture Specification

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner A733 (`sun60iw2p1`) | 2× Cortex-A76 (Big) + 6× Cortex-A55 (LITTLE) DynamIQ Cluster |
| **RAM** | 6 GiB LPDDR5 (400 MHz to 2400 MHz) | Multi-PState dynamic training executed by `boot0` |
| **Interrupt Controller** | ARM GIC-600 (GICv3) | Distributor: `0x03400000`, Redistributors: `0x03460000` |
| **Mainline Kernel Status** | **Out-of-Tree** | Supported via patch `0001-arm64-dts-allwinner-add-sun60i-a733-cubie-a7a.patch` |
| **Bootloader Stack** | **Vendor-Extracted Blob** | 16 MB `radxa_a733_bootloader.bin` (contains `boot0` + BL31 + U-Boot 2018.07) |
| **Wi-Fi / BT Transport** | **USB 2.0 High-Speed** | `0xA69C:0x8800` (Uses standalone `aic8800_fdrv.ko`) |
| **BROM Boot Geometry** | Sector 256 / Offset 128 KiB | Skips sectors 1–33 to maintain full GPT partition table compatibility |

---

## 2. Boot Flow Architecture

```
   ┌──────────────────────────────────────────────────────────┐
   │ 1. BootROM (BROM in silicon ROM)                         │
   │    • Reads boot0 from SD card offset 128 KiB (Sector 256)│
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 2. boot0 (First-Stage Bootloader in internal SRAM)       │
   │    • Configures PMIC, sets DRAM_VCC = 560 mV             │
   │    • Dynamically trains LPDDR5 PStates (400 -> 2400 MHz) │
   │    • Validates 6144 MB RAM integrity (Result = 7)        │
   │    • Loads BL31 (0x48000000) & U-Boot (0x4A000000) in RAM│
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 3. ARM Trusted Firmware BL31 (Vendor sun60i)             │
   │    • Configures GICv3 system registers (ICC_SRE_EL1.SRE=1)│
   │    • Sets up secure monitor & PSCI v0.2 SMC services     │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 4. Vendor U-Boot 2018.07 (BL33 in LPDDR5)                │
   │    • Requires pre-existing /memory node in DTB           │
   │    • Executes boot.scr -> Loads Linux 7.1 Image & DTB    │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 5. Mainline Linux 7.1 PREEMPT_RT Kernel                  │
   │    • GICv3 root IRQ domain                               │
   │    • XuanTie E907 remoteproc co-processor                │
   │    • Standalone USB AIC8800 wireless driver              │
   └──────────────────────────────────────────────────────────┘
```

---

## 3. Platform-Specific Quirks, Pitfalls & Verified Fixes

### Quirk 1: GICv2 vs GICv3 Firmware Panic
- **Problem**: The original vendor DTS declared legacy GICv2 (`compatible = "arm,cortex-a15-gic"` at `0x03021000`), but ATF BL31 enabled GICv3 system registers (`ICC_SRE_EL1.SRE = 1`). Linux 7.1 panicked at `irq-gic.c:57`.
- **Fix**: Replaced with native GICv3 distributor (`0x03400000`) and redistributor (`0x03460000`) mapping in `sun60i-a733-cubie-a7a.dts`, preserving `phandle = <0x9b>`.

### Quirk 2: Do NOT Include `sun55i-a523.dtsi` on A733
- **Problem**: Attempting to clean up the A733 DTS by including `sun55i-a523.dtsi` caused a silent hang at BL3-1 because the A523 CCU clock registers and CPU topology starved the A733 hardware of clocks.
- **Fix**: Maintain the complete 2,564-line native `sun60i` hardware device tree directly.

### Quirk 3: Vendor U-Boot 2018.07 `/memory` Query
- **Problem**: Vendor U-Boot queries for an *existing* `/memory` node with libfdt; if absent, it throws `FDT_ERR_NOTFOUND` and does not insert memory banks, causing Linux to see 0 MB RAM.
- **Fix**: Hardcode base 6 GiB memory definition in the DTS:
  ```dts
  memory@40000000 {
      device_type = "memory";
      reg = <0x00 0x40000000 0x01 0x80000000>;
  };
  ```

### Quirk 4: `post-image.sh` Stale DTB Overwrite
- **Problem**: `board/radxa/cubie_a7a/post-image.sh` was copying an outdated binary over the freshly compiled DTB.
- **Fix**: Removed the stale `cp -f` line in `post-image.sh`.

---

## 4. Build Commands for A7A

```bash
# In build directory (e.g. bld.a7a)
make cubie_a7a_defconfig
make
```
- Output DTB: `images/sun60i-a733-cubie-a7a.dtb` (`42,605 bytes`)
- Output Disk Image: `images/sdcard.img` (`620,756,992 bytes`)

### Flashing SD Card
```bash
sudo dd if=images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```
