# Unlocking the XuanTie RISC-V Core on Allwinner T527 / Radxa Cubie A5E
### Part 1: Architecture & Boot Mechanics

Heterogeneous multi-core SoCs—pairing high-performance 64-bit ARM Cortex-A 
application cores with low-power, deterministic auxiliary microcontrollers—have 
become the standard architecture for modern embedded systems, robotics, and 
industrial automation. Silicon like the **Allwinner T527 / A527** (featured on 
the **Radxa Cubie A5E**) integrates an octa-core ARM Cortex-A55 cluster 
alongside an auxiliary **XuanTie E907 RISC-V core** (RV32IMAFCX @ 200 MHz).

Getting this co-processor online requires establishing reliable hardware 
lifecycle control, clock tree synchronization, and deterministic memory 
placement before loading production firmware.

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

This article is **Part 1 of a multi-part hands-on series** documenting the 
practical bring-up and engineering realities of the XuanTie E907 RISC-V 
co-processor under Linux:

* **Part 1 (This Article)**: Bill of Materials, architectural rationale, TRM memory maps, SRAM architecture & RemoteProc boot mechanics, `startup.S` FPU initialization, and debugging realities on live silicon.
* **Part 2**: Authoring the Linux `remoteproc` kernel driver, memory-mapped ELF loading into 512 KB continuous SRAM, and systematically proving hardware state with the `firmware/e907-riscv/apps` verification suite.
* **Part 3**: Inter-processor communication (IPC) deep dive—lock-free shared SRAM + hardware mailbox doorbells, standard VirtIO RPMsg, and hybrid SRAM/DDR bulk streaming.
* **Part 4**: Deep dive into modern zero-allocation C++ coroutines and event loops on bare-metal RISC-V.

---

## 1. Bill of Materials & Hardware Prerequisites

To follow this series and replicate the tests directly on physical hardware, 
you will need:

### Hardware Setup
* **Target SBC**: **Radxa Cubie A5E** (Allwinner T527 / A527 SoC, 2GB–4GB LPDDR4X, eMMC / MicroSD).
* **Linux Host Serial Console**: 3.3V TTL USB-to-UART adapter connected to the primary debug header (`UART0` @ `0x02500000`, 115200 8N1).
* **Auxiliary RISC-V Serial Diagnostics**: Secondary 3.3V TTL USB-to-UART adapter connected to the dedicated CPUS Always-On serial pins (`S_UART0` @ `0x07080000`, 115200 8N1).
* **Power Supply**: Standard 5V / 3A USB Type-C power adapter.
* **Optional Hardware Probe**: T-Head CK-Link or SEGGER J-Link for instruction-level hardware single-stepping via board test pads.

### Host Toolchain & Software Prerequisites
* **RISC-V Bare-Metal Cross-Compiler**: `riscv-none-elf-gcc` / `riscv-none-elf-g++` (RV32IMAFC ABI `ilp32f` or `ilp32d`).
* **ARM64 Linux Kernel Toolchain**: `aarch64-linux-gnu-gcc` (GCC 13+ recommended).
* **Device Tree Compiler**: `dtc` (v1.6.0+).
* **Linux Distribution**: Upstream Linux kernel (6.6+ or 7.x PREEMPT_RT) with `CONFIG_REMOTEPROC=y` and `CONFIG_MAILBOX=y`.

---

## 2. Immediate "Quick Win": Verifying Hardware Readiness

Before diving into assembly and driver internals, you can immediately verify 
whether your running Linux system recognizes the RemoteProc subsystem and 
co-processor hardware memory nodes:

```bash
# 1. Check for registered RemoteProc subsystem instances:
ls -la /sys/class/remoteproc/
# Expected output: remoteproc0 (XuanTie E907 RISC-V)

# 2. Inspect kernel dmesg for remoteproc driver probing:
dmesg | grep -i -E "remoteproc|rproc|sunxi"

# 3. Check live Device Tree nodes for the XuanTie E907 block:
ls -d /sys/firmware/devicetree/base/soc/remoteproc@7130000
```

If `/sys/class/remoteproc/remoteproc0` is present, your kernel is ready to 
manage the co-processor lifecycle directly via standard sysfs interfaces.

---

## 3. Why Use the XuanTie RISC-V Co-Processor?

Modern embedded Linux platforms excel at complex workloads—networking, file 
systems, multimedia pipelines, computer vision, and machine learning. However, 
running jitter-sensitive, hard real-time control tasks directly on an application 
processor introduces fundamental engineering challenges.

The auxiliary **XuanTie E907 RISC-V core** on the Allwinner T527 solves these 
challenges through asymmetric multiprocessing (AMP):

### 3.1 Deterministic Timing & Real-Time Control Loops
* **The DRAM Bottleneck**: The ARM Cortex-A55 cluster executes out of external LPDDR4/4X dynamic RAM (`0x40000000`). Even with the Linux `PREEMPT_RT` patchset, DRAM access is inherently non-deterministic. Periodic row refreshes (`tRFC`), memory controller arbitration among 8 CPU cores, GPU, NPU, ISP, and DMA engines, and cache-line refills introduce latency spikes from hundreds of nanoseconds to several milliseconds.
* **Zero-Wait-State SRAM**: The XuanTie E907 executes out of dedicated on-chip SRAM (SRAM Space 0 at `0x3FFC0000`, SRAM Space 1 at `0x40000000`) with fixed single-cycle, zero-wait-state access across 512 KB continuous memory. Instruction execution times and memory latency are 100% deterministic.
* **Hard Real-Time Loops**: Applications such as drone flight controllers, gimbal stabilization, and motor control (FOC) require strict periodic execution at 8–50 kHz with sub-microsecond jitter. The E907 features a dedicated RISC-V PLIC, hardware single-precision FPU (`F`), and 32 integer registers, enabling it to service high-rate sensor interrupts (SPI IMU `DRDY` signals) with instantaneous, deterministic response.

### 3.2 CPU Offload, Fault Isolation & Power
* **Offload Linux Cores**: Servicing ultra-high-frequency interrupts on the ARM host burns CPU cycles in context switching, kernel transitions, and cache thrashing. Offloading to the co-processor frees the Cortex-A55 cluster for NPU inference, video streaming, ROS2 nodes, and flight log storage.
* **Fault Containment**: The E907 resides in an independent power, clock, and reset domain. If Linux panics, OOMs, or undergoes an OTA update, the RISC-V core continues running—maintaining actuator currents, triggering emergency shutdowns, or signaling via GPIOs and CAN-FD.
* **Low-Power Standby**: The eight ARM Cortex-A55 cores at 1.8 GHz consume several watts. The E907 can remain active at low power while Linux sleeps, waking the host via inter-core interrupt when a trigger condition is met.

---

## 4. Silicon Architecture, Naming & Board Comparison

When navigating Allwinner documentation and Linux kernel sources, naming 
conventions across document revisions can be confusing:

```text
                                +---> Allwinner T527 (Industrial SBC — Radxa Cubie A5E)
                                |
sun55i Generation (Same Die IP) +---> Allwinner A527 (Commercial SBC)
                                |
                                +---> Allwinner A523 (Tablet / OTT Platform)
```

* **Same Silicon Core**: The **T527** (industrial grade) and **A527** (commercial grade) share the exact same internal silicon die, bus topology, and MCU memory map as the **A523**.
* **Kernel Codename (`sun55i`)**: In upstream Linux and U-Boot, this generation is codenamed **`sun55i`**. The board device tree (`sun55i-a527-cubie-a5e.dts`) includes the base `sun55i-a523.dtsi`, and the clock driver is `ccu-sun55i-a523-mcu.c`.
* **Dedicated RISC-V RemoteProc Architecture**: While the physical T527 die includes an auxiliary audio DSP block, our Linux RemoteProc implementation (`sunxi_rproc.c`) strictly focuses on the **XuanTie E907 RISC-V** co-processor following upstream kernel subsystem separation guidelines (see [DSP Decoupling Rationale](../architecture/dsp_decoupling_rationale.md)).
* **Sibling Generation (`sun60i` / A733)**: The **Allwinner A733** (powering the **Radxa Cubie A7A**) belongs to the newer `sun60i` big.LITTLE generation (2x Cortex-A76 + 6x Cortex-A55). While its main peripheral space is relocated, its auxiliary MCU subsystem reuses a **XuanTie RISC-V core** (E902) executing out of SRAM A2 and adheres to the identical `remoteproc` driver model.

### 4.1 Board Hardware Comparison

#### Radxa Cubie A5E (Allwinner T527 / A527, `sun55i`)
* **Application Processor**: 8× ARM Cortex-A55 @ 1.8 GHz
* **Auxiliary Real-Time Core**: XuanTie E907 (RV32IMAFCX @ 200 MHz, 32 GPRs, Hardware Single FPU)
* **Audio DSP**: Decoupled (Not managed by `sunxi_rproc.c`)
* **Fast On-Chip Memory**: 512 KB Continuous SRAM (`0x3FFC0000`–`0x40040000`)
* **Hardware Reset Vector**: `STA_ADD_REG` defaults to `0x3FFC0000`
* **Hardware Mailbox**: 8-channel bi-directional MSGBOX (`0x03003000`)
* **Linux Driver Framework**: `sunxi_rproc.c` (Linux RemoteProc)

#### Radxa Cubie A7A (Allwinner A733, `sun60i`)
* **Application Processor**: 2× ARM Cortex-A76 @ 2.0 GHz + 6× Cortex-A55
* **Auxiliary Real-Time Core**: XuanTie E902 (RV32EMC @ 200 MHz, 16 GPRs, No FPU)
* **Audio DSP**: None
* **Fast On-Chip Memory**: 208 KB Shared SRAM A2 (`0x00040000`)
* **Hardware Reset Vector**: Hardwired Reset Vector @ `0x00040000`
* **Hardware Mailbox**: 8-channel bi-directional MSGBOX (`0x03003000`)
* **Linux Driver Framework**: `sunxi_rproc.c` (Linux RemoteProc)

---

## 5. T527 Reference Manual Mapping & Silicon Reality

All hardware register offsets, memory windows, and control blocks referenced here 
are derived directly from the official **Allwinner T527 User Manual V0.92** and 
verified on live silicon:

### 5.1 Where to Find This Information in the T527 User Manual
* **Chapter 2: System Address Map & Memory Mapping (Section 2.1, Table 2-1)**:
  * `Dedicated MCU SRAM / SRAM A3 Space 0`: `0x07280000 – 0x072BFFFF` (256 KB)
  * `Dedicated MCU SRAM / SRAM A3 Space 1`: `0x072C0000 – 0x072FFFFF` (256 KB)
  * `Hardware MSGBOX`: `0x03003000 – 0x03003FFF` (4 KB)
  * `RTC`: `0x07090000 – 0x070903FF` (1 KB)
  * `System DRAM`: `0x40000000` base
  * *Silicon Confirmation*: Address `0x07090000` is explicitly documented as the **RTC (Real-Time Clock)** register block. The T527 User Manual contains no memory-mapped Debug Module (DM/DMI) entry anywhere in the system bus interconnect tables.
* **Chapter on MCU Subsystem & RISC-V Configuration (`RISCV_CFG` @ `0x07130000`)**:
  * `0x0000` (`VER_REG`): Version Register (`0x00010000` = v1.0).
  * `0x0204` (`STA_ADD_REG`): **Start Vector / Boot Address Register**. Defines initial program counter address fetched upon reset deassertion. In silicon, **its factory default value is `0x3FFC0000`**.
  * `0x0248` (`WORK_MODE_REG`): Work Mode Register. Bit 3 (`BIT_LOCK_STA`) indicates hardware core lockup status (0 = Running normally, 1 = Core lockup).
* **Chapter on MCU Clock Control Unit (`MCU_CCU` @ `0x07102000`)**:
  * `0x07102120` (`MCU_CLK_REG`): XuanTie E907 core clock gating and divider selection.
  * `0x07102124` (`MCU_RST_REG`): XuanTie E907 reset control (Bit 16: CFG reset, Bit 17: DBG reset, Bit 18: Core Run reset).
* **Chapter on Hardware Message Box (`CPUX_MSGBOX` @ `0x03003000` / `RISCV_MSGBOX` @ `0x07136000`)**:
  * 8-channel bi-directional hardware FIFO doorbells connecting ARM64 GIC SPI interrupts and RISC-V PLIC interrupts.

### 5.2 Verified Hardware Memory Windows (Allwinner T527 / A523)

The authoritative memory mapping registered in the Linux RemoteProc driver 
(`sunxi_rproc.c` / `sun55i-a523.dtsi`) is structured as follows:

* **SRAM Space 0 (`r_sram`)**: Host `0x07280000` -> Core `0x3FFC0000` (256 KB)
  * *Role*: Primary Boot & Execution pool (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`). Hardcoded hardware reset entry vector. Zero wait states.
* **SRAM Space 1 (`r_sram1`)**: Host `0x072C0000` -> Core `0x40000000` (256 KB)
  * *Role*: Secondary High-Speed SRAM Bank for shared IPC buffers and stack extension. Zero wait states.
* **RISC-V CFG Control Block (`cfg`)**: Host `0x07130000` -> Core `0x07130000` (4 KB)
  * *Role*: Hardware MMIO registers: Version (`0x0000`), Boot Entry Vector `STA_ADD_REG` (`0x0204`, defaults to `0x3FFC0000`), and Work Mode / Lockup Status `WORK_MODE_REG` (`0x0248`).
* **MCU CCU Clocks & Resets**: Host `0x07102000` -> Core `0x07102000` (4 KB)
  * *Role*: Clock gates (`0x07102120`), resets (`0x07102124`: bit 16 CFG, bit 17 DBG, bit 18 CORE).
* **Hardware MSGBOX**: Host `0x03003000` -> Core `0x03003000` (4 KB)
  * *Role*: 8-channel bi-directional doorbell FIFO. Port 2 (Ch 8/9) connects Host ARM & E907 RISC-V.
* **RemoteProc Trace Buffer (`trace0`)**: Host `0x07285A30`+ -> Core `0x3FFC5A30`+ (4 KB)
  * *Role*: RemoteProc debugfs trace buffer (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`). Mapped inside SRAM Space 0 (`.trace_buffer`).
* **Main AP Peripheral Space**: Host `0x02000000`+ -> Core `0x02000000`+
  * *Role*: 1:1 mapped application peripherals: PIO GPIO controller (`0x02000000`), Linux UART0 debug console (`0x02500000`), UART2 navigation port (`0x02500800`), SPI0 (`0x04025000`).
* **CPUS Always-On Peripheral Space**: Host `0x07000000`+ -> Core `0x07000000`+
  * *Role*: 1:1 mapped co-processor peripherals: dedicated `S_UART0` serial console (`0x07080000`, 115200 baud), `R_PIO` GPIO (`0x07022000`), `R_TIMER`, `R_PWM`.

### 5.3 Address Translation Overview

```text
  LINUX HOST (ARM64) PHYSICAL VIEW                  XUANTIE E907 RISC-V CORE VIEW
  ================================                  =============================
  0x07280000 - 0x072BFFFF [ 256 KB ] -------------> 0x3FFC0000 - 0x3FFFFFFF (SRAM Space 0)
    (Device Tree: "r_sram", Base Reset Entry)         (Primary Boot, .vectors, .text, stack)

  0x072C0000 - 0x072FFFFF [ 256 KB ] -------------> 0x40000000 - 0x4003FFFF (SRAM Space 1)
    (Device Tree: "r_sram1")                          (Secondary High-Speed SRAM Bank)

  0x07130000 - 0x07130FFF [   4 KB ] -------------> 0x07130000 - 0x07130FFF (CFG Regs)
    (Device Tree: "cfg", STA_ADD_REG 0x204)           (Boot Vector: 0x3FFC0000)

  0x03003000 - 0x03003FFF [   4 KB ] -------------> 0x03003000 - 0x03003FFF (MSGBOX)
    (Port 2 Ch 8/9: Host <-> E907)                    (Port 2: RISC-V Local Mailbox)

  0x48000000 - 0x480FFFFF [   1 MB ] -------------> 0x48000000 - 0x480FFFFF (DDR DMA Pool)
    (Reserved VirtIO RPMsg Pool)                      (vrings & Streaming Payloads)
```

---

## 6. Memory Architecture & Boot Mechanics: SRAM & Startup Sequence

On the Allwinner T527, Linux RemoteProc and the XuanTie E907 co-processor communicate 
and boot through dedicated on-chip SRAM:

### 6.1 The Boot Reality: RemoteProc Boots Directly from SRAM Space 0 (`0x3FFC0000`)

> **Caution (The `0x40000000` vs `0x3FFC0000` Boot Gotcha):**  
> Early vendor BSPs and community bring-up attempts frequently locked up because code attempted to boot the E907 at address `0x40000000`. On the T527, `0x40000000` is the base of DRAM (and secondary SRAM Space 1). However, the silicon reset vector and default `STA_ADD_REG` (`0x07130204`) strictly point to **SRAM Space 0 at `0x3FFC0000`**. Firmware must be linked to ORIGIN `0x3FFC0000`.

1. **Device Tree Bindings (`sun55i-a523.dtsi`)**:
   During driver probe, `sunxi_rproc.c` binds to the registered hardware blocks:
   ```dts
   rproc: remoteproc@7130000 {
       compatible = "allwinner,sun55i-a523-rproc",
                    "allwinner,sun55i-a527-rproc";
       reg = <0x07130000 0x1000>,
             <0x07280000 0x40000>,
             <0x072C0000 0x40000>,
             <0x07010364 0x4>;
       reg-names = "cfg", "r_sram", "r_sram1", "remap";
       clocks = <&mcu_ccu CLK_BUS_MCU_RISCV_CFG>,
                <&mcu_ccu CLK_MCU_RISCV>,
                <&mcu_ccu CLK_BUS_MCU_PUBSRAM>,
                <&mcu_ccu CLK_BUS_MCU_RISCV_MSGBOX>;
       clock-names = "bus", "core", "sram", "msgbox";
       resets = <&mcu_ccu RST_BUS_MCU_RISCV_CFG>,
                <&mcu_ccu RST_BUS_MCU_RISCV_CORE>,
                <&mcu_ccu RST_BUS_MCU_PUBSRAM>,
                <&mcu_ccu RST_BUS_MCU_RISCV_MSGBOX>;
       reset-names = "cfg", "core", "sram", "msgbox";
       mboxes = <&msgbox 8>, <&msgbox 9>;
       mbox-names = "rx", "tx";
       status = "disabled";
   };
   ```

2. **Loading Firmware into SRAM Space 0**:
   The primary co-processor firmware (`e907_sram.ld`) is linked to execute out of **SRAM Space 0 (`0x3FFC0000`)**:
   ```ld
   MEMORY {
       SRAM (rwx) : ORIGIN = 0x3FFC0000, LENGTH = 256K
   }
   ```
   When Linux loads `firmware.elf`, `sunxi_rproc_da_to_va()` translates device addresses in the range `0x3FFC0000`–`0x3FFFFFFF` directly to the mapped host virtual address of `r_sram` and copies `.vectors`, `.text`, and `.data` via `memcpy_toio()`.

3. **Starting the Core via `STA_ADD_REG`**:
   When starting the co-processor (`echo start > /sys/class/remoteproc/remoteproc0/state`):
   * `sunxi_rproc_start()` retrieves the ELF entry point (`rproc->bootaddr`), which is **`0x3FFC0000`** (`_vectors`).
   * The driver programs this address into the hardware boot vector register `STA_ADD_REG` (`0x07130204`).
   * The driver deasserts the core run reset (`rst_core`, bit 18 in `MCU_RST_REG 0x07102124`).
   * The XuanTie E907 begins execution immediately from `0x3FFC0000` in SRAM Space 0.

---

### 6.2 Bootstrap Sequence & FPU Initialization (`startup.S`)

When the core deasserts reset at `0x3FFC0000`, execution starts in `startup.S`:

```assembly
.section .vectors, "ax"
.global _vectors
_vectors:
    j reset_handler
    /* Trap and interrupt vector table entries... */

.section .text.startup, "ax"
.global reset_handler
reset_handler:
    /* 1. Install direct Machine Trap Vector */
    la t0, default_trap_entry
    csrw mtvec, t0

    /* 2. Mask all external/timer interrupts during initialization */
    csrw mie, zero

    /* 3. Diagnostic proof marker in SRAM */
    li t0, 0xDEADBEEF
    la t1, __sram_c_start
    sw t0, 0(t1)

    /* 4. Initialize 16-byte ABI-aligned Stack Pointer */
    la sp, _stack_top

    /* 5. Initialize Global Pointer for relaxed addressing */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* 6. Enable Hardware Single-Precision FPU (CRITICAL STEP) */
    li t0, (1 << 13) | (1 << 14)  /* mstatus.FS = 0b11 (Dirty/Initial) */
    csrs mstatus, t0
    csrw fcsr, zero               /* Clear accrued exceptions & set round-to-nearest */

    /* 7. Zero BSS Segment */
    la t0, _sbss
    la t1, _ebss
1:
    bge t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    /* 8. Call C++ Static Constructors */
    call __libc_init_array

    /* 9. Jump to Main Application */
    call main

    /* 10. Trap on main exit */
3:  wfi
    j 3b
```

> **Important (The `mstatus.FS` Trap Gotcha):**  
> At hardware reset, RISC-V `mstatus.FS` is `00` (**Off**). Attempting to execute any floating-point instruction (`flw`, `fsw`, `fadd.s`, `fmul.s`) or access `fcsr` while `FS == 00` immediately triggers an **Illegal Instruction Exception** (`mcause = 2`). Setting `mstatus.FS = 0b11` in `startup.S` enables the hardware Single-Precision FPU.

---

## 7. Debugging Realities: What Works on T527 Silicon (and What Doesn't)

> **Bottom Line First:** The Allwinner T527 does **not** expose a memory-mapped RISC-V Debug Module Interface (DMI) on its non-secure bus interconnect. Target-hosted OpenOCD cannot attach directly over `/dev/mem`. This section explains what that means, what platforms do have it, and what works today on T527.

### 7.1 What Memory-Mapped Debug Access Looks Like (And Why T527 Doesn't Have It)

Modern heterogeneous SoCs—such as the **Texas Instruments AM62x / AM64x (K3)**, **STMicroelectronics STM32MP1 / STM32MP2**, and **NXP i.MX8M**—implement **Direct Memory-Mapped Debug Access (DMEM)**. In this architecture, the auxiliary core's RISC-V Debug Module registers are exposed directly to the ARM application processor's bus, enabling target-hosted OpenOCD + GDB over SSH without any physical probe:

```text
Development Workstation
  riscv-none-elf-gdb <firmware.elf>
  (gdb) target remote board:3333
        |
        | GDB Remote Serial Protocol (RSP)
        v
  OpenOCD on ARM64 Linux host
  (reads RISC-V DMI registers via /dev/mem MMIO)
        |
        | Internal SoC Bus (AXI/AHB)
        v
  RISC-V Debug Module Interface (DMI registers)
  e.g. TI AM62x: mapped into host physical address space
```

On the **Allwinner T527**, the XuanTie E907 Debug Module is **not routed** to the non-secure ARM bus interconnect. The address `0x07090000` often speculated about in community discussions is the **RTC register block** per the T527 User Manual—not a DMI window.

### 7.2 What Works Today: Four Practical Debug Strategies

For XuanTie E907 firmware development on current T527 hardware, these four strategies provide reliable, production-grade diagnostics:

1. **Linux RemoteProc Trace Buffers (`trace0`)** *(Primary)*: The `.resource_table` in firmware declares a `RSC_TRACE` entry backed by a circular ring buffer in SRAM Space 0 (`0x3FFC0000`). Linux maps it and exposes a live streaming interface:
   ```bash
   cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
   ```
   Zero-overhead, no extra hardware required. Used by all `firmware/e907-riscv/apps` test applications.

2. **Dedicated Hardware UART (`S_UART0` @ `0x07080000`)**: A RISC-V-owned independent serial port in the CPUS Always-On domain at 115200 baud. Provides immediate low-level boot diagnostics completely separate from the Linux console UART (`UART0` @ `0x02500000`).

3. **Lock-Free Shared SRAM Ring Buffers**: High-speed SPSC telemetry buffers in SRAM Space 0 (`0x3FFC0000`) or Space 1 (`0x40000000`), read from Linux via `/dev/mem` or UIO.

4. **External Hardware JTAG Probes**: Connect a T-Head CK-Link or SEGGER J-Link to the physical JTAG test pads on the board for instruction-level single-stepping and hardware watchpoints.

---

## 8. Summary & What's Next in Part 2

In this introductory article, we established:
1. **Bill of Materials & Prerequisites**: Target hardware, dual UART serial diagnostics, and toolchain setup.
2. **Why use the RISC-V co-processor**: Deterministic SRAM execution (bypassing DRAM refresh jitter), offloading Linux CPU cycles, executing 10–50 kHz hard real-time control loops, and maintaining fault isolation.
3. **Silicon architecture & TRM mapping**: Navigating `sun55i-a523` naming, board differences between Cubie A5E and A7A, architectural isolation of the RISC-V co-processor, and exact register locations in the *Allwinner T527 User Manual V0.92*.
4. **Memory architecture & boot mechanics**: How Linux RemoteProc (`sunxi_rproc.c`) loads firmware into SRAM Space 0 (`0x3FFC0000`), programs `STA_ADD_REG` to boot directly from SRAM, initializes the single-precision FPU (`mstatus.FS = 0b11`), and manages the 16 KB aligned stack.
5. **Debugging realities**: Dispelling the `0x07090000` DMI myth, comparing with TI AM62x / STM32MP1, and detailing the four practical debugging techniques available today.

In **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](part2_building_remoteproc_and_hardware_proof.md)**, we move from architecture to software implementation:
* Authoring the **Linux 7.1 `sunxi_rproc.c` RemoteProc kernel driver**.
* Configuring multi-segment ELF placement (SRAM Space 0, SRAM Space 1, and DDR carveouts) and built-in debugfs trace logging.
* Proving hardware state transitions using the all-new `firmware/e907-riscv/apps` verification suite (`testBasic`, `testStringBinaryTrace0`, `testCrash`, `testPing`, `testPingRpmsg`, `testDRAMMsg`).

---

### Series Navigation
* **Part 1: Architecture and Boot Mechanics** *(You are here)*
* **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](part2_building_remoteproc_and_hardware_proof.md)**
* **[Part 3: Inter-Processor Communication (IPC) Deep Dive](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**
* **[Part 4: Deep Dive into Bare-Metal C++ Coroutines](part4_deep_dive_baremetal_cpp_coroutines.md)**
