# Allwinner A733 (sun60iw2) XuanTie E902 Boot & Coprocessor Architecture

**Document Version:** 2.0  
**Date:** September 6, 2026  
**Target SoCs / Boards:** Allwinner A733 / sun60iw2 (Radxa Cubie A7A, Banana Pi A733, Radxa Cubie A7Z)  
**Author / Integration:** Embedded Real-Time & Heterogeneous Compute Team  

---

## 1. Executive Summary

This document provides the complete hardware, security, and boot architecture for the embedded **T-Head XuanTie E902 32-bit RISC-V core** on the Allwinner A733 (sun60iw2) SoC. It specifies how to target the platform for two distinct operational models:

1. **Mode 1: Standard Power Management (Suspend/Resume / `scp.fex`)**
   - Traditional consumer battery/tablet profile requiring S3 deep sleep (Suspend-to-RAM).
   - E902 runs Allwinner's `scp.fex` firmware loaded at boot time by `boot0` into DRAM at `0x40014000`.
2. **Mode 2: Real-Time Embedded Control (Linux `remoteproc`)**
   - Industrial automation, robotics, deterministic I/O, and real-time control where suspend/resume is disabled.
   - E902 is repurposed as an on-chip real-time coprocessor managed dynamically by the Linux `remoteproc` framework (`sunxi_rproc.c`) or loaded via custom boot firmware.

---

## 2. Silicon Topology & Domain Architecture

Unlike the Allwinner T527 / A523 which features a high-performance XuanTie E906/E907 core with dedicated hardware FPU and ITCM/DTCM inside an open MCU domain (`0x07100000+`), the **A733 integrates a XuanTie E902 core inside the CPUS / Always-On (`R_`) power management subsystem**:

| Silicon Feature | Allwinner T527 / A523 (Cubie A5E) | Allwinner A733 (Cubie A7A & Banana Pi A733) |
| :--- | :--- | :--- |
| **RISC-V Core IP** | XuanTie E906 / E907 (RV32IMAFDC + FPU) | **XuanTie E902 (RV32EMC, 16 GPRs, Integer only)** |
| **Subsystem Domain** | MCU / DSP Domain (`0x07100000+`) | **CPUS / Always-On (`R_`) Domain (`0x07000000+`)** |
| **Max Clock Rate** | 200 MHz | 200 MHz |
| **Registers** | 32 General Purpose Registers (`x0`–`x31`) | **16 General Purpose Registers (`x0`–`x15`)** |
| **Tightly Coupled Memory** | 64 KB ITCM (`0x00000000`), 64 KB DTCM (`0x00080000`)| **NO TCMs** (Executes out of SRAM A2 or DRAM) |
| **On-Chip SRAM** | 256 KB Dedicated MCU SRAM (`0x07280000`) | **208 KB System SRAM A2 (`0x00040000`–`0x00073FFF`)** |
| **CFG Register Base** | `0x07130000` (`MCU_CFG`) | **`0x07032000` (`E902_CFG`)** |
| **Start Vector Register**| `0x07130204` (`STA_ADD`) | **`0x07032204` (`STA_ADD`)** |
| **TrustZone Firewall** | **Open Non-Secure** (Directly writable in EL1) | **Secure-Gated by default** in factory BL31 |

---

## 3. TrustZone Security Boundary & Hardware Readback

### A. The Start Address Register (`0x07032204`)
The start address register `0x07032204` defines the instruction fetch entry point of the E902 upon reset:
* **Hardware Readback**: In stock bootloader configurations, reading `0x07032204` returns **`0x40014000`**.
* **DRAM Offset**: In Allwinner SPL architecture (`sun60iw2p1.h`), `SCP_CODE_DRAM_OFFSET` is `0x14000`. With DRAM mapped at `0x40000000`, this equals `0x40014000`.
* **Default BL31 Lockout**: In factory ARM Trusted Firmware (BL31), `0x07032000` is mapped in the Secure World. Writes from Non-Secure Linux EL1 are filtered by the interconnect firewall unless BL31 is unlocked (see §5).

---

## 4. Dual-Mode Architecture: Suspend/Resume vs. Real-Time Control

```
+───────────────────────────────────────────────────────────────────────────────────────────────────+
| ALLWINNER A733 DUAL-MODE SELECTION BLUEPRINT                                                      |
+───────────────────────────────────────────────────────────────────────────────────────────────────+

                                     [ Power-On / boot0 (SPL) ]
                                                 │
                                                 ▼
                                     [ U-Boot / TOC1 Package ]
                                                 │
                   ┌─────────────────────────────┴─────────────────────────────┐
                   ▼                                                           ▼
       [ MODE 1: Suspend / Resume ]                                [ MODE 2: Real-Time RemoteProc ]
  ┌─────────────────────────────────────┐                     ┌─────────────────────────────────────┐
  │ • TOC1 contains scp.fex             │                     │ • TOC1 omits scp.fex                │
  │ • Loads to 0x40014000 at power-on   │                     │ • E902 held in reset at boot        │
  │ • E902 manages AXP8191 PMIC over RSB│                     │ • U-Boot initializes AXP8191 PMIC   │
  │ • BL31 installs SCPI PSCI handlers  │                     │ • BL31 unlocks R_SPC & TZMA to NS   │
  │ • Linux reserves 0x40014000 (no-map)│                     │ • Linux sunxi_rproc loads firmware  │
  │ • remoteproc node disabled in DTS   │                     │ • remoteproc node enabled in DTS    │
  └─────────────────────────────────────┘                     └─────────────────────────────────────┘
```

---

## 4.1 A733 U-Boot Power FEX (`sys_config.fex` / `power.fex`) Architecture & Compilation

The Allwinner A733 hardware power tree and regulator voltage tables are defined using Allwinner's **FEX configuration format** and compiled directly into the binary boot structures during the U-Boot packaging phase.

### A. Power FEX Configuration Schema (`sys_config.fex`)

The power configuration defines the initial boot voltages, PMIC bus bindings, and power domain assignments:

```ini
[power_sply]
dcdc1_vol       = 3300000   ; 3.3V - USB 2.0 Hub (FE1.1S), AIC8800 Wi-Fi 6, Motorcomm GbE PHY
dcdc2_vol       = 900000    ; 0.9V - ARM Cortex-A76 Big Cores VDD-CPU
dcdc3_vol       = 900000    ; 0.9V - ARM Cortex-A55 Little Cores VDD-CPU
dcdc4_vol       = 1100000   ; 1.1V - LPDDR5 VDD2 DRAM Core Power
aldo1_vol       = 1800000   ; 1.8V - VCC-PL / VCC-PM PRCM IO Banks & Analog PLL
aldo2_vol       = 1800000   ; 1.8V - MIPI-CSI / MIPI-DSI Analog Power
aldo3_vol       = 3300000   ; 3.3V - GPIO Port H / Port B IO Voltage (VCC-IO)
bldo1_vol       = 1800000   ; 1.8V - LPDDR5 VDDQ / PLL Reference
cldo1_vol       = 3300000   ; 3.3V - MicroSD Card VCC-SD (`PF0`-`PF5`)

[pmu1_para]
pmu_used        = 1
pmu_twi_addr    = 0x34      ; PMIC Device Address on RSB / TWI
pmu_twi_id      = 0         ; Bound to r_rsb / r_i2c0 (0x07083000)
pmu_irq_id      = 203       ; GIC SPI 203 / R_PIO Interrupt
pmu_battery_rdc = 100
pmu_battery_cap = 0
pmu_bat_unused  = 1
pmu_power_key   = 1
pmu_reset_key   = 1
```

### B. Compilation & Packaging Pipeline (`dragonsecboot`)

During the U-Boot build, the text FEX files are compiled into binary parameter blocks and packaged into the TOC1 container:

1. **FEX Compiler (`fexc` / `script.bin` generator)**:
   - Converts `sys_config.fex` into binary struct format with 32-bit little-endian fields and CRC checks.
2. **TOC1 Manifest Integration (`boot_package.cfg`)**:
   ```ini
   [package]
   item=u-boot,          u-boot.bin,          0x4a000000
   item=monitor,         bl31.bin,            0x48000000
   item=scp,             scp.fex,             0x40014000
   item=dtb,             sun60i-a733-a7a.dtb, 0x4fa00000
   item=power_cfg,       power.fex,           0x40020000
   ```
3. **Packaging (`dragonsecboot`)**:
   - Compiles the components into `boot_package.fex` and writes it to **Sector 24576 (12.0 MB offset)** on the boot media.

### C. Boot-Time Hardware Rail Sequencing

```
┌────────────────────────────────────────────────────────────────────────┐
│ 1. Vendor boot0 (SRAM @ 0x47000)                                       │
│    • Reads power.fex binary block from boot_package.fex (Sector 24576) │
│    • Configures AXP8191 DCDC2/3 (0.9V VDD-CPU) & DCDC4 (1.1V LPDDR5)   │
│    • Calibrates LPDDR5 PHY at target voltage                           │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ 2. scp.fex Execution (XuanTie E902 @ 0x40014000)                       │
│    • Reads [power_sply] tables from DRAM                               │
│    • Initializes Reduced Serial Bus (RSB) at 0x07083000                │
│    • Programs AXP8191 DCDC1 = 3.3V (Enables FE1.1S Hub & AIC8800 Wi-Fi)│
│    • Programs ALDO1/3 & CLDO1 for system I/O buses                     │
│    • Enters SCPI command listener loop for TF-A BL31 DVFS calls        │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ 3. Mainline U-Boot Proper (ARM EL2 @ 0x4A000000)                       │
│    • Queries PMIC state and verifies voltage rail stability            │
│    • Boots Linux kernel 7.1 PREEMPT_RT with active DTB regulator trees │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 5. E902 as a Dedicated I/O Processor (Offloading Linux)

In Mode 2, the XuanTie E902 is repurposed as a **high-speed I/O Front-End and Hardware Serializer**. Instead of burdening the Linux ARM host with thousands of individual hardware interrupts per second, the E902 handles all time-critical peripheral transactions directly:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        XUANTIE E902 I/O FRONT-END PROCESSOR                            │
├─────────────────────────┬────────────────────────────┬─────────────────────────────────┤
│    PERIPHERAL BUS       │      HARDWARE TARGET       │        REAL-TIME TASK           │
├─────────────────────────┼────────────────────────────┼─────────────────────────────────┤
│ • High-Speed SPI        │ IMU (ICM-42688 / BMI270)   │ 1 kHz - 8 kHz burst sampling   │
│ • PIO Edge Interrupts   │ IMU DRDY, Encoders, PPS    │ Microsecond-accurate ISR capture│
│ • I2C (TWI Bus)         │ Barometer, Compass         │ Background telemetry polling   │
│ • Serial UART           │ GPS (NMEA / UBX Protocol)  │ Hardware FIFO drain & parsing   │
└─────────────────────────┴─────────────┬──────────────┴─────────────────────────────────┘
                                        │
                                        │  Batched binary frames + 64-bit timestamps
                                        ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│               LOCK-FREE SPSC RINGBUFFER IN SYSTEM SRAM A2 (0x00040000)                 │
└───────────────────────────────────────┬────────────────────────────────────────────────┘
                                        │
                                        │  Mailbox Doorbell Interrupt (msgbox0)
                                        ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        LINUX ARM HOST (PREEMPT_RT EL1)                                 │
│ • 0% CPU cycles wasted on high-frequency per-byte / per-sample IRQ thrashing            │
│ • Wakes only to consume pre-validated, timestamped sensor batches in user space         │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### Key Advantages of the I/O Offload Model:
1. **Zero Linux IRQ Jitter**: Servicing high-speed UART bytes (e.g. 460800 baud GPS streams) and 8 kHz IMU DRDY lines inside Linux causes frequent context switches and cache thrashing. The E902 absorbs 100% of these interrupts.
2. **Microsecond Hardware Timestamping**: Sensor frames and GPS PPS pulses are timestamped in hardware by the E902 at the exact moment of arrival before any OS scheduling delay occurs.
3. **Fail-Safe Operation**: If the Linux host temporarily spikes in load or performs an update, the E902 continues to capture sensor history and maintain actuator holding states without missing a single byte.

---

## 6. Mode 2 Implementation Blueprint (Linux RemoteProc & Real-Time Control)

When building embedded control systems, robotics, or industrial automation platforms that run 24/7 without suspend/resume, follow this blueprint across all firmware and kernel layers:

### A. ARM Trusted Firmware (TF-A / BL31) Changes
To allow Linux `remoteproc` to write `0x07032204` and load code into System SRAM A2 without bus aborts:

1. **Un-gate R_SPC and R_TZMA in `plat/allwinner/common/sunxi_security.c`**:
   ```c
   void sunxi_security_setup(void)
   {
       /* Configure R_SPC to grant Non-Secure access to CPUS / PRCM / R_PIO */
       mmio_write_32(SUNXI_R_SPC_BASE + 0x04, 0xffffffff);
       mmio_write_32(SUNXI_R_SPC_BASE + 0x14, 0xffffffff);
       mmio_write_32(SUNXI_R_SPC_BASE + 0x24, 0xffffffff);
       mmio_write_32(SUNXI_R_SPC_BASE + 0x34, 0xffffffff);

       /* Open System SRAM A2 (0x00040000 - 0x00078000) to Non-Secure world */
       mmio_write_32(SUNXI_R_TZMA_BASE + 0, 0);
       mmio_write_32(SUNXI_R_TZMA_BASE + 4, 0);
       mmio_write_32(SUNXI_R_TZMA_BASE + 8, 0);
   }
   ```
2. **Native Power Management Fallback**:
   When `scp.fex` is absent, TF-A detects that SCPI is unavailable and falls back to `sunxi_native_pm.c` (direct watchdog reset and CPU power gating without requiring SCP).

---

### B. U-Boot & TOC1 Packaging Changes
1. **Packaging**: Generate a TOC1 container without `scp.fex` (e.g. `radxa_a733_bootloader_rt.bin`).
2. **PMIC Standalone Initialization**:
   Ensure U-Boot initializes the **AXP8191 PMIC** over RSB (`0x07083000`):
   - Enable `DCDC1` (3.3V system power for USB hub, Wi-Fi, Ethernet).
   - Enable `ALDO1` (3.3V for `VCC-PL` and `VCC-PM` I/O banks).
3. **Core Reset**: Leave the E902 held in reset in `r_ccu` at boot.

---

### C. Linux Kernel Driver & Device Tree Changes
1. **Device Tree Overlay (`cubie-a7a-rproc.dtso`)**:
   ```dts
   /dts-v1/;
   /plugin/;

   / {
       compatible = "radxa,cubie-a7a", "allwinner,sun60i-a733";

       fragment@0 {
           target = <&rproc>;
           __overlay__ {
               status = "okay";
               memory-region = <&rproc_carveout>;
           };
       };

       fragment@1 {
           target-path = "/reserved-memory";
           __overlay__ {
               #address-cells = <2>;
               #size-cells = <2>;

               rproc_carveout: rproc@4e000000 {
                   compatible = "shared-dma-pool";
                   reg = <0x00 0x4e000000 0x00 0x01000000>; /* 16 MB pool */
                   no-map;
               };
           };
       };
   };
   ```

2. **Driver (`sunxi_rproc.c`)**:
   - Register `"allwinner,sun60i-a733-rproc"`.
   - Map **SRAM A2 (`0x00040000`–`0x00073FFF`, 208 KB)** as the primary fast execution window via `devm_ioremap_wc()` with `is_iomem = true`.
   - Write entry address to `priv->cfg_va + 0x0204` (`E902_STA_ADD_REG`) during `sunxi_rproc_start()`.
   - Control core execution via `r_ccu` reset bits.

---

### D. RISC-V Firmware Compiler & ABI Contract
Because the E902 is RV32E, firmware applications must be built targeting:
```bash
-march=rv32emc_zicsr -mabi=ilp32e -mcmodel=medany
```
- **Registers**: Uses only `x0`–`x15`. Compiling with standard `ilp32` (32 registers) or float instructions will trigger illegal instruction hardware faults.
- **Linker Address**: Link firmware to `ORIGIN = 0x00044000` (System SRAM A2) or `ORIGIN = 0x4E000000` (DRAM Carveout).

---

## 7. Comparison Matrix: Mode 1 vs. Mode 2

| Feature / Subsystem | Mode 1: Suspend/Resume (`scp.fex`) | Mode 2: Linux RemoteProc (Real-Time Control) |
| :--- | :--- | :--- |
| **Primary Use Case** | Consumer Battery Devices (S3 Sleep) | **24/7 Embedded Control, Robotics, Real-Time I/O** |
| **TOC1 Container** | Contains `scp.fex` | **Omits `scp.fex`** |
| **E902 Boot Time** | Starts at power-on (`boot0`) | **Starts dynamically from Linux (`remoteproc`)** |
| **PMIC Control** | Handled by `scp.fex` over RSB | **Handled directly by U-Boot / Linux PMIC driver** |
| **BL31 Security** | `0x07032204` locked in Secure World | **`R_SPC` & `R_TZMA` unlocked for Non-Secure EL1** |
| **DRAM Map** | `0x40014000` reserved (`no-map`) | **`0x4E000000` DMA pool / SRAM A2 (`0x00040000`)** |
| **Linux RemoteProc** | Disabled (`status = "disabled"`) | **Enabled (`sunxi_rproc.c`)** |
| **Firmware Toolchain**| Vendor binary | **Bare-metal C++ (`-march=rv32emc_zicsr -mabi=ilp32e`)** |
