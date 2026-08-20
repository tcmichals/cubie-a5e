# 🚁 Platform Guide: Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)

This document is the dedicated hardware and bootloader specification for the **Radxa Cubie A5E** flight controller. It covers SoC architecture, mainline upstream status, boot sequence, memory configuration, and platform-specific bring-up details.

---

## 1. Hardware Architecture Specification

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner A527 / T527 (`sun55iw3`) | 8× ARM Cortex-A55 Cores (Octa-core) |
| **RAM** | 2 GiB / 4 GiB LPDDR4 / LPDDR4X | Dynamic probing via U-Boot `dram_init` |
| **Interrupt Controller** | ARM GIC-600 (GICv3) | Base MMIO at `0x03400000` / `0x03460000` |
| **Mainline Kernel Status** | **100% In Mainline Linux** | `sun55i-a523.dtsi` / `sun55i-a527-cubie-a5e.dts` in upstream Linux 7.1 |
| **Mainline TF-A Status** | **In Mainline TF-A** | `PLAT=sun55i_a523` |
| **Mainline U-Boot Status** | **In Mainline U-Boot** | U-Boot 2026.01 (`BR2_TARGET_UBOOT=y`) |
| **Wi-Fi / BT Transport** | **SDIO 3.0** (4-bit, 50 MHz) | Uses `aic8800_bsp.ko` + `aic8800_fdrv.ko` |
| **BROM Boot Geometry** | Sector 16 / Offset 8 KiB | Standard Allwinner eMMC/SD boot geometry |

---

## 2. Boot Flow Architecture

```
   ┌──────────────────────────────────────────────────────────┐
   │ 1. BootROM (BROM in silicon ROM)                         │
   │    • Reads from SD card offset 8 KiB (Sector 16)          │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 2. Mainline U-Boot SPL (Compiled from source)            │
   │    • Initializes clocks, PMIC, and LPDDR4 RAM controller │
   │    • Loads TF-A (BL31) and U-Boot proper (BL33)          │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 3. ARM Trusted Firmware BL31 (Mainline sun55i_a523)      │
   │    • Configures GICv3 system registers (ICC_SRE_EL1.SRE=1)│
   │    • Sets up secure monitor & PSCI v0.2 SMC services     │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 4. Mainline U-Boot 2026.01 Proper                        │
   │    • Injects dynamic /memory node via fdt_fixup_memory_banks │
   │    • Executes boot.scr -> Loads Linux 7.1 Image & DTB    │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 5. Mainline Linux 7.1 PREEMPT_RT Kernel                  │
   └──────────────────────────────────────────────────────────┘
```

---

## 3. Platform-Specific Bring-Up Quirks & Fixes

1. **Serial Console Disconnect (`ttyS0` vs `ttyS2`)**:
   - *Problem*: Adding `&uart2` in the flight stack overlay dynamically shifted `ttyS0` to UART2, breaking the debug console.
   - *Fix*: Enforce absolute aliases in the overlay:
     ```dts
     &{/aliases} {
         serial0 = "/soc/serial@2500000";
         serial2 = "/soc/serial@2500800";
     };
     ```

2. **SDIO Wakeup & OOB Register Configuration**:
   - The AIC8800 on A5E communicates over SDIO; the driver must read `wakeup_reg` (0x01) and avoid writing `0x40504084` to prevent hijacking SDIO DAT1 interrupts. (See [`docs/common/WIFI_AIC8800_GUIDE.md`](../common/WIFI_AIC8800_GUIDE.md)).

---

## 4. Build Commands for A5E

```bash
# In build directory (e.g. bld.a5e)
make cubie_a5e_defconfig
make
```
- Output Disk Image: `images/sdcard.img`
