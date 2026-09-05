# Direct Memory-Mapped Debug Access (DMEM) Architecture & Co-Processor Debugging

## Executive Summary

Direct Memory-Mapped Debug Access (**DMEM**) is an on-chip debugging paradigm used in modern heterogeneous asymmetric multiprocessing (AMP) SoCs. It connects core debug modules (ARM CoreSight DAPBUS or RISC-V Debug Module Interface) directly to the system interconnect, enabling native, Linux-hosted OpenOCD and GDB remote debugging without external hardware debug probes.

This document describes:
1. How the `dmem` architecture is implemented on SoCs from Texas Instruments, STMicroelectronics, and NXP.
2. The current hardware reality on **Allwinner T527** silicon.
3. The future roadmap and hope for Allwinner `dmem` support.
4. Recommended debugging workflows for active T527 firmware development.

---

## 1. How `dmem` Operates on Heterogeneous SoCs (TI, ST, NXP)

In SoCs with native `dmem` support (e.g. TI AM62x / AM64x / K3, STMicroelectronics STM32MP1 / STM32MP2, and NXP i.MX):

```text
  ┌────────────────────────────────────────────────────────┐
  │                    GDB Debugger                        │
  │   (e.g. riscv-none-elf-gdb / gdb-multiarch)            │
  └───────────────────────────┬────────────────────────────┘
                              │ 
                              │  GDB Remote Serial Protocol (RSP)
                              │  (TCP Port 3333)
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │              OpenOCD (Running on Linux Target)         │
  │              (adapter driver dmem transport)           │
  └───────────────────────────┬────────────────────────────┘
                              │ 
                              │  Physical System Bus Access
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │       Memory-Mapped Debug Module Interface (DM / DMI)  │
  │        (Directly accessible by non-secure ARM core)    │
  └────────────────────────────────────────────────────────┘
```

### The Soft-Wire JTAG Paradigm
- **Pioneering Work**: Demonstrated by Texas Instruments (Nishanth Menon) and the BeagleBoard.org Foundation (Jason Kridner) on platforms like the BeaglePlay and BeagleBone AI-64.
- **Mechanism**: In SoCs supporting this model, the host maps the auxiliary core debug registers directly into virtual address space. OpenOCD reads and writes debug run-control registers (`dmcontrol`, `dmstatus`, CoreSight DP/AP) using standard 32-bit load/store instructions.
- **Benefit**: Fully self-hosted debugging over SSH. Developers can set breakpoints, single-step co-processor firmware, and inspect registers without soldering JTAG headers or purchasing external debug probes.

---

## 2. Allwinner T527 Silicon Reality

On current **Allwinner T527 silicon (Radxa Cubie A5E)**:
- The XuanTie E907 co-processor is fully functional for real-time applications with dedicated clocks (`mcu_ccu`), 128KB PubSRAM C, 256KB Dedicated MCU SRAM, and open non-secure MMIO reset control at `0x07102124`.
- However, **Allwinner does not route a memory-mapped `dmem` interface** for the XuanTie RISC-V Debug Module (DM) into the non-secure ARM bus interconnect.
- Because the debug module is not memory-mapped to the ARM interconnect, target-side OpenOCD cannot attach directly over the bus without external hardware debug probes.

### Future Silicon Outlook
We hope that Allwinner will incorporate a standard memory-mapped `dmem` bus interface in future SoC revisions. Adding a non-secure MMIO window to the RISC-V Debug Module Interface (DMI) will allow the Linux open-source community to use native OpenOCD and GDB workflows directly on Allwinner SoCs, matching the experience on TI and ST platforms.

---

## 3. Standard Debugging Methods on Current T527 Silicon

For developers building firmware for the XuanTie co-processor on T527 today, the following standard mechanisms provide robust, high-performance diagnostics:

### A. Linux RemoteProc Trace Buffer (`trace0`)
The standard Linux `remoteproc` subsystem automatically exposes circular trace buffers declared in the firmware ELF resource table:
```bash
# Start the co-processor
echo "riscv-firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Read live firmware logs from debugfs
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

### B. Dedicated Serial Console (`S_UART0`)
The XuanTie co-processor has direct access to `S_UART0` at physical base `0x07080000`. This enables low-overhead, independent serial output (115200 baud) that does not interfere with the main Linux kernel console (`UART0` @ `0x02500000`).

### C. Lock-Free Shared SRAM Ring Buffers
High-bandwidth telemetry (such as high-rate IMU samples or motor telemetry) can be streamed through Shared SRAM A2 (`0x00040000`–`0x00073FFF`) using lock-free Single Producer Single Consumer (SPSC) circular queues.

### D. Hardware Mailbox Doorbell IPC
The Allwinner hardware mailbox at `0x03003000` provides sub-microsecond interrupt signaling between the ARM64 Linux host and the RISC-V core.

### E. Physical Hardware JTAG Probe
For source-level interactive debugging with breakpoints and single-stepping, developers can connect a physical hardware debug probe (e.g. T-Head CK-Link, SEGGER J-Link, or FT2232D) to the board's dedicated JTAG pins and run OpenOCD on their host development PC.

---

## 4. Reset & Clocks Architecture on T527

The T527 provides clean, non-secure MMIO register control for the RISC-V MCU subsystem without requiring TrustZone Secure Monitor Calls (`smc`):

1. **MCU CCU Register (`0x07102124`)**:
   - Bit 0: `CLK_BUS_MCU_RISCV_CFG` (MCU CFG bus clock enable)
   - Bit 16: `RST_BUS_MCU_RISCV_CFG` (CFG bus reset release)
   - Bit 17: `RST_BUS_MCU_RISCV_DEBUG` (Debug module reset release)
   - Bit 18: `RST_BUS_MCU_RISCV_CORE` (Core execution reset release)

2. **Boot Entry Vector (`STA_ADD_REG` @ `0x07130204`)**:
   - The XuanTie E907 boots from the address programmed into `STA_ADD_REG` (e.g. `0x00020000` PubSRAM C).
   - The mainline `sunxi_rproc` driver handles loading the ELF into SRAM, un-gating clocks, setting `STA_ADD_REG`, and releasing reset cleanly.

---

## 5. References & Prior Art

1. **"Debugging Heterogeneous SoC Using OpenOCD"**
   - **Author**: Nishanth Menon (Texas Instruments Inc.)
   - **Summary**: Technical presentation detailing self-hosted OpenOCD debugging (`--enable-dmem`) on TI AM62x/AM64x SoCs.

2. **OpenOCD `dmem` Driver**:
   - `src/jtag/drivers/dmem.c` in upstream OpenOCD tree.
   - `board/ti_am625_swd_native.cfg` — Reference native OpenOCD configuration script using `adapter driver dmem`.

3. **RISC-V External Debug Support Specification**:
   - Version 0.13 / 1.0 (RISC-V International).
   - Standardizes Debug Module (DM) register definitions (`dmcontrol`, `dmstatus`, `abstractcs`, `command`, `data0`).
