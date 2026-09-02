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
| **Dedicated SRAM** (`r_sram`) | `0x07280000` | `0x00000000` | 256 KB | **Hardware Reset Vector Table (`.vectors`)**, ISRs, Fast Code |
| **Shared SRAM** (`sram`) | `0x00040000` | `0x00040000` | 160 KB | Shared SRAM A2 (Telemetry, SPSC IPC Rings) |
| **DRAM Window** | `0x40000000`..`0x7FFFFFFF` | `0x40000000`..`0x7FFFFFFF` | 1 GiB | Direct Physical DDR access (DMA payloads) |
| **MCU CCU** (`cfg`) | `0x07102000` | `0x07102000` | 4 KB | E907 Core Reset, Bus Bridges, Clock Controls |
| **MSGBOX (IPI)** | `0x03004000` | `0x07094000` | 4 KB | Inter-Core Mailbox Doorbell IRQ |
| **S_UART0 (Debug)** | `0x07080000` | `0x07080000` | 4 KB | RISC-V Dedicated Serial Console (115200) |
| **S_GPIO (R-PIO)** | `0x07025000` | `0x07025000` | 8 KB | Low-Latency I/O (`PL`, `PM` banks) |

---

## 3. Power, Reset & Clock Control (MCU CCU Registers at `0x07102000`)

The XuanTie E907 co-processor lifecycle and bus bridges are controlled via the **MCU CCU (`0x07102000`)**:

### Key Control Registers:
| Offset | Register Name | Bitfields | Functional Description |
| :--- | :--- | :--- | :--- |
| `0x0108` | `TZMA0_REG` | `0x00010001` | **SRAM Adapter Bridge**: Un-gates CPU/DMA access to Dedicated SRAM. |
| `0x010C` | `TZMA1_REG` | `0x00010001` | **Peripheral Adapter Bridge**: Un-gates core peripheral access. |
| `0x0114` | `PUBSRAM_GATE` | `0x00010001` | **PubSRAM Clock Gate**: Connects core to shared system memory. |
| `0x011C` | `MBUS_CLK` | `0x00000003` | **MBUS Subsystem Clock**: Interconnect clock for high-speed transfers. |
| `0x0120` | `RISCV_CLK` | `0x80000000` | **Core Execution Clock**: Enables 24 MHz OSC reference clock. |
| `0x0124` | `CORE_RST` | **`0x00070001`** (Run)<br>**`0x00030001`** (Reset) | **Core Execution Reset**: Writing `0x00070001` releases the XuanTie E907 core from reset to begin instruction fetch from `0x00000000`. |
| `0x0128` | `MSGBOX_GATE` | `0x00010001` | **Hardware Mailbox Gate**: Enables hardware IPC mailbox. |

---

## 4. Linux 7.1 Remote Processor (`remoteproc`) Integration

The kernel driver `drivers/remoteproc/sunxi_rproc.c` adheres strictly to standard upstream Linux remoteproc architecture:
* **No Synthetic Code / No Trampolines**: The driver is a pure lifecycle manager. It does not modify memory or inject trampolines.
* **Firmware Contract**: The firmware ELF **must** link its reset vector table at **`0x00000000`** (`ORIGIN = 0x00000000` in Dedicated SRAM). The hardware E907 core always fetches its reset instruction directly from address `0x00000000`.

### A. Lifecycle Management Commands
```bash
# 1. Install bare-metal ELF binary to Linux firmware directory
cp firmware.elf /lib/firmware/riscv-firmware.elf

# 2. Assign firmware to remoteproc instance
echo riscv-firmware.elf > /sys/class/remoteproc/remoteproc0/firmware

# 3. Boot XuanTie E907 RISC-V core
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. View real-time printk / trace buffer from RISC-V
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# 5. Stop co-processor
echo stop > /sys/class/remoteproc/remoteproc0/state
```

### B. Device Tree Node (A733 DTS)
```dts
rproc: remoteproc@7102000 {
    compatible = "allwinner,sun60i-a733-rproc", "allwinner,sunxi-rproc";
    reg = <0x07102000 0x1000>,
          <0x07280000 0x40000>,
          <0x00040000 0x28000>;
    reg-names = "cfg", "r_sram", "sram";
    clocks = <&r_ccu CLK_RISCV_24M>,
             <&r_ccu CLK_RISCV_CFG>,
             <&r_ccu CLK_RISCV>;
    clock-names = "parent", "bus", "core";
    resets = <&r_ccu RST_BUS_RISCV_CFG>;
    reset-names = "cfg";
    mboxes = <&msgbox 0>;
    mbox-names = "tx";
    memory-region = <&rproc_trace>;
    memory-region-names = "trace";
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
