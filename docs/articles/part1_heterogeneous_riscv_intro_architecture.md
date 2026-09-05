# Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Part 1): Architecture and Memory-Mapped Debugging

Heterogeneous multi-core SoCs—pairing high-performance 64-bit ARM Cortex-A application cores with low-power, deterministic auxiliary microcontrollers—have become the standard architecture for modern embedded systems. Silicon like the **Allwinner T527 / A527** (featured on the **Radxa Cubie A5E**) integrates an octa-core ARM Cortex-A55 cluster alongside an auxiliary **XuanTie E907 RISC-V core** (RV32IMAFDC @ 200 MHz) and a **Cadence Tensilica HiFi4 Audio DSP** (@ 600 MHz).

Getting these co-processors online requires establishing reliable hardware lifecycle control, clock tree synchronization, and deterministic memory placement before loading production firmware.

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

This article is **Part 1 of a series** documenting the practical bring-up of the XuanTie E907 RISC-V co-processor on Linux:
* **Part 1 (This Article)**: Why use the RISC-V co-processor, TRM memory maps, ITCM/DTCM architecture and two-stage bootstrapping, the RemoteProc driver model, and on-chip memory-mapped debugging.
* **Part 2**: Authoring the Linux `remoteproc` kernel driver, multi-segment ELF placement, and proving hardware state with automated Python DMI scripts over OpenOCD.
* **Part 3**: General embedded firmware development, TCM vs. DRAM memory determinism, lightweight lock-free IPC (libmetal), live GDB workflows, and an introduction to bare-metal C++ coroutines.
* **Part 4**: Deep dive into C++20 coroutines on bare-metal RISC-V—benchmarks, memory profiles, and comparison against RTOS task switching.

---

## 1. Why Use the XuanTie RISC-V Co-Processor?

Modern embedded Linux platforms excel at complex workloads—networking, file systems, multimedia pipelines, computer vision, and machine learning. However, running jitter-sensitive, hard real-time control tasks directly on an application processor introduces fundamental engineering challenges.

The auxiliary **XuanTie E907 RISC-V core** on the Allwinner T527 solves these challenges through asymmetric multiprocessing (AMP):

### 1.1 Deterministic Timing: SRAM vs. External DRAM
* **The DRAM Bottleneck**: The ARM Cortex-A55 cluster executes operating system workloads out of external LPDDR4/4X dynamic RAM (`0x40000000`). Even with the Linux `PREEMPT_RT` patchset, DRAM access is non-deterministic. Periodic DRAM row refreshes ($t_{\text{RFC}}$), memory controller queue arbitration among 8 CPU cores, GPU, NPU, ISP, and DMA engines, and cache-line refills introduce latency spikes ranging from hundreds of nanoseconds to several milliseconds.
* **Zero-Wait-State SRAM Execution**: The XuanTie E907 executes out of internal fast SRAM (PubSRAM C at `0x00020000` and Dedicated MCU SRAM at `0x3FFC0000`) and private Tightly-Coupled Memory (ITCM/DTCM). On-chip SRAM delivers fixed, single-cycle, zero-wait-state memory access with zero refresh interruptions. Instruction execution times and memory latency are 100% deterministic.

### 1.2 Offloading the Linux Application Cores
* Linux is an operating system optimized for throughput, multitasking, and rich application ecosystems. Servicing ultra-high-frequency interrupts directly on the host consumes significant CPU cycles in context switching, kernel transitions, and cache thrashing.
* Offloading continuous, high-rate real-time tasks to the co-processor frees up the octa-core Cortex-A55 cluster to focus entirely on compute-heavy tasks: running neural network models on the VIP9000 NPU, streaming high-definition video, serving web dashboards, managing ROS2 nodes, and writing black-box flight logs to NVMe or eMMC storage.

### 1.3 Microsecond-Scale Hard Real-Time Control Loops (10 kHz – 50 kHz)
* In applications such as drone flight controllers, robotic gimbal stabilization, and motor control (Field-Oriented Control / FOC), control loops must execute at strict periodic intervals (e.g., 8 kHz to 32 kHz) with sub-microsecond jitter.
* The XuanTie E907 features a dedicated RISC-V Platform-Level Interrupt Controller (PLIC), hardware single- and double-precision floating-point units (FPU), and 32 integer registers, allowing it to service high-rate sensor interrupts (such as SPI IMU `DRDY` signals) with instantaneous, deterministic response times.

### 1.4 Hardware Fault Containment and System Safety
* The XuanTie E907 resides in an independent power, clock, and reset domain.
* If the Linux application environment encounters an out-of-memory (OOM) panic, locks up under an extreme compute spike, or undergoes an in-field Over-the-Air (OTA) kernel update, the RISC-V co-processor continues running uninterrupted. It can maintain actuator holding currents, execute graceful emergency shutdowns, deploy recovery chutes, or signal emergency status via dedicated GPIOs and CAN-FD buses.

### 1.5 Ultra-Low-Power Standby and Always-On Monitoring
* Running eight ARM Cortex-A55 cores at 1.8 GHz consumes several watts of power. In battery-powered edge installations, the Linux application cores can enter deep sleep states while the XuanTie E907 remains clocked at low power, polling sensors or monitoring communication channels. When a designated trigger condition is met, the co-processor wakes the Linux host via an inter-core interrupt.

---

## 2. Silicon Architecture, Naming & Board Comparison

When navigating Allwinner documentation and Linux kernel sources, naming conventions across document revisions can be confusing:

```text
                                ┌─────────────► Allwinner T527 (Industrial SBC — Radxa Cubie A5E)
                                │
sun55i Generation (Same Die IP) ┼─────────────► Allwinner A527 (Commercial SBC)
                                │
                                └─────────────► Allwinner A523 (Tablet / OTT Platform)
```

* **Same Silicon Core**: The **T527** (industrial grade) and **A527** (commercial grade) share the exact same internal silicon die, bus topology, and MCU memory map as the **A523**.
* **Kernel Codename (`sun55i`)**: In upstream Linux and U-Boot, this generation is codenamed **`sun55i`**. The board device tree (`sun55i-a527-cubie-a5e.dts`) includes the base `sun55i-a523.dtsi`, and the clock driver is `ccu-sun55i-a523-mcu.c`.
* **Sibling Generation (`sun60i` / A733)**: The **Allwinner A733** (powering the **Radxa Cubie A7A**) belongs to the newer `sun60i` big.LITTLE generation (2x Cortex-A76 + 6x Cortex-A55). While its main peripheral space is relocated, its auxiliary MCU subsystem reuses a **XuanTie RISC-V core** (E902) executing out of SRAM A2 and adheres to the identical `remoteproc` driver model.

### Board Hardware Comparison: Radxa Cubie A5E vs. Cubie A7A

| Feature | Radxa Cubie A5E | Radxa Cubie A7A |
| :--- | :--- | :--- |
| **SoC** | **Allwinner T527 / A527** (`sun55i`) | **Allwinner A733** (`sun60i`) |
| **Application Cores** | 8x ARM Cortex-A55 @ 1.8 GHz | 2x ARM Cortex-A76 @ 2.0 GHz + 6x Cortex-A55 |
| **Auxiliary Real-Time Core** | **XuanTie E907 RISC-V** (RV32IMAFDC @ 200 MHz) | **XuanTie E902 RISC-V** (RV32EMC @ 200 MHz) |
| **Fast On-Chip SRAM** | 128 KB Shared PubSRAM C (`0x00020000`) + 256 KB Dedicated MCU SRAM (`0x3FFC0000` Core / `0x07280000` Host) | 208 KB Shared SRAM A2 (`0x00040000`) |
| **Boot & Control Registers** | 4 KB E907 CFG (`0x07130000`, `STA_ADD_REG` @ `0x0204`) | PRCM `r_ccu` Reset Control (Hardwired Reset Vector @ `0x00040000`) |
| **Hardware Mailbox** | 8-channel MSGBOX (`0x03003000`) | 8-channel MSGBOX (`0x03003000`) |
| **Debug Module Architecture** | External JTAG / RemoteProc `trace0` (No on-chip MMIO DMI) | External JTAG / RemoteProc `trace0` (No on-chip MMIO DMI) |
| **Linux Driver Model** | `sunxi_rproc.c` (`remoteproc`) | `sunxi_rproc.c` (`remoteproc`) |

---

## 3. T527 Reference Manual Mapping & Silicon Reality

All hardware register offsets, memory windows, and control blocks referenced here are derived directly from the official **Allwinner T527 User Manual V0.92** and verified on live silicon.

### 3.1 Where to Find This Information in the T527 User Manual
* **Chapter 2: System Address Map & Memory Mapping (Section 2.1, Table 2-1)**:
  - Documents the top-level memory map: `PubSRAM C` (`0x00020000`, 128 KB), `Dedicated MCU SRAM / SRAM A3` (`0x07280000`, 256 KB), `Hardware MSGBOX` (`0x03003000`), `RTC` (`0x07090000`), and `DRAM` (`0x40000000`).
  - *Silicon Confirmation*: Notice that address `0x07090000` is explicitly documented as the **RTC (Real-Time Clock)** register block. The T527 User Manual contains no memory-mapped Debug Module (DM/DMI) entry anywhere in the system bus interconnect tables.
* **Chapter on MCU Subsystem & RISC-V Configuration (`RISCV_CFG` @ `0x07130000`)**:
  - `0x0000` (`VER_REG`): Version Register.
  - `0x0204` (`STA_ADD_REG`): **Start Vector / Boot Address Register**. Defines the initial program counter address fetched by the E907 core upon reset deassertion.
  - `0x0248` (`WORK_MODE_REG`): Work Mode Register. Bit 3 (`BIT_LOCK_STA`) indicates hardware core lockup status if the processor attempts an invalid bus transaction.
* **Chapter on MCU Clock Control Unit (`MCU_CCU` @ `0x07102000`)**:
  - `0x07102120` (`MCU_CLK_REG`): XuanTie E907 core clock gating and divider selection.
  - `0x07102124` (`MCU_RST_REG`): XuanTie E907 reset control (Bit 16: CFG reset, Bit 17: DBG reset, Bit 18: Core Run reset).
  - `0x07102114` (`SRAM_CLK_REG` / `SRAM_RST_REG`): PubSRAM C clock gating and bus reset release.
* **Chapter on Hardware Message Box (`CPUX_MSGBOX` @ `0x03003000` / `RISCV_MSGBOX` @ `0x07136000`)**:
  - 8-channel bi-directional hardware FIFO doorbells connecting ARM64 GIC SPI 147 and RISC-V PLIC IRQ 25.

### 3.2 Verified Hardware Memory Windows (Allwinner T527 / A523)

The authoritative memory mapping registered in the Linux RemoteProc driver (`sunxi_rproc.c` / `sun55i-a523.dtsi`) is structured as follows:

| Memory Region | Host ARM Physical Address | RISC-V E907 Core Address | Size | RemoteProc DTS Binding | Description & Hardware Role |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Shared PubSRAM C** | **`0x00020000`** | **`0x00020000`** | **128 KB** | `reg-names = "sram"` | Primary boot and execution window (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`). 1:1 identity mapped on both interconnects. |
| **Dedicated MCU SRAM (`r_sram`)** | **`0x07280000`** | **`0x3FFC0000`** | **256 KB** | `reg-names = "r_sram"` | Zero-wait-state dedicated MCU SRAM space 0. High-performance memory for real-time control loops and high-throughput SPSC ring buffers. |
| **RISC-V CFG Control Block** | **`0x07130000`** | **`0x07130000`** | **4 KB** | `reg-names = "cfg"` | Hardware MMIO registers: Version (`0x0000`), Boot Entry Vector `STA_ADD_REG` (`0x0204`), and Work Mode / Lockup Status `WORK_MODE_REG` (`0x0248`). Not general SRAM. |
| **MCU CCU Clocks & Resets** | **`0x07102000`** / `0x07010000` | **`0x07102000`** / `0x07010000` | **64 KB** | `clocks = <&mcu_ccu ...>` | Clock gate (`0x07102120`), resets (`0x07102124`: bit 16 CFG, bit 17 DBG, bit 18 CORE), and PubSRAM clock/reset (`0x07102114`). |
| **Hardware MSGBOX** | **`0x03003000`** | **`0x03003000`** | **4 KB** | `mboxes = <&msgbox 0>, <&msgbox 1>` | 8-channel bi-directional doorbell FIFO. Channel 0: RISC-V to Linux (GIC SPI 147); Channel 1: Linux to RISC-V (PLIC IRQ 25). |
| **DDR Trace Buffer (`trace0`)** | **`0x48000000`** | **`0x48000000`** | **4 KB** | `memory-region` (carveout) | RemoteProc debugfs trace buffer (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`). |
| **DDR DMA Payload Pool** | **`0x48100000`** | **`0x48100000`** | **1 MB** | `memory-region` (carveout) | Non-cacheable DDR DMA payload buffer pool for high-bandwidth IPC transfers. |
| **Main Peripheral Space** | **`0x02000000`+** | **`0x02000000`+** | — | Native SoC buses | 1:1 mapped peripherals: PIO GPIO controller (`0x02000000`), UART0 debug console (`0x02500000`), UART2 navigation port (`0x02500800`), SPI0 (`0x04025000`). |

### 3.3 Visual Address Translation Architecture

```text
+===================================================================================+
|                  ALLWINNER T527 / A523 MEMORY MAPPING ARCHITECTURE                |
+===================================================================================+

  LINUX HOST (ARM64) PHYSICAL VIEW                  XUANTIE E907 RISC-V CORE VIEW
  ================================                  =============================
  0x00020000 - 0x0003FFFF [ 128 KB ] ─────────────> 0x00020000 - 0x0003FFFF (PubSRAM C)
    (Device Tree: "sram")                             (Default Boot, .vectors, .text, stack)

  0x07280000 - 0x072BFFFF [ 256 KB ] ─────────────> 0x3FFC0000 - 0x3FFFFFFF (Dedicated SRAM)
    (Device Tree: "r_sram")                           (Zero-Wait-State High-SRAM Window)

  0x07130000 - 0x07130FFF [   4 KB ] ─────────────> 0x07130000 - 0x07130FFF (CFG Regs)
    (Device Tree: "cfg", STA_ADD_REG 0x204)           (Boot Vector & Lockup Status)

  0x03003000 - 0x03003FFF [   4 KB ] ─────────────> 0x03003000 - 0x03003FFF (MSGBOX)
    (ARM GIC IRQ 147)                                 (RISC-V PLIC IRQ 25)

  0x48000000 - 0x48000FFF [   4 KB ] ─────────────> 0x48000000 - 0x48000FFF (trace0)
    (RemoteProc Trace Carveout)                       (Debugfs Live Logging)

  0x48100000 - 0x481FFFFF [   1 MB ] ─────────────> 0x48100000 - 0x481FFFFF (DDR DMA Pool)
    (Reserved Memory Pool)                            (High-Bandwidth Telemetry Buffers)
+===================================================================================+
```

---

## 4. ITCM and DTCM Memory Architecture & Programmer's Implementation Guide

For embedded firmware engineers, understanding how **ITCM (Instruction Tightly-Coupled Memory)** and **DTCM (Data Tightly-Coupled Memory)** operate on the XuanTie E907 is critical for achieving deterministic, single-cycle execution.

### 4.1 Architectural Role: Why TCM is Essential
The XuanTie E907 features a modified Harvard bus architecture:
* **ITCM (64 KB @ `0x00000000`)**: Wired directly to the core's instruction fetch pipeline. Instruction fetches complete in **1 clock cycle with zero wait states**, completely bypassing the L1 instruction cache. This eliminates cache miss penalties, bus arbitration delays, and pipeline stalls.
  - *Ideal Use Cases*: Time-critical interrupt handlers (such as Mailbox doorbell ISRs and SPI DMA callbacks), machine-mode trap handlers (`mtvec`), and inner PID flight stabilization loops.
* **DTCM (64 KB @ `0x00080000`)**: Wired directly to the core's load/store execution unit. Reads and writes complete in **1 clock cycle with zero wait states**, bypassing the L1 data cache.
  - *Ideal Use Cases*: Fast call stack (`.stack`), lookup tables (LUTs), critical state machines, and circular ring buffer head/tail pointers.

### 4.2 The Hardware Challenge: Why RemoteProc Cannot Load TCM Directly
On the Allwinner A523/T527 SoC:
1. **Private Core Bus Isolation**: ITCM and DTCM reside exclusively on the **private internal core bus** of the XuanTie E907.
2. **No Host Slave Bridge**: There is no active external bus slave port allowing the ARM Cortex-A55 cores or the Linux RemoteProc loader to write directly into ITCM or DTCM while the co-processor is held in reset. Legacy documentation suggesting host addresses at `0x07110000` or `0x07120000` does not correspond to functional writable memory on A523/T527 silicon.
3. **Hardware Lockup on Boot**: If you configure the boot entry register `STA_ADD_REG` (`0x07130204`) directly to `0x00000000` and deassert reset, the core attempts to fetch instructions from uninitialized TCM. This causes an immediate instruction fetch abort and forces the XuanTie core into **Hardware Lockup** (`WORK_MODE_REG 0x07130248` Bit 3 `BIT_LOCK_STA = 1`).

### 4.3 The Solution: Two-Stage Bootstrapping (LMA vs. VMA Staging)
To utilize ITCM and DTCM on Allwinner T527 without hardware lockup, firmware uses the classic embedded **Load Memory Address (LMA) vs. Virtual/Execution Memory Address (VMA)** staging pattern:

1. **Host Loading (LMA - Load Memory Address)**: Linux RemoteProc loads the compiled firmware ELF directly into accessible on-chip SRAM:
   - **PubSRAM C (`0x00020000`, 128 KB)**, OR
   - **Dedicated High SRAM (`0x3FFC0000`, 256 KB)**.
2. **Core Startup**: The E907 starts executing from SRAM (`_start` in `startup.S`).
3. **Core-Initiated Copy (VMA - Virtual/Execution Memory Address)**: Early in the startup assembly sequence, the E907's own instructions copy designated `.itcm` functions and `.dtcm` data from SRAM (LMA) into local TCM (VMA).
4. **Instruction Pipeline Synchronization (`fence.i`)**: The core executes `fence.i` to invalidate and synchronize its instruction fetch pipeline so that newly copied instructions in ITCM are fetched cleanly.
5. **Deterministic Execution**: The core jumps into its TCM-resident critical routines, executing with true 1-cycle latency.

### 4.4 Linker Script Configuration (`firmware.ld`)
To stage TCM code and data, configure the GNU Linker Script with distinct Load Memory Addresses using the `AT(...)` keyword:

```ld
MEMORY
{
    /* Staging / Primary Executable SRAM (Accessible by Linux RemoteProc) */
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K

    /* Tightly-Coupled Memories (Private Local E907 Core View) */
    ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx) : ORIGIN = 0x00080000, LENGTH = 64K
}

SECTIONS
{
    /* Primary bootstrap vectors and startup in SRAM */
    .vectors : { KEEP(*(.vectors)) } > SRAM
    .text    : { *(.text) *(.text.*) } > SRAM
    .rodata  : { *(.rodata) *(.rodata.*) } > SRAM

    /* Critical Real-Time Code: Stored in SRAM (LMA), Executed in ITCM (VMA) */
    .itcm_text : AT(_sidata_itcm)
    {
        . = ALIGN(4);
        _sitcm = .;
        *(.itcm)
        *(.itcm.*)
        *(.fast_code)
        . = ALIGN(4);
        _eitcm = .;
    } > ITCM
    _sidata_itcm = LOADADDR(.itcm_text);

    /* Critical Real-Time Data: Stored in SRAM (LMA), Executed in DTCM (VMA) */
    .dtcm_data : AT(_sidata_dtcm)
    {
        . = ALIGN(4);
        _sdtcm = .;
        *(.dtcm)
        *(.dtcm.*)
        *(.fast_data)
        . = ALIGN(4);
        _edtcm = .;
    } > DTCM
    _sidata_dtcm = LOADADDR(.dtcm_data);

    /* DTCM Uninitialized BSS */
    .dtcm_bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss_dtcm = .;
        *(.dtcm_bss)
        *(.dtcm_bss.*)
        . = ALIGN(4);
        _ebss_dtcm = .;
    } > DTCM
}
```

### 4.5 Assembly Startup Copy Routine (`startup.S`)
In `startup.S`, insert the copy routine before invoking C++ constructors or jumping to `main()`:

```assembly
    /* =============================================================
     * 1. Copy Critical Real-Time Code from SRAM (LMA) to ITCM (VMA)
     * ============================================================= */
    la      a0, _sitcm              /* Destination: ITCM start (0x00000000) */
    la      a1, _eitcm              /* Destination: ITCM end */
    la      a2, _sidata_itcm        /* Source: LMA located in PubSRAM C */
    beq     a0, a2, .Lcopy_itcm_done
.Lcopy_itcm_loop:
    bgeu    a0, a1, .Lcopy_itcm_done
    lw      t0, 0(a2)
    sw      t0, 0(a0)
    addi    a0, a0, 4
    addi    a2, a2, 4
    j       .Lcopy_itcm_loop
.Lcopy_itcm_done:
    fence.i                         /* Synchronize instruction pipeline & cache */

    /* =============================================================
     * 2. Copy Initialized Data from SRAM (LMA) to DTCM (VMA)
     * ============================================================= */
    la      a0, _sdtcm              /* Destination: DTCM start (0x00080000) */
    la      a1, _edtcm              /* Destination: DTCM end */
    la      a2, _sidata_dtcm        /* Source: LMA located in PubSRAM C */
    beq     a0, a2, .Lcopy_dtcm_done
.Lcopy_dtcm_loop:
    bgeu    a0, a1, .Lcopy_dtcm_done
    lw      t0, 0(a2)
    sw      t0, 0(a0)
    addi    a0, a0, 4
    addi    a2, a2, 4
    j       .Lcopy_dtcm_loop
.Lcopy_dtcm_done:
```

### 4.6 How Programmers Use TCM in C / C++
In application code, tag critical functions and variables with compiler section attributes:

```cpp
#define __ITCM_TEXT __attribute__((section(".itcm"), noinline))
#define __DTCM_DATA __attribute__((section(".dtcm")))
#define __DTCM_BSS  __attribute__((section(".dtcm_bss")))

// Critical attitude estimation loop running with 1-cycle latency in ITCM
void __ITCM_TEXT fast_flight_loop_isr(void) {
    // Process IMU measurements and update motor outputs
}

// Critical PID parameters stored with zero wait states in DTCM
static volatile float __DTCM_DATA pid_gains[3] = {1.25f, 0.05f, 0.12f};
static volatile uint32_t __DTCM_BSS fast_cycle_count;
```

### 4.7 The Zero-Copy Alternative: Dedicated MCU SRAM (`0x3FFC0000` / `0x07280000`)
If your application requires zero-wait-state performance across a large footprint without the startup overhead of copying sections from SRAM to TCM:
* Use **Dedicated MCU SRAM (`r_sram`)**.
* It is a **256 KB** continuous SRAM block mapped at host physical `0x07280000` and core local address **`0x3FFC0000`**.
* Linux RemoteProc can write to `0x07280000` directly while the core is in reset, allowing firmware to boot and execute with zero wait states directly out of SRAM Space 0 with no assembly staging required.

---

## 5. Direct Memory-Mapped Debug Access (DMEM): The Paradigm & The Hope for Future Silicon

### 5.1 The Soft-Wire JTAG Paradigm
In traditional microcontroller development, debugging requires:
* Soldering 1.27mm / 2.54mm JTAG headers to the board.
* Connecting external hardware debug probes (e.g., SEGGER J-Link, T-Head CK-Link, or FTDI adapters).
* Managing loose jumper wires, signal integrity issues, and ground loops.

To solve this, modern heterogeneous SoCs—such as the **Texas Instruments AM62x / AM64x (K3)**, **STMicroelectronics STM32MP1 / STM32MP2**, and **NXP i.MX8M**—implement **Direct Memory-Mapped Debug Access (DMEM)**. 

In this architecture, the SoC interconnect exposes the auxiliary core's debug registers (ARM CoreSight DAP or RISC-V Debug Module Interface) directly to the application processor's internal bus:

```text
┌─────────────────────────────────────────────────────────────┐
│                    GDB Debugger Host                        │
│   (riscv-none-elf-gdb / gdb set arch riscv:rv32)            │
└─────────────────────────────┬───────────────────────────────┘
                              │ GDB Remote Serial Protocol (RSP) :3333
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              OpenOCD (Running on Linux Host)                │
│  - Translates GDB commands into RISC-V Debug Module actions │
│  - Communicates directly over local memory-mapped bus (dmem)│
└─────────────────────────────┬───────────────────────────────┘
                              │ Physical Interconnect (MMIO)
                              ▼
┌─────────────────────────────────────────────────────────────┐
│       Memory-Mapped RISC-V Debug Module Interface (DMI)     │
│       (Directly mapped into host physical address space)    │
└─────────────────────────────────────────────────────────────┘
```

> **The Open-Source Community Lineage:**  
> This on-chip debugging methodology was pioneered by **Nishanth Menon** (Texas Instruments) and **Jason Kridner** (BeagleBoard.org Foundation) on platforms like the **BeaglePlay** and **BeagleBone AI-64** by introducing the `dmem` adapter driver into upstream OpenOCD (`board/ti_am625_swd_native.cfg`). By routing debug registers across the internal bus, developers can set breakpoints, single-step co-processor firmware, and inspect registers natively over SSH without soldering a single header wire.

The presentation documenting this breakthrough is essential viewing:  
🎥 **[Debugging Heterogeneous SoC Using OpenOCD — Nishanth Menon, Texas Instruments (YouTube)](https://youtu.be/hKFvxgbHUfg?si=Mhd7lEJgq9oBp3t9)**

### 5.2 Current Allwinner T527 Silicon Reality: No DM or DMI Register Mapping
On current **Allwinner T527 silicon (Radxa Cubie A5E)**:
* **There is NO memory-mapped Debug Module (DM) or Debug Module Interface (DMI) register block on the system interconnect.**
* The physical address `0x07090000` often speculated about in early community discussions is actually the **RTC (Real-Time Clock)** register block in the official Allwinner T527 User Manual.
* Because the XuanTie RISC-V Debug Module is not routed into the non-secure ARM bus interconnect, target-hosted OpenOCD cannot attach to the co-processor directly over the internal bus.

### 5.3 The Hope for Future Allwinner Silicon
The entire purpose of examining this JTAG-less architecture is to **advocate and express our hope that Allwinner will incorporate a memory-mapped DMI interface in future SoC revisions**.

Adding an MMIO window to the RISC-V Debug Module Interface (DMI) on future Allwinner silicon would allow the open-source Linux community to utilize native, target-hosted OpenOCD and GDB remote debugging out of the box—matching the development experience on TI Sitara and STMicroelectronics platforms.

### 5.4 Practical Debugging Workflows on Current T527 Hardware
For engineers developing XuanTie E907 firmware on current T527 silicon today, reliable diagnostics are achieved using:
1. **Linux RemoteProc Trace Buffers (`trace0`)**: Continuous, zero-overhead circular log streaming via the debugfs entry `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
2. **Dedicated Hardware UART (`S_UART0`)**: Independent serial console output mapped to `0x07080000` (115200 baud), providing immediate low-level boot diagnostics.
3. **Lock-Free Shared SRAM Ring Buffers**: High-speed Single Producer Single Consumer (SPSC) telemetry buffers in Shared PubSRAM C (`0x00020000`) or Dedicated MCU SRAM (`0x3FFC0000`), synchronized with hardware mailbox doorbells.
4. **External Hardware JTAG Probes**: Connecting an external debug probe (e.g., T-Head CK-Link or SEGGER J-Link) to the physical JTAG test pads on the board when instruction-level single-stepping or hardware watchpoints are required.

---

## 6. Summary & What's Next in Part 2

In this introductory article, we established:
1. **Why we want to use the RISC-V co-processor**: Deterministic SRAM execution (bypassing DRAM refresh jitter), offloading Linux CPU cycles, executing 10–50 kHz hard real-time control loops, and maintaining fault isolation.
2. **Silicon architecture & TRM mapping**: Navigating `sun55i-a523` naming, board differences between Cubie A5E and A7A, and exact register locations in the *Allwinner T527 User Manual V0.92*.
3. **ITCM and DTCM memory architecture**: Why RemoteProc cannot load TCM directly, and how the two-stage LMA vs. VMA bootstrapping pattern (`startup.S` copy loop and `fence.i`) enables deterministic 1-cycle execution.
4. **The DMEM debugging paradigm**: Clarifying that current T527 silicon does not route a memory-mapped DMI block, explaining why we advocate for Allwinner to adopt this in future silicon, and outlining current production debugging options.

In **Part 2**, we move from architecture to software implementation:
* Authoring the **Linux 7.1 `sunxi_rproc.c` RemoteProc kernel driver**.
* Configuring multi-segment ELF placement (PubSRAM C, Dedicated MCU SRAM, and DDR carveouts) and built-in debugfs trace logging.
* Proving hardware state transitions using automated Python test tooling over OpenOCD.

---

### Series Navigation
* **Part 1: Architecture and Memory-Mapped Debugging** *(You are here)*
* **[Part 2: Building the Linux `remoteproc` Driver and Proving Hardware State](part2_building_remoteproc_and_hardware_proof.md)**
* **[Part 3: Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**
* **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E907](part4_deep_dive_baremetal_cpp_coroutines.md)**
