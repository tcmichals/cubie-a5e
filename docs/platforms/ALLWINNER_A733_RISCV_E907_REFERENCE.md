# Allwinner A733 / T527 XuanTie E907 RISC-V Co-Processor Reference Guide

> **Target Co-Processor**: T-Head / XuanTie E907 (RV32IMAC @ up to 600 MHz) with TCM, FPU, and CLIC.  
> **Host Kernel**: Linux 7.1 PREEMPT_RT (`drivers/remoteproc/sunxi_rproc.c`)  
> **Firmware SDK**: [`RISCV_Linux_T527_A733`](file:///run/media/tcmichals/projects/radxa/RISCV_Linux_T527_A733/) (AbstractX C++20 coroutine HAL)  
> **Official Datasheet/Manual**: *Allwinner A733 User Manual V0.92*, Chapter 4 (PRCM) & Chapter 5/12 (CPUS & Interrupts).

---

## 1. Co-Processor Architectural Summary

The Allwinner A733 integrates an embedded **T-Head XuanTie E907 32-bit RISC-V core** (alongside an E902 power-management controller) in the Always-On `CPUS` domain.

```
+---------------------------------------------------------------------------------------+
|                          XUANTIE E907 RISC-V SUBSYSTEM (A733)                         |
|                                                                                       |
|  +---------------------------------------------------------------------------------+  |
|  | Core Specifications:                                                            |  |
|  | - ISA: RV32IMAFDCP (32-bit RISC-V, Integer, Mult/Div, Atomic, Float, Double)    |  |
|  | - Clock Speed: Up to 600 MHz (Clocked via PRCM @ 0x07010000)                    |  |
|  | - Pipeline: 5-stage, single-issue, in-order execution                          |  |
|  | - Interrupt Controller: Core-Local Interrupt Controller (CLIC)                   |  |
|  +---------------------------------------------------------------------------------+  |
|                                                                                       |
|  +-----------------------------------+   +-----------------------------------------+  |
|  |      Zero-Wait Tightly Coupled    |   |           Shared High-Speed SRAM        |  |
|  |               (TCM)               |   |                                         |  |
|  |  +-----------------------------+  |   |  +-----------------------------------+  |  |
|  |  | ITCM: 64 KB (Core: 0x00000) |  |   |  | SRAM C: 320 KB                    |  |  |
|  |  | (Vector table & ISRs)       |  |   |  | (Core: 0x07130000 / Host: 0x07130)|  |  |
|  |  +-----------------------------+  |   |  +-----------------------------------+  |  |
|  |  +-----------------------------+  |   |  +-----------------------------------+  |  |
|  |  | DTCM: 64 KB (Core: 0x80000) |  |   |  | Shared SRAM: 512 KB               |  |  |
|  |  | (Stack & Coroutine Pools)   |  |   |  | (Mapped to DDR window via remap)  |  |  |
|  |  +-----------------------------+  |   |  +-----------------------------------+  |  |
|  +-----------------------------------+   +-----------------------------------------+  |
|                                                                                       |
|  +---------------------------------------------------------------------------------+  |
|  | Inter-Processor Communication (IPC) & Peripherals                               |  |
|  | - Hardware Mailbox: 8-Channel Doorbell (Host: 0x03004000, RISC-V: 0x07094000)   |  |
|  | - SPSC Lock-Free Ring Descriptors in SRAM C (sub-20ns latency)                  |  |
|  | - Dedicated CPUS Peripherals: S_UART0/1, S_TWI0..2, S_SPI, S_PWM, RTC, S_GPIO   |  |
|  +---------------------------------------------------------------------------------+  |
+---------------------------------------------------------------------------------------+
```

---

## 2. Dual Memory Map: Host ARM64 vs RISC-V Core

Because the RISC-V core has its own internal bus address mapping, physical addresses differ between the host ARM Cortex-A76/A55 application cores and the XuanTie E907 core:

| Memory Region | Host (ARM64) Address | RISC-V Core Address | Size | Primary Usage |
| :--- | :--- | :--- | :--- | :--- |
| **ITCM** | `0x07110000` | `0x00000000` | 64 KB | Vector Table, Reset Trampoline, Fast ISRs |
| **DTCM** | `0x07120000` | `0x00080000` | 64 KB | Stack Pointer (`sp`), Coroutine Frame Pools |
| **SRAM C** | `0x07130000` | `0x07130000` | 320 KB | Main Application Code & SPSC IPC Rings |
| **DRAM Window** | `0x40000000`..`0x7FFFFFFF` | `0x40000000`..`0x7FFFFFFF` | 1 GiB | Direct Physical DDR access (DMA payloads) |
| **PRCM (R-CCU)**| `0x07010000` | `0x40010000` / `0x07010000`| 64 KB | Power, Clock, and Reset Registers |
| **MSGBOX (IPI)**| `0x03004000` | `0x07094000` | 4 KB | Inter-Core Mailbox Doorbell IRQ |
| **S_UART0 (Debug)**| `0x07080000` | `0x07080000` | 4 KB | RISC-V Dedicated Serial Console (115200) |
| **S_GPIO (R-PIO)** | `0x07025000` | `0x07025000` | 8 KB | Low-Latency I/O (`PL`, `PM` banks) |

---

## 3. Power, Reset & Clock Control (PRCM Registers)

The RISC-V core is controlled via the Always-On PRCM block (`0x07010000`).

### Key Control Registers:
| Offset | Register Name | Bitfields | Functional Description |
| :--- | :--- | :--- | :--- |
| `0x0210` | `RISCV_24M_CLK_REG` | `[25:24]` | **Clock Source Select**: `00` = DCXO (24MHz), `01` = RTC_32K, `10` = 16M_RC |
| `0x021C` | `RISCV_BGR_REG` | **Bit 16**: `RISCV_CFG_RST`<br>**Bit 1**: `RISCV_CFG_GATING`<br>**Bit 0**: `RISCV_GATING` | **Bus Gating & Reset**: Controls peripheral clocks and reset de-assertion to the core subsystem. |
| `0x0020` | `DSP_CLK_REG` | `[31]`: Enable<br>`[26:24]`: Factor N<br>`[18:16]`: Factor M | Main PLL Multiplier / Divider for core execution clock (up to 600 MHz). |
| `0x0100` | `DSP_RST_REG` | **Bit 17**: `SYS_RST`<br>**Bit 16**: `CORE_RST` | **Core Execution Reset**:<br>- Assert `SYS_RST` while holding `CORE_RST` to initialize memory.<br>- Release `CORE_RST` to start executing instructions from `0x00000000`. |

---

## 4. Linux 7.1 Remote Processor (`remoteproc`) Integration

The kernel driver [`drivers/remoteproc/sunxi_rproc.c`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/patches/linux/0002-remoteproc-sunxi-add-allwinner-riscv-remoteproc.patch) manages the complete lifecycle of the XuanTie core:

### A. Lifecycle Management Commands
```bash
# 1. Install bare-metal ELF binary to Linux firmware directory
cp firmware.elf /lib/firmware/sunxi_riscv_fw.elf

# 2. Assign firmware to remoteproc instance
echo sunxi_riscv_fw.elf > /sys/class/remoteproc/remoteproc0/firmware

# 3. Boot XuanTie E907 RISC-V core
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. View real-time printk / trace buffer from RISC-V
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# 5. Stop co-processor
echo stop > /sys/class/remoteproc/remoteproc0/state
```

### B. Device Tree Node (A733 DTS)
```dts
rproc: remoteproc@7010000 {
    compatible = "allwinner,sun60i-a733-rproc", "allwinner,sun55i-a527-rproc", "allwinner,sunxi-rproc";
    reg = <0x00 0x07010000 0x00 0x1000>,  /* PRCM / CFG Base */
          <0x00 0x07110000 0x00 0x10000>, /* ITCM (64 KB) */
          <0x00 0x07120000 0x00 0x10000>, /* DTCM (64 KB) */
          <0x00 0x07130000 0x00 0x50000>; /* SRAM C (320 KB) */
    reg-names = "cfg", "itcm", "dtcm", "sram";
    clocks = <&r_ccu CLK_RISCV_24M>,
             <&r_ccu CLK_RISCV_CFG>,
             <&r_ccu CLK_RISCV>;
    clock-names = "parent", "bus", "core";
    resets = <&r_ccu RST_BUS_RISCV_CFG>;
    reset-names = "cfg";
    mboxes = <&msgbox 0>;
    mbox-names = "tx";
    status = "okay";
};
```

---

## 5. Standalone Firmware SDK: AbstractX & HAL

The full firmware workspace is located at:  
📂 **[`/run/media/tcmichals/projects/radxa/RISCV_Linux_T527_A733/`](file:///run/media/tcmichals/projects/radxa/RISCV_Linux_T527_A733/)**

### Firmware Architectural Highlights:
1. **AbstractX Native Coroutines**:
   - Stackless C++20 coroutines (`co_await`) without dynamic memory allocation (`malloc`).
   - Sub-20ns resumption latency on pin edge triggers (`DRDY`) and timer expirations.
2. **Zero-Copy Lock-Free IPC Rings**:
   - 16-byte Single Producer Single Consumer (SPSC) descriptors mapped into shared SRAM C.
   - Hardware Mailbox doorbell interrupts ensure ARM64 Linux and RISC-V synchronize in a single CPU cycle.
3. **Diagnostics & Binary Tracing**:
   - Google Pigweed compile-time tokenized string logging (4 bytes per log message).
   - BareCTF binary trace logging mapped directly into Linux `debugfs/remoteproc/remoteproc0/trace0`.
