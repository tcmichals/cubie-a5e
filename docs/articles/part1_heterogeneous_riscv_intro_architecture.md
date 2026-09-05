# Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Part 1): Architecture and Memory-Mapped Debugging

Heterogeneous multi-core SoCs—pairing high-performance 64-bit ARM Cortex-A application cores with low-power, deterministic auxiliary microcontrollers—have become the standard architecture for modern embedded systems. Silicon like the **Allwinner T527 / A527** (featured on the **Radxa Cubie A5E**) integrates an octa-core ARM Cortex-A55 cluster alongside an auxiliary **XuanTie E907 RISC-V core** (RV32IMAFDC @ 200 MHz) and a **Cadence Tensilica HiFi4 Audio DSP** (@ 600 MHz).

Getting these co-processors online requires establishing reliable hardware lifecycle control, clock tree synchronization, and deterministic memory placement before loading production firmware.

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

This article is **Part 1 of a series** documenting the practical bring-up of the XuanTie E907 RISC-V co-processor on Linux:
* **Part 1 (This Article)**: Why use the RISC-V co-processor, TRM memory maps, SRAM / TCM hardware architecture & RemoteProc boot mechanics, and the on-chip memory-mapped debugging paradigm.
* **Part 2**: Authoring the Linux `remoteproc` kernel driver, memory-mapped ELF loading into PubSRAM C and Dedicated MCU SRAM, and systematically proving hardware state with the `riscv-firmware/apps` suite.
* **Part 3**: General embedded firmware development, TCM vs. DRAM memory determinism, lightweight lock-free IPC (libmetal), live GDB workflows, and an introduction to bare-metal C++ coroutines.
* **Part 4**: Deep dive into C++20 coroutines on bare-metal RISC-V—benchmarks, memory profiles, and comparison against RTOS task switching.

---

## 1. Why Use the XuanTie RISC-V Co-Processor?

Modern embedded Linux platforms excel at complex workloads—networking, file systems, multimedia pipelines, computer vision, and machine learning. However, running jitter-sensitive, hard real-time control tasks directly on an application processor introduces fundamental engineering challenges.

The auxiliary **XuanTie E907 RISC-V core** on the Allwinner T527 solves these challenges through asymmetric multiprocessing (AMP):

### 1.1 Deterministic Timing: SRAM & TCM vs. External DRAM
* **The DRAM Bottleneck**: The ARM Cortex-A55 cluster executes operating system workloads out of external LPDDR4/4X dynamic RAM (`0x40000000`). Even with the Linux `PREEMPT_RT` patchset, DRAM access is non-deterministic. Periodic DRAM row refreshes (`tRFC`), memory controller queue arbitration among 8 CPU cores, GPU, NPU, ISP, and DMA engines, and cache-line refills introduce latency spikes ranging from hundreds of nanoseconds to several milliseconds.
* **Zero-Wait-State SRAM & TCM Execution**: The XuanTie E907 executes out of dedicated Tightly-Coupled Memories (64 KB ITCM and 64 KB DTCM) and internal fast SRAM (PubSRAM C at `0x00020000` and Dedicated MCU SRAM at `0x3FFC0000`). On-chip TCM and SRAM deliver fixed, single-cycle, zero-wait-state memory access with zero refresh interruptions. Instruction execution times and memory latency are 100% deterministic.

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
| **Auxiliary Real-Time Core** | **XuanTie E907 RISC-V** (RV32IMAFDC @ 200 MHz, 32 GPRs, FPU) | **XuanTie E902 RISC-V** (RV32EMC @ 200 MHz, 16 GPRs, No FPU) |
| **Tightly-Coupled Memory (TCM)**| **64 KB ITCM** (`0x00000000` Core) / **64 KB DTCM** (`0x00080000` Core)<br>*(Internal Harvard pipeline; populated via SRAM LMA/VMA staging)* | **None** (E902 silicon has NO TCM blocks) |
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
  - Documents the top-level memory map:
    - `PubSRAM C`: `0x00020000 – 0x0003FFFF` (128 KB)
    - `Dedicated MCU SRAM / SRAM A3`: `0x07280000 – 0x072BFFFF` (256 KB)
    - `Hardware MSGBOX`: `0x03003000 – 0x03003FFF` (4 KB)
    - `RTC`: `0x07090000 – 0x070903FF` (1 KB)
    - `System DRAM`: `0x40000000` base
  - *Silicon Confirmation*: Notice that address `0x07090000` is explicitly documented as the **RTC (Real-Time Clock)** register block. The T527 User Manual contains no memory-mapped Debug Module (DM/DMI) entry anywhere in the system bus interconnect tables.
* **Chapter on MCU Subsystem & Memory Mapping (Section on MCU Memory)**:
  - `PubSRAM C` (`0x00020000 – 0x0003FFFF`, 128 KB): Shared on-chip SRAM, 1:1 identity mapped across ARM host and XuanTie E907. This is the primary boot and execution memory for the co-processor.
  - `Dedicated MCU SRAM / SRAM A3` (`0x07280000 – 0x072BFFFF`, 256 KB): Dedicated zero-wait-state MCU SRAM (mapped to core address `0x3FFC0000 – 0x3FFFFFFF`).
  - `Core Local TCM`: 64 KB ITCM (`0x00000000` Core) and 64 KB DTCM (`0x00080000` Core) connected directly to the XuanTie E907 Harvard pipeline.
* **Chapter on MCU Subsystem & RISC-V Configuration (`RISCV_CFG` @ `0x07130000`)**:
  - `0x0000` (`VER_REG`): Version Register.
  - `0x0204` (`STA_ADD_REG`): **Start Vector / Boot Address Register**. Defines the initial program counter address fetched by the E907 core upon reset deassertion. On T527, Linux RemoteProc sets this to `0x00020000` to boot the core cleanly from PubSRAM C.
  - `0x0248` (`WORK_MODE_REG`): Work Mode Register. Bit 3 (`BIT_LOCK_STA`) indicates hardware core lockup status (0 = Running normally, 1 = Core lockup).
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
| **Shared PubSRAM C** | **`0x00020000`** | **`0x00020000`** | **128 KB** | `reg-names = "sram"` | **Primary boot and execution window** (`.vectors`, `.text`, `.data`, `.stack`, `.resource_table`). 1:1 identity mapped across host and core. |
| **Dedicated MCU SRAM (`r_sram`)** | **`0x07280000`** | **`0x3FFC0000`** | **256 KB** | `reg-names = "r_sram"` | Zero-wait-state dedicated MCU SRAM space 0. High-performance memory for real-time control loops and SPSC ring buffers. |
| **RISC-V CFG Control Block** | **`0x07130000`** | **`0x07130000`** | **4 KB** | `reg-names = "cfg"` | Hardware MMIO registers: Version (`0x0000`), Boot Entry Vector `STA_ADD_REG` (`0x0204`, set to `0x00020000`), and Work Mode / Lockup Status `WORK_MODE_REG` (`0x0248`). Not general SRAM. |
| **MCU CCU Clocks & Resets** | **`0x07102000`** | **`0x07102000`** | **4 KB** | `clocks = <&mcu_ccu ...>` | Clock gate (`0x07102120`), resets (`0x07102124`: bit 16 CFG, bit 17 DBG, bit 18 CORE), and PubSRAM clock/reset (`0x07102114`). |
| **Hardware MSGBOX** | **`0x03003000`** | **`0x03003000`** | **4 KB** | `mboxes = <&msgbox 0>, <&msgbox 1>` | 8-channel bi-directional doorbell FIFO. Channel 0: RISC-V to Linux (GIC SPI 147); Channel 1: Linux to RISC-V (PLIC IRQ 25). |
| **Instruction TCM (ITCM)** | Internal | **`0x00000000`** | **64 KB** | Private E907 Bus | Single-cycle zero-wait-state instruction execution. Directly connected to the E907 fetch pipeline; populated via startup LMA-to-VMA staging from SRAM. |
| **Data TCM (DTCM)** | Internal | **`0x00080000`** | **64 KB** | Private E907 Bus | Single-cycle zero-wait-state data memory. Directly connected to the E907 load/store pipeline; populated via startup LMA-to-VMA staging from SRAM. |
| **RemoteProc Trace Buffer (`trace0`)** | **`0x00020000`+** / `0x48000000` | **`0x00020000`+** / `0x48000000` | **4 KB** | `resource_table` / `memory-region` | RemoteProc debugfs trace buffer (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`). Mapped inside PubSRAM C by default (`.trace_buffer`), or allocated via DDR carveout (`0x48000000`). |
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
    (Device Tree: "sram", Identity Mapped)            (Primary Boot, .vectors, .text, stack)

  0x07280000 - 0x072BFFFF [ 256 KB ] ─────────────> 0x3FFC0000 - 0x3FFFFFFF (Dedicated SRAM)
    (Device Tree: "r_sram")                           (Zero-Wait-State High-SRAM Window)

  0x07130000 - 0x07130FFF [   4 KB ] ─────────────> 0x07130000 - 0x07130FFF (CFG Regs)
    (Device Tree: "cfg", STA_ADD_REG 0x204)           (Boot Vector: 0x00020000)

  0x03003000 - 0x03003FFF [   4 KB ] ─────────────> 0x03003000 - 0x03003FFF (MSGBOX)
    (ARM GIC IRQ 147)                                 (RISC-V PLIC IRQ 25)

  0x48000000 - 0x48000FFF [   4 KB ] ─────────────> 0x48000000 - 0x48000FFF (trace0)
    (RemoteProc Trace Carveout)                       (Debugfs Live Logging)

  0x48100000 - 0x481FFFFF [   1 MB ] ─────────────> 0x48100000 - 0x481FFFFF (DDR DMA Pool)
    (Reserved Memory Pool)                            (High-Bandwidth Telemetry Buffers)

  ---------------------------------------------------------------------------------
  [ XUANTIE E907 INTERNAL HARVARD CORE MEMORY (Populated via SRAM LMA->VMA Staging) ]
  • ITCM (Instruction TCM): 0x00000000 - 0x0000FFFF (64 KB, 1-cycle fetch, no miss)
  • DTCM (Data TCM):        0x00080000 - 0x0008FFFF (64 KB, 1-cycle load/store)
+===================================================================================+
```

---

## 4. Memory Architecture & Boot Mechanics: SRAM, ITCM, and DTCM

For embedded firmware engineers, understanding the interplay between **on-chip SRAM** and **Tightly-Coupled Memories (ITCM / DTCM)** on the XuanTie E907 is essential for achieving reliable boots and microsecond-scale execution determinism.

### 4.1 The Boot Reality: RemoteProc Boots Directly from PubSRAM C (`0x00020000`)
On the Allwinner T527, Linux RemoteProc and the XuanTie E907 co-processor communicate and boot through dedicated on-chip SRAM:

1. **Device Tree Bindings (`sun55i-a523.dtsi`)**:
   During driver probe, `sunxi_rproc.c` binds to the registered hardware blocks:
   ```dts
   rproc: remoteproc@7130000 {
       compatible = "allwinner,sun55i-a523-rproc",
                    "allwinner,sun55i-a527-rproc";
       reg = <0x07130000 0x1000>,
             <0x07280000 0x40000>,
             <0x00020000 0x20000>;
       reg-names = "cfg", "r_sram", "sram";
       ...
   };
   ```

2. **Loading Firmware into PubSRAM C**:
   The primary co-processor firmware (`firmware_t527.ld`) is linked to execute out of **PubSRAM C (`0x00020000`)**:
   ```ld
   MEMORY {
       SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K
   }
   ```
   When Linux loads `firmware.elf`, `sunxi_rproc_da_to_va()` translates device addresses in the range `0x00020000`–`0x0003FFFF` directly to the mapped host virtual address of `sram` and copies `.vectors`, `.text`, and `.data` via `memcpy_toio()`.

3. **Starting the Core via `STA_ADD_REG`**:
   When starting the co-processor (`echo start > /sys/class/remoteproc/remoteproc0/state`):
   - `sunxi_rproc_start()` retrieves the ELF entry point (`rproc->bootaddr`), which is **`0x00020000`** (`_start`).
   - The driver programs this address into the hardware boot vector register `STA_ADD_REG` (`0x07130204`):
     ```c
     writel((u32)rproc->bootaddr, priv->cfg_va + E906_STA_ADD_REG);
     ```
   - The driver deasserts the core run reset (`rst_core`, bit 18 in `MCU_RST_REG 0x07102124`).
   - The XuanTie E907 begins execution immediately from `0x00020000` in PubSRAM C.
   - All 7 progressive test apps in `riscv-firmware/apps/` (`testBasic`, `testStringBinaryTrace0`, `testCrash`, `testPing`, `testPingRpmsg`, `testDRAMMsg`, `exampleRiscv`) boot and run reliably using this direct SRAM architecture.

### 4.2 The Role of ITCM and DTCM on XuanTie E907
While PubSRAM C provides excellent zero-wait-state performance for general firmware execution, high-frequency real-time applications benefit from the XuanTie E907's dedicated **Tightly-Coupled Memories**:
* **ITCM (64 KB @ `0x00000000` Core)**: Connected directly to the core's 5-stage instruction fetch pipeline. Instruction fetches complete in **1 clock cycle with zero wait states**, completely bypassing the L1 instruction cache. This eliminates cache miss penalties, bus arbitration delays, and pipeline stalls.
  - *Ideal Use Cases*: Time-critical interrupt handlers (Mailbox doorbell ISR, SPI DMA callbacks), machine-mode trap handlers (`mtvec`), and inner PID flight attitude estimation loops.
* **DTCM (64 KB @ `0x00080000` Core)**: Connected directly to the core's load/store execution unit. Reads and writes complete in **1 clock cycle with zero wait states**, bypassing the L1 data cache.
  - *Ideal Use Cases*: Fast call stack (`.stack`), lookup tables (LUTs), critical state variables, and lock-free ring buffer pointers.

### 4.3 Staging Code & Data into TCM: The LMA vs. VMA Pattern
Because ITCM and DTCM reside on the private internal bus of the XuanTie E907 core, the co-processor does not boot from `0x00000000` directly. Instead, embedded developers use the standard **LMA (Load Memory Address) vs. VMA (Virtual/Execution Memory Address) staging pattern**:
1. **Host Loading (LMA)**: Linux RemoteProc loads the entire firmware binary into accessible on-chip SRAM (PubSRAM C at `0x00020000` or Dedicated MCU SRAM at `0x3FFC0000`).
2. **Core Startup**: The E907 starts executing from SRAM (`_start` at `0x00020000`).
3. **Core-Initiated Copy (VMA)**: Early in the startup assembly sequence, the E907's own CPU instructions copy designated `.itcm` functions and `.dtcm` data from SRAM (LMA) to TCM (VMA).
4. **Instruction Synchronization (`fence.i`)**: The core executes `fence.i` to invalidate and synchronize its instruction fetch pipeline so newly copied instructions in ITCM are fetched cleanly.
5. **Execution**: The core branches into or calls the TCM-resident routines, running at pure 1-cycle latency!

> **Current Repository Baseline**: The working firmware in this repository ([`riscv-firmware/common/arch_riscv/firmware_t527.ld`](../../riscv-firmware/common/arch_riscv/firmware_t527.ld)) uses a **pure SRAM-only** linker script — all sections (`.vectors`, `.text`, `.rodata`, `.data`, `.bss`, `.stack`, `.resource_table`) are placed in PubSRAM C (`0x00020000`). This is a reliable, zero-complexity baseline that runs all 7 firmware apps successfully. The TCM staging pattern described in Section 4.4 below is the **production extension pattern** for deploying latency-critical ISRs and real-time data into ITCM/DTCM — it can be layered on top of the existing `firmware_t527.ld` baseline when sub-microsecond ISR jitter becomes a hard requirement.

### 4.4 Step-by-Step Staging Implementation (Production TCM Extension Pattern)
The following shows how to extend the baseline linker script and startup code to add ITCM/DTCM staging for latency-critical real-time sections:

#### 1. GNU Linker Script (`firmware_tcm_staging.ld`) — Extends `firmware_t527.ld` with ITCM/DTCM regions
```ld
OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* Primary Executable & Staging SRAM */
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K

    /* Tightly-Coupled Memories (Local E907 Core View) */
    ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx) : ORIGIN = 0x00080000, LENGTH = 64K
}

SECTIONS
{
    /* Primary bootstrap and vector table in SRAM */
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
        *(.fastcode)
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
        *(.fastdata)
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

    /* Linux RemoteProc Resource Table in Shared SRAM */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
    } > SRAM
}
```

#### 2. Assembly Startup Staging Routine — TCM Copy Extension

> **Note**: The current [`riscv-firmware/common/arch_riscv/startup.S`](../../riscv-firmware/common/arch_riscv/startup.S) performs `.data` copy and `.bss` zero-initialization within SRAM only. The assembly below shows the additional ITCM/DTCM staging loops to insert when TCM sections are added to the linker script.

```assembly
    /* =============================================================
     * 1. Copy Critical Code from SRAM (LMA) to ITCM (VMA)
     * ============================================================= */
    la      a0, _sitcm              /* Destination: ITCM start (0x00000000) */
    la      a1, _eitcm              /* Destination: ITCM end */
    la      a2, _sidata_itcm        /* Source: LMA in SRAM */
    beq     a0, a2, .Lcopy_itcm_done /* Skip if LMA == VMA */
.Lcopy_itcm_loop:
    bgeu    a0, a1, .Lcopy_itcm_done
    lw      t0, 0(a2)
    sw      t0, 0(a0)
    addi    a0, a0, 4
    addi    a2, a2, 4
    j       .Lcopy_itcm_loop
.Lcopy_itcm_done:
    fence.i                         /* CRITICAL: Synchronize instruction cache & pipeline */

    /* =============================================================
     * 2. Copy Critical Data from SRAM (LMA) to DTCM (VMA)
     * ============================================================= */
    la      a0, _sdtcm              /* Destination: DTCM start (0x00080000) */
    la      a1, _edtcm              /* Destination: DTCM end */
    la      a2, _sidata_dtcm        /* Source: LMA in SRAM */
    beq     a0, a2, .Lcopy_dtcm_done
.Lcopy_dtcm_loop:
    bgeu    a0, a1, .Lcopy_dtcm_done
    lw      t0, 0(a2)
    sw      t0, 0(a0)
    addi    a0, a0, 4
    addi    a2, a2, 4
    j       .Lcopy_dtcm_loop
.Lcopy_dtcm_done:

    /* =============================================================
     * 3. Zero DTCM Uninitialized BSS (.dtcm_bss)
     * ============================================================= */
    la      a0, _sbss_dtcm
    la      a1, _ebss_dtcm
.Lzero_dtcm_loop:
    bgeu    a0, a1, .Lzero_dtcm_done
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       .Lzero_dtcm_loop
.Lzero_dtcm_done:
```

#### 3. Pinning Functions and Data in C/C++
```c
#define __ITCM_TEXT __attribute__((section(".itcm"), noinline))
#define __DTCM_DATA __attribute__((section(".dtcm")))

/* Pinned in ITCM: 1-cycle execution, zero cache miss penalty */
void __ITCM_TEXT fast_flight_loop_isr(void) {
    /* Critical 50 kHz PID attitude loop */
}
```

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
3. **Memory architecture & boot mechanics**: How Linux RemoteProc (`sunxi_rproc.c`) loads firmware into PubSRAM C (`0x00020000`), programs `STA_ADD_REG` to boot directly from SRAM, and how time-critical routines can be staged into ITCM/DTCM for 1-cycle deterministic latency.
4. **The DMEM debugging paradigm**: Clarifying that current T527 silicon does not route a memory-mapped DMI block, explaining why we advocate for Allwinner to adopt this in future silicon, and outlining current production debugging options.

In **Part 2**, we move from architecture to software implementation:
* Authoring the **Linux 7.1 `sunxi_rproc.c` RemoteProc kernel driver**.
* Configuring multi-segment ELF placement (PubSRAM C, Dedicated MCU SRAM, and DDR carveouts) and built-in debugfs trace logging.
* Proving hardware state transitions using the all-new `riscv-firmware/apps` verification suite (`testBasic`, `testStringBinaryTrace0`, `testCrash`, `testPing`, `testPingRpmsg`, `testDRAMMsg`).

---

### Series Navigation
* **Part 1: Architecture and Memory-Mapped Debugging** *(You are here)*
* **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](part2_building_remoteproc_and_hardware_proof.md)**
* **[Part 3: Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**
* **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E907](part4_deep_dive_baremetal_cpp_coroutines.md)**
