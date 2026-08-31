# 🚁 Platform Guide: Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)

This document is the dedicated hardware, bootloader, and peripheral specification for the **Radxa Cubie A5E** flight controller. It covers SoC architecture, mainline upstream status, boot sequence, memory configuration, and hardware enablement for the **Camera (CSI/ISP)** and **NPU (VIP9000)** subsystems.

---

## 1. Hardware Architecture Specification

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner A527 / T527 (`sun55iw3`) | 8× ARM Cortex-A55 Cores (Octa-core) |
| **RAM** | 2 GiB / 4 GiB LPDDR4 / LPDDR4X | Dynamic probing via U-Boot `dram_init` |
| **NPU** | **2.0 TOPS VeriSilicon VIP9000** | VIPLite / Galcore kernel driver (`0x07122000`) |
| **Camera Subsystem** | **Allwinner Gen-4 Video In (VIN)** | 4× MIPI CSI-2 receivers + ISP + Multi-scalers |
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

## 3. NPU Subsystem Architecture (VeriSilicon VIP9000)

### 3.1 Hardware Register Map & Resources
* **Base Register**: `0x07122000` (4 KB)
* **Interrupt**: `GIC_SPI 199` (`IRQ_TYPE_LEVEL_HIGH`)
* **Clocks**:
  * `CLK_NPU` (Main CCU @ `0x02001000`)
  * `CLK_PLL_NPU_2X` (PLL Source)
  * `CLK_BUS_MCU_NPU_ACLK` / `CLK_BUS_MCU_NPU_HCLK` (MCU CCU @ `0x07100000`)
* **Resets**: `RST_BUS_MCU_NPU` (MCU CCU)
* **Power Domain**: `A523_PD_NPU` (`ppu PD_NPU` / PPU Domain 1)
* **DVFS / Operating Points**:
  * 546 MHz @ 0.92V
  * 696 MHz @ 1.05V

### 3.2 Driver Implementation Pathways
1. **VIPLite Driver (`aw_nna_vip` / `/dev/vip_drv`)**:
   - Direct kernel interface for VeriSilicon VIPLite runtime (`libVIPCore.so`, `libVIPLite.so`).
   - Ideal for deploying compiled Network Binary Graph (`.nbg`) neural networks (YOLO, MobileNet, custom CNNs).
2. **Galcore Driver (`aw_nna_galcore` / `/dev/galcore`)**:
   - Kernel interface for Vivante OpenVX and TIM-VX / TensorFlow Lite delegate execution.

---

## 4. Camera & Video Input (VIN) Architecture

### 4.1 Topology Overview
Allwinner Gen-4 Video In pipeline (`vind0` @ `0x05800800`):
* **MIPI CSI-2 Receivers (4 Units)**:
  * `mipi0` @ `0x05810100` (GIC SPI 137, 2/4-lane)
  * `mipi1` @ `0x05810200` (2-lane)
  * `mipi2` @ `0x05810300` (2/4-lane)
  * `mipi3` @ `0x05810400` (2-lane)
* **CSI Parser Channels (4 Units)**:
  * `csi0` @ `0x05820000` (SPI 130), `csi1` @ `0x05821000` (SPI 131), `csi2` @ `0x05822000` (SPI 132), `csi3` @ `0x05823000` (SPI 147)
* **Hardware ISP & Scalers**:
  * `isp00` @ `0x05900000` (SPI 133)
  * Multi-channel scalers `scaler00`..`scaler30` (`0x05910000`–`0x05910c00`, SPI 126–129)
* **Power Domain**: `A523_PCK_VI` (PCK-600 Domain 2)

### 4.2 Camera Pinout & Power Wiring on Cubie A5E
* **Sensor MCLKs**: `PE0` (MCLK0), `PE5` (MCLK1), `PE9` (MCLK2)
* **Camera Control I2C (CCI)**: `twi3` / `twi4`
* **Sensor Power Rails**:
  * `reg_bldo1`: 1.8V (IOVDD)
  * `reg_aldo2` / `reg_aldo3`: 2.8V (AVDD)
  * `reg_eldo4`: 1.2V (DVDD)

---

## 5. Platform-Specific Bring-Up Quirks & Fixes

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

## 6. Build Commands for A5E

```bash
# In build directory (e.g. bld.a5e)
make cubie_a5e_defconfig
make
```
- Output Disk Image: `images/sdcard.img`

