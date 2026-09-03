# Allwinner A733 (sun60iw2) XuanTie E902 Boot & Coprocessor Architecture

**Document Version:** 1.0  
**Date:** September 3, 2026  
**Target SoC:** Allwinner A733 / sun60iw2 (Radxa Cubie A7A & Cubie A7Z)  
**Author / Integration:** Flight Controller & Heterogeneous Compute Team  

---

## 1. Executive Summary

This document describes the silicon topology, hardware memory layout, TrustZone security boundaries, and boot sequence of the embedded **T-Head XuanTie E902 32-bit RISC-V core** on the Allwinner A733 (sun60iw2) SoC. It clarifies the role of the legacy Allwinner System Control Processor (`scp.fex`) versus modern Linux `remoteproc` execution for high-speed avionics and real-time flight control loops.

---

## 2. Silicon Topology & Domain Differences

Unlike the Allwinner T527 (sun60iw1) and A523 (sun55iw3) which integrate a high-performance XuanTie E906/E907 core with dedicated ITCM/DTCM inside an open MCU/DSP peripheral domain (`0x07100000+`), the **A733 integrates a XuanTie E902 (RV32EMC) core inside the CPUS / Always-On (`R_`) power management subsystem**:

| Feature | Allwinner T527 / A523 | Allwinner A733 (sun60iw2) |
| :--- | :--- | :--- |
| **RISC-V Core IP** | XuanTie E906 / E907 (RV32IMAFDC + FPU) | XuanTie E902 (RV32EMC, 16 registers) |
| **Subsystem Domain** | MCU / DSP Subsystem (`0x07100000+`) | CPUS / `R_` Always-On Domain (`0x07000000+`) |
| **Control / Config Base** | `0x07130000` (MCU CFG) | `0x07032000` (`E902_CFG_BASE`) |
| **Boot Address Register** | `0x07130204` (`STA_ADD`) | `0x07032204` (`STA_ADD`) |
| **TrustZone Protection** | **Non-Secure (Open)**: Writable directly from Linux EL1. | **Secure-Gated**: Interconnect write-protects register against Non-Secure EL1. |
| **Memory Available** | 64 KB ITCM, 64 KB DTCM, Shared SRAM | System SRAM A2 (`0x00040000`), DRAM Carveout |

---

## 3. The TrustZone Security Boundary & Hardware Readback

### A. Register Protection at `0x07032204`
The start address register `0x07032204` defines the instruction fetch address of the E902 core upon reset.
* In hardware readback, `devmem 0x07032204 32` reads **`0x40014000`**.
* Direct MMIO writes (`writel()` or `/dev/mem`) from Non-Secure Linux EL1 are filtered out by the ARM TrustZone bus firewall. The register remains `0x40014000`.
* Calling ARM SMCCC (`0x8000ff06` `ARM_SVC_WRITE_SEC_REG`) returns `0xffffffffffffffff` (`SMCCC_RET_NOT_SUPPORTED`), proving that the factory ARM Trusted Firmware (BL31) deliberately locks `0x07032204` and disallows modifying the entry vector at runtime.

### B. The Meaning of `0x40014000`
In Allwinner's SPL architecture (`spl-pub/include/configs/sun60iw2p1.h`):
```c
#define SCP_CODE_DRAM_OFFSET    (0x14000)
```
With DRAM physically mapped at `0x40000000`, offset `0x14000` equals **`0x40014000`**.
This proves that early boot code (`sboot`) programs the E902 to boot directly from DRAM at `0x40014000`.

---

## 4. Allwinner Legacy `scp.fex` vs. Flight Controller Remoteproc

### A. Factory Purpose (`scp.fex`)
Allwinner developed the E902 firmware (`scp.fex`, also known as `arisc`) for consumer battery-operated devices (tablets and OTT boxes):
1. **Suspend-to-RAM (Deep Sleep)**: When the host Cortex-A55 cores enter S3 sleep (`echo mem > /sys/power/state`), the ARM cores power down. The E902 stays alive on a 32 kHz crystal to monitor power buttons, RTC alarms, or IR remotes.
2. **Power Sequencing**: Helping the PMIC shut down power rails during battery cutoff.
3. **Idle State**: During standard awake Linux execution, `scp.fex` executes a low-power `wfi` (Wait For Interrupt) polling loop and performs zero active tasks.

### B. Flight Controller / Real-Time Avionics Role
In an unmanned aerial vehicle (UAV) or high-reliability robotics controller:
* **The system never sleeps**: Suspend-to-RAM is completely disabled; deep sleep in mid-flight would cause an unrecoverable loss of control.
* **Deterministic Core**: The E902 is repurposed as a dedicated hard-real-time processor handling 1 kHz IMU filtering, motor PWM generation, and telemetry without Linux scheduler jitter.

---

## 5. Architectural Implementation Options

### Option 1: Linux Remoteproc Runtime Loading (DRAM Carveout)
* **Device Tree**: Carve out `0x40014000` (256 KB) using a `reserved-memory` node with `no-map`. This prevents the Linux page allocator from allocating kernel or user pages across `0x40014000`.
* **Remoteproc Mapping**: Add `<0x40014000 0x40000>` to `reg` in `remoteproc@7032000` with `reg-names = "cfg", "dram", "sram"`.
* **Firmware Linker**: Link `riscv-firmware.elf` at `0x40014000`.
* **Execution**: Linux `remoteproc` loads the ELF directly into `0x40014000` and pulses the E902 reset line in `R_CCU`. The core boots at `0x40014000` into your firmware.

### Option 2: Pure Bootloader-Level Decoupling (Own U-Boot / ATF)
To prevent `sboot` from ever touching the E902 or loading `scp.fex`:
1. **Package Configuration**: In the U-Boot package generator (`dragon_toc.cfg` / `boot_package.cfg`), remove the `item=scp, scp.fex` line.
2. **Result**: `sboot` does not allocate DRAM for SCP and leaves the E902 held in reset from cold boot.
3. **Clean Handover**: When Linux boots, the E902 is untouched and cold-reset, ready for full initialization by `sunxi_rproc`.
