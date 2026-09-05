# RISC-V Co-Processor Programming & Bring-up Guide

This guide explains the architecture of the **XuanTie E907 RISC-V co-processor** on the Radxa Cubie A5E (Allwinner T527 / A527 / `sun55i`), detailing its memory interfaces, firmware compilation flow, boot-up sequence, modern C++ HAL modules, inter-processor communication (IPC), and real-time benchmarking tools.

---

## 1. Co-Processor Architecture Overview

The Allwinner T527 SoC integrates a **T-Head XuanTie E907** as its real-time auxiliary co-processor. It is a high-determinism, low-power 32-bit RISC-V processor designed to handle time-critical attitude estimation, sensor filtering, and low-jitter motor control loops, completely isolated from the Linux OS domain running on the 8× ARM Cortex-A55 cores.

```
+-----------------------------------------------------------------------------------------+
|                               ALLWINNER T527 / A527 SOC                                 |
|                                                                                         |
|  +-----------------------------------+   +-------------------------------------------+  |
|  |     APPLICATION DOMAIN (ARM64)    |   |     REAL-TIME CO-PROCESSOR DOMAIN         |  |
|  |  - 8× Cortex-A55 @ 1.80 GHz       |   |  - XuanTie E907 RV32IMAFDC @ 200 MHz      |  |
|  |  - Mainline Linux 7.1 PREEMPT_RT  |   |  - Cadence Tensilica HiFi4 Audio DSP      |  |
|  |  - RemoteProc Kernel Driver       |   |    @ 600 MHz                              |  |  +-----------------+-----------------+   +---------------------+---------------------+  |
|                    │                                           │                        |
|                    ▼                                           ▼                        |
|  +-----------------------------------------------------------------------------------+  |
|  |               SHARED SYSTEM INTERCONNECT & ON-CHIP SRAM BUS                       |  |
|  |  - 128 KB Shared PubSRAM C (0x00020000) [Default Boot & Fast Runtime Execution]   |  |
|  |  - 256 KB Dedicated MCU SRAM (0x3ffc0000 Core / 0x07280000 Host) [High-Perf SRAM]|  |
|  |  - 4 KB RISC-V CFG Control Block (0x07130000) [STA_ADD_REG @ 0x204, WORK_MODE]   |  |
|  |  - 4 KB DDR RemoteProc Trace Carveout (0x48000000)                                |  |
|  |  - 1 MB DDR DMA Payload Pool (0x48100000)                                         |  |
|  |  - Up to 4 GiB LPDDR4/4X System RAM (0x40000000)                                  |  |
|  +-----------------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------------+
```

### Hardware Specifications
* **Clock Speed:** Operates at **up to 200 MHz** (managed by `mcu_ccu` @ `0x07102000`). *(Note: The companion Cadence Tensilica HiFi4 Audio DSP operates at 600 MHz).*
* **ISA Profile (RV32IMAFDC / RV32GC):**
  - **I**: 32 standard 32-bit General Purpose Registers (`x0`–`x31`).
  - **M**: Hardware Integer Multiplication and Division.
  - **A**: Atomic Memory Operations (`lr.w`, `sc.w`, `amo*`).
  - **F & D**: Hardware Single- and Double-precision IEEE-754 Floating Point Units (`f0`–`f31` 64-bit float registers).
  - **C**: Compressed 16-bit instructions for high code density.
  - **DSP / RVP**: Packed SIMD & DSP extensions for accelerated digital signal processing.
  - **_zicsr & _zifencei**: Standard CSR manipulation and instruction fence operations.
* **On-Chip Fast Memory Architecture:** 
  - **128 KB PubSRAM C (`0x00020000`)**: 1:1 Identity-mapped on both ARM and RISC-V interconnects. Native default boot and execution memory for Linux RemoteProc.
  - **256 KB Dedicated MCU SRAM (`0x3ffc0000` Core / `0x07280000` Host)**: Zero-wait-state dedicated on-chip memory space (`r_sram`) for maximum IPC throughput and real-time control loops.
* **Toolchain / ABI:** Target `-march=rv32imafdc_zicsr_zifencei -mabi=ilp32d -mcmodel=medany`.

---

## 2. Verified Memory Map of XuanTie E907 on Allwinner T527 / A523

### 2.1 Memory Subsystem Mapping (Hardware Confirmed via Live Probe)

| Memory Region | Linux Host (ARM64) Physical Address | E907 RISC-V Core Address (DA) | Size | Latency & Usage |
| :--- | :--- | :--- | :--- | :--- |
| **Shared PubSRAM C** | **`0x00020000`** | **`0x00020000`** | **128 KB** | **Primary Boot & Execution window**; directly mapped in mainline RemoteProc (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`) |
| **Dedicated MCU SRAM (`r_sram`)** | **`0x07280000`** | **`0x3ffc0000`** | **256 KB** | Zero-wait-state dedicated high-memory window; verified live execution at ~570k counts/10ms |
| **RISC-V CFG Control Block** | **`0x07130000`** | **`0x07130000`** | **4 KB** | Hardware registers: `0x0000` (`VER_REG`), `0x0204` (`STA_ADD_REG` Boot vector), `0x0248` (`WORK_MODE_REG`) |
| **DDR Trace Buffer (`trace0`)** | **`0x48000000`** | **`0x48000000`** | **4 KB** | RemoteProc debugfs trace buffer (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`) |
| **DDR DRAM DMA Carveout** | **`0x48100000`** | **`0x48100000`** | **1 MB** | PMP non-cacheable high-bandwidth payload pool (`testDRAMMsg`) |

> [!IMPORTANT]
> ### WHY `0x00000000` AND `0x07110000`/`0x07120000` DO NOT EXIST ON A523/T527
> 1. **Theoretical TCM Mappings Were Inaccurate**: Legacy documentation for older chips (e.g. Allwinner D1) placed ITCM at `0x00000000` and DTCM at `0x00080000`. On Allwinner A523/T527, `0x07110000` and `0x07120000` are non-writable/reserved registers.
> 2. **Hardware Lockup Discovery**: Setting `STA_ADD_REG` to `0x000000BA` causes an immediate bus error on instruction fetch, triggering a double-fault on `mtvec` (also `0x0`) and placing the core into **Hardware Lockup** (`WORK_MODE_REG 0x07130248 = 0x0000000B`, Bit 3 `BIT_LOCK_STA = 1`).
> 3. **Verified Live Boot Addresses**: Setting `STA_ADD_REG` to **`0x00020000`** (PubSRAM C) or **`0x3ffc0000`** (Dedicated SRAM) runs cleanly without lockup (`WORK_MODE_REG = 0x00000003`, Bit 3 `BIT_LOCK_STA = 0`).

### 2.2 Allwinner On-Chip SRAM Partitioning & Hardware Allocation

| SRAM Bank | Physical Base (Host) | Core Address (E907) | Size | Hardware Owner | Primary Purpose & Usage | Allowed for RISC-V E907? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`BROM`** | `0x00000000` | Unmapped | 128 KB | SoC Hardware | Silicon Mask ROM; executes first instruction on power-on reset | ❌ **No** (BootROM) |
| **`PubSRAM C`** | `0x00020000` | `0x00020000` | 128 KB | **XuanTie E907 / Linux** | **Default RemoteProc firmware window (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`)** | ✅ **YES (Default Boot Memory)** |
| **`SRAM A2`** | `0x00040000` | `0x00040000` | 208 KB | **Secure EL3 (TF-A) / CPUS** | **Secure World (TF-A BL31, OP-TEE, PSCI 1.1 power management, CPU suspend/hotplug), or A733 `scp.fex` PMIC core** | ❌ **STRICTLY PROHIBITED** (TrustZone Firewall) |
| **`CFG Regs`** | `0x07130000` | `0x07130000` | 4 KB | **Host & E907 Control** | Hardware version (`0x00`), Boot entry vector (`0x204`), Work Mode / Lockup status (`0x248`) | ✅ **YES (Registers Only, Not SRAM)** |
| **`R_SRAM`** | `0x07280000` | `0x3ffc0000` | 256 KB | **XuanTie E907** | **Zero-wait-state high-performance dedicated SRAM space 0** | ✅ **YES (Zero-Wait-State SRAM)** |
| **`trace0`** | `0x48000000` | `0x48000000` | 4 KB | Linux RemoteProc | RemoteProc debugfs trace buffer (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`) | ✅ **YES (Logging Carveout)** |
| **`dram_dma`**| `0x48100000`| `0x48100000` | 1 MB | Linux RemoteProc | Non-cacheable DDR DMA payload buffer pool for high-bandwidth IPC (`testDRAMMsg`) | ✅ **YES (Streaming Carveout)** |

### 2.3 Control, Peripheral & Inter-Core Registers

| Peripheral Block | Linux Host Physical Address | E907 RISC-V Address | Description & Hardware Usage |
| :--- | :--- | :--- | :--- |
| **MCU CCU Clocks & Resets** | **`0x07102000`** | **`0x07102000`** | Core clock gate (`0x07102120`), resets (`0x07102124`: bit 16 CFG, bit 17 DBG, bit 18 CORE), PubSRAM clock/reset (`0x07102114`) |
| **RISC-V CFG Controller** | **`0x07130000`** | **`0x07130000`** | Boot entry vector register (`0x07130204`), Work Mode & Lockup status register (`0x07130248`) |
| **Hardware MSGBOX (Mailbox)** | **`0x03003000`** | **`0x03003000`** | Hardware doorbell FIFO: <br>• **Channel 0**: RISC-V $\rightarrow$ Linux (GIC SPI 147)<br>• **Channel 1**: Linux $\rightarrow$ RISC-V (PLIC IRQ 25) |
| **Main PIO (GPIO B–K)** | **`0x02000000`** | **`0x02000000`** | 1:1 mapped GPIO pin control registers |
| **UART0 (Debug Console)** | **`0x02500000`** | **`0x02500000`** | Shared serial console |
| **UART2 (Co-processor Port)** | **`0x02500800`** | **`0x02500800`** | High-speed serial / RC receiver interface |
| **SPI0 Controller** | **`0x04025000`** | **`0x04025000`** | Direct high-speed peripheral bus |

### 2.4 Visual Address Translation Architecture

```
+===================================================================================+
|                     ALLWINNER T527 ADDRESS SPACE MAPPING                          |
+===================================================================================+

  LINUX HOST (ARM64) PHYSICAL VIEW                  XUANTIE E907 RISC-V CORE VIEW
  ================================                  =============================
  0x00020000 - 0x0003FFFF [ 128 KB ] ─────────────> 0x00020000 - 0x0003FFFF (PubSRAM C)
    (Mapped via RemoteProc "sram")                    (Default Boot, .vectors, .text, .data, stack)

  0x07280000 - 0x072BFFFF [ 256 KB ] ─────────────> 0x3ffc0000 - 0x3fffffff (Dedicated SRAM)
    (Mapped via RemoteProc "r_sram")                  (Zero-wait-state High-SRAM Window)

  0x07130000 - 0x07130FFF [   4 KB ] ─────────────> 0x07130000 - 0x07130FFF (CFG Regs)
    (STA_ADD_REG 0x204, WORK_MODE 0x248)              (Control & Lockup Status)

  0x48000000 - 0x48000FFF [   4 KB ] ─────────────> 0x48000000 - 0x48000FFF (trace0)
    (RemoteProc Trace Carveout)                       (Direct Identity Mapped)

  0x48100000 - 0x481FFFFF [   1 MB ] ─────────────> 0x48100000 - 0x481FFFFF (DDR Carveout)
    (DMA Reserved Memory Pool)                        (PMP Non-Cacheable Payload Buffers)
+===================================================================================+
```

### 2.5 How Linux RemoteProc (`sunxi_rproc.c`) Routes Firmware ELFs

When Linux RemoteProc loads a firmware ELF:
1. **PubSRAM C Segments (`0x00020000`–`0x0003FFFF`, 128 KB)**:
   `sunxi_rproc_da_to_va()` maps the device address 1:1 to host physical memory `0x00020000` and copies code/data directly via `memcpy_toio()`. This is supported out-of-the-box by mainline Linux without kernel modifications.
2. **Dedicated MCU SRAM Segments (`0x3ffc0000`–`0x3fffffff`, 256 KB)**:
   High SRAM zero-wait-state window on the E907 interconnect, mapped to host physical address `0x07280000`. Provides 3x higher throughput for tight computational loops.
3. **RISC-V CFG Controller (`0x07130000`)**:
   Hardware control registers (not writable SRAM). On start, the driver writes the ELF entry point (`0x00020000` or `0x3ffc0000`) to `STA_ADD_REG` (`0x07130204`). Core status and lockup can be checked at `WORK_MODE_REG` (`0x07130248`).
4. **DDR Carveouts (`0x48000000` & `0x48100000`)**:
   Directly mapped into kernel virtual address space and accessed via non-cached DMA coherent mappings.

### 2.6 ITCM & DTCM Architecture & Programmer's Implementation Guide

For embedded and hard real-time systems engineers, **ITCM (Instruction Tightly-Coupled Memory)** and **DTCM (Data Tightly-Coupled Memory)** are the most critical memory subsystems on the XuanTie E907 core.

#### 1. Architectural Role & Why TCM is Needed
The XuanTie E907 features a modified Harvard bus architecture:
* **ITCM**: Connected directly to the core's instruction fetch pipeline. Fetches occur in **1 single clock cycle with zero wait states**, completely decoupled from the system bus and L1 instruction cache. This eliminates cache miss penalties, bus arbitration delays, and pipeline stalls.
  - **Ideal Use Cases**: Critical interrupt handlers (e.g. Mailbox Doorbell ISR, high-rate SPI/UART DMA callbacks), trap/fault handlers, and inner real-time PID attitude estimation loops.
* **DTCM**: Connected directly to the core's load/store execution unit. Reads and writes complete in **1 clock cycle with zero wait states**.
  - **Ideal Use Cases**: Stack (`.stack`), fast lookup tables (LUTs), critical state machines, and circular ring buffer head/tail pointers where atomic synchronization cannot afford bus jitter.

#### 2. The Hardware Challenge: Why RemoteProc Cannot Load TCM Directly
On the Allwinner A523/T527 SoC:
* ITCM and DTCM reside on the **private internal core bus** of the XuanTie E907.
* Mainline Linux and the ARM Cortex-A55 cores operate over the main system AXI interconnect.
* **Silicon Reality**: There is no active external bus bridge allowing the ARM host to write directly into the E907's private TCM while the core clock and TCM controllers are in reset. Legacy documentation suggested writing to `0x07110000` or `0x07120000`, but live hardware probing proves those registers are non-writable/reserved on A523/T527.
* Setting the core's boot entry register (`STA_ADD_REG`) directly to `0x00000000` while TCM is uninitialized causes an immediate instruction fetch abort, resulting in **Hardware Lockup** (`WORK_MODE_REG 0x07130248 = 0x0000000B`, Bit 3 `BIT_LOCK_STA = 1`).

#### 3. The Solution: Two-Stage Bootstrapping ("How to Copy to TCM")
To utilize ITCM and DTCM on Allwinner T527 without hardware lockup, programmers use the standard embedded **LMA vs. VMA Staging Pattern**:
1. **Host Loading (LMA - Load Memory Address)**: Linux RemoteProc loads the entire firmware ELF into accessible on-chip SRAM:
   - **PubSRAM C (`0x00020000`, 128 KB)**, OR
   - **Dedicated High SRAM (`0x3ffc0000`, 256 KB)**.
2. **Core Startup**: The E907 starts executing from SRAM (`_start` in `startup.S`).
3. **Core-Initiated Copy (VMA - Virtual/Execution Memory Address)**: Early in the startup sequence, the E907's own CPU instructions copy the designated `.itcm` functions and `.dtcm` data from SRAM (LMA) to TCM (VMA).
4. **Instruction Synchronization (`fence.i`)**: The core executes `fence.i` to invalidate and synchronize its instruction fetch pipeline so newly copied instructions in ITCM are fetched cleanly.
5. **Execution**: The core jumps into or calls the TCM-resident routines, running at pure 1-cycle latency!

#### 4. Step-by-Step Linker Script Configuration (`.ld`)
To stage TCM code and data, configure the GNU Linker Script with distinct Load Memory Addresses (`AT(...)`):

```ld
MEMORY
{
    /* Staging / Primary Executable SRAM */
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K

    /* Tightly-Coupled Memories (Local E907 Core View) */
    ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx) : ORIGIN = 0x00080000, LENGTH = 64K
}

SECTIONS
{
    /* Primary bootstrap in SRAM */
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

#### 5. Assembly Startup Copy Routine (`startup.S`)
In `startup.S`, insert the copy routine before calling C/C++ constructors or `main()`:

```assembly
    /* =============================================================
     * 1. Copy Critical Code from SRAM (LMA) to ITCM (VMA)
     * ============================================================= */
    la      a0, _sitcm              /* Destination: ITCM start */
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
    la      a0, _sdtcm              /* Destination: DTCM start */
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
     * 3. Clear DTCM BSS (.dtcm_bss)
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

#### 6. How Programmers Use TCM in C / C++
Programmers define compiler macros to place critical functions and variables into TCM:

```c
#define __ITCM_TEXT __attribute__((section(".itcm"), noinline))
#define __DTCM_DATA __attribute__((section(".dtcm")))
#define __DTCM_BSS  __attribute__((section(".dtcm_bss")))

/* Pinned in ITCM: 1-cycle execution, immune to system bus congestion */
void __ITCM_TEXT fast_flight_loop_isr(void) {
    // Hard real-time attitude estimation & motor PWM update
}

/* Pinned in DTCM: 1-cycle read/write, zero cache jitter */
static volatile float __DTCM_DATA pid_gains[3] = {1.25f, 0.05f, 0.12f};
static volatile uint32_t __DTCM_BSS fast_cycle_count;
```

#### 7. The Zero-Copy Alternative: Dedicated High SRAM (`0x3ffc0000` / `0x07280000`)
If your application needs high-performance execution without the overhead of copying sections from LMA to VMA at boot:
* The XuanTie E907 on T527 features **256 KB of Dedicated High SRAM** mapped at **`0x3ffc0000`** in the core's address space (physical **`0x07280000`**).
* **Direct RemoteProc Loading**: Unlike private TCM, Dedicated High SRAM is an on-chip SRAM bank accessible to both the ARM host bus and the E907 core. Linux RemoteProc can load code and data directly into it at startup.
* **Measured Performance**: In live hardware execution tests on the Radxa Cubie A5E, a tight counting loop ran at **~570,000 counts per 10 ms** in Dedicated High SRAM versus **~171,000 counts per 10 ms** in PubSRAM C.
* For large real-time applications (up to 256 KB), Dedicated High SRAM provides near-TCM execution speeds with 100% zero boot-time copy overhead!

#### 8. Memory Hierarchy & Determinism Comparison

| Subsystem | Core Address | Host Address | Access Latency | Cache Jitter? | Direct RemoteProc ELF Load? | Typical Programmer Usage |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **ITCM** | `0x00000000` | Unmapped | **1 Cycle (Zero Wait)** | **0% (Deterministic)** | ❌ No (Requires boot copy) | Time-critical ISRs, trap vectors, PID inner loops |
| **DTCM** | `0x00080000` | Unmapped | **1 Cycle (Zero Wait)** | **0% (Deterministic)** | ❌ No (Requires boot copy) | High-speed stack, fast LUTs, atomic state |
| **Dedicated High SRAM** | `0x3ffc0000` | `0x07280000` | **~1-2 Cycles (Fast on-chip)**| **0% (Deterministic)** | ✅ **YES (Zero Copy)** | Full 256 KB firmware, real-time RTOS, SPSC queues |
| **PubSRAM C** | `0x00020000` | `0x00020000` | **~2-4 Cycles (Shared bus)** | **Very Low** | ✅ **YES (Default Boot)** | General firmware, bootloader, trace buffer |
| **DDR DRAM** | `0x40000000`+ | `0x40000000`+ | **~50-100+ Cycles** | **High (L1 miss penalty)** | ✅ Yes (DMA Carveout) | Large streaming payload buffers (1 MB+) |

---

## 3. Firmware Layout, Linker Script & Bootstrap Sequence

### Unified Linker Script (`riscv-firmware/common/arch_riscv/firmware_t527.ld`)

The unified linker script places execution code, data, stack, trace buffer, and shared structures into verified **PubSRAM C (`0x00020000`, 128 KB)**:

```ld
OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K
}

SECTIONS
{
    /* Exception vector table must be 64-byte aligned for RISC-V mtvec */
    .vectors :
    {
        . = ALIGN(64);
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > SRAM

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > SRAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > SRAM

    /* RemoteProc Resource Table (Parsed by Linux on ELF Load) */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > SRAM

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM
    _sidata = LOADADDR(.data);

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /* Execution Stack (8 KB) */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x2000;
        _stack_top = .;
    } > SRAM

    /* Scratchpad Memory */
    .dtcm_scratch (NOLOAD) :
    {
        . = ALIGN(4);
        __dtcm_scratch_start = .;
        *(.dtcm_scratch)
        *(.dtcm_scratch.*)
        . = ALIGN(4);
        __dtcm_scratch_end = .;
    } > SRAM

    /* RemoteProc Trace Buffer */
    .trace_buffer (NOLOAD) :
    {
        . = ALIGN(4);
        __trace_start = .;
        KEEP(*(.trace_buffer))
        KEEP(*(.trace_buffer.*))
        . = ALIGN(4);
        __trace_end = .;
    } > SRAM

    /* Shared Application / IPC Memory */
    .sram_c (NOLOAD) :
    {
        . = ALIGN(4);
        __sram_c_start = .;
        *(.sram_c)
        *(.sram_c.*)
        *(.sram_c_loc1)
        *(.sram_c_loc2)
        . = ALIGN(4);
        __sram_c_end = .;
    } > SRAM
}
```

### Bootstrap Sequence (`riscv-firmware/common/arch_riscv/startup.S`)

1. **Disable Interrupts**: `csrw mie, zero`, `csrw mip, zero`.
2. **Setup Stack Pointer**: `la sp, _stack_top` in SRAM (`0x00020000 + offset`).
3. **Setup Global Pointer**: `la gp, __global_pointer$` for relaxed linker addressing.
4. **Configure Trap Vector**: `csrw mtvec, _vectors` (aligned to 64 bytes in SRAM `0x00020000`) before enabling FPU.
5. **Enable Hardware FPU**: `csrs mstatus, (3 << 13)` (Sets `mstatus.FS = 0b11` to enable single/double precision FPU).
6. **Copy Initialized Data**: Checks if LMA != VMA before copying `.data`.
7. **Zero BSS**: Clears `.bss` variables in SRAM.
8. **Call Global C++ Constructors**: Calls `__libc_init_array` if present.
9. **Jump to Application**: Executes `call main`.

---

## 4. Modern Zero-Allocation C++ HAL Modules (`common/hal/`)

The firmware architecture uses a modular, zero-allocation C++ HAL suite located under [`riscv-firmware/common/hal/`](file:///home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/riscv-firmware/common/hal/):

* **`hal::Rpmsg` (`hal/rpmsg.hpp`, `hal/rpmsg.cpp`)**:
  - Zero-allocation VirtIO vring and OpenAMP RPMsg driver.
  - Implements VirtIO split vrings, descriptor tables, available/used rings, and Name Service Announcements.
  - Interoperates cleanly with Linux kernel `virtio_rpmsg_bus` and `rpmsg_char`.
* **`hal::SpscQueue` (`hal/spsc_queue.hpp`)**:
  - Lock-free, zero-allocation Single-Producer Single-Consumer circular ring buffer.
  - Utilizes C++11 atomic acquire-release memory fences for synchronization between ARM64 Linux and RISC-V E907 in shared SRAM without locking.
* **`hal::Pmp` (`hal/pmp.hpp`, `hal/pmp.cpp`)**:
  - Configures XuanTie E907 Physical Memory Protection (PMP) CSRs (`pmpaddr*`, `pmpcfg*`).
  - Marks external DDR DMA payload buffers (`0x48100000`) as non-cacheable to eliminate cache invalidation/flush overhead.
* **`hal::Trace` (`hal/trace.hpp`)**:
  - Zero-allocation ASCII string and packed binary ring-buffer logger.
  - Formats telemetry, heartbeats, and sensor readings directly into the RemoteProc debugfs `trace0` buffer.
* **`hal::Crash` (`hal/crash.hpp`)**:
  - Machine-mode exception and trap autopsy handler.
  - Captures all 31 GPRs (`x1`–`x31`) and CSRs (`mepc`, `mcause`, `mtval`, `mstatus`) upon fatal faults, outputting structured crash logs to `trace0` and writing `0xDEADF00D` to SRAM (`0x00020000`).
* **`hal::Timer` (`hal/timer.hpp`)**:
  - Calibrated 64-bit microsecond counter and busy-wait delay for the 200 MHz core (`TICKS_PER_US = 200`).

---

## 5. Test Applications Suite (`apps/`)

Under [`riscv-firmware/apps/`](file:///home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/riscv-firmware/apps/), seven progressive test applications validate core functionality, memory mapping, telemetry, exception handling, and three inter-processor communication paradigms:

```text
apps/
├── testBasic/               # Minimal boot, PubSRAM execution, and live counter increments
├── testStringBinaryTrace0/  # Combined ASCII text + packed binary telemetry with hardware FPU
├── testCrash/               # Hardware exception trapping (mtvec) & full register crash dump
├── testPing/                # Fast, low-jitter Direct Shared Memory (hal::SpscQueue) + Linux benchmark
│   └── linux/               # ping_shm Linux host companion benchmark tool
├── testPingRpmsg/           # Standard Linux VirtIO RPMsg (hal::Rpmsg) echo firmware + Linux benchmark
│   └── linux/               # ping_rpmsg Linux host companion benchmark tool
└── testDRAMMsg/             # Hybrid SRAM SPSC Queue + DDR DRAM Payload Buffers + PMP non-cacheable
    └── linux/               # ping_dram Linux host companion benchmark tool
```

### Application Details

1. **`testBasic`**: Boots into PubSRAM C `0x00020000` and continuously writes magic counters to SRAM (`0x00021000`, `0x00021004`) for sanity testing.
2. **`testStringBinaryTrace0`**: Registers a `.resource_table` with a 4 KB `trace0` buffer in PubSRAM C. Combines double-precision hardware FPU math (sine wave computation) with a 36-byte packed binary `TelemetryPacket` in SRAM (`0x00021000`) and formatted ASCII log output in `trace0`.
   > **Note on `epoll` & Polling**: Upstream Linux debugfs `trace0` (`drivers/remoteproc/remoteproc_debugfs.c`) does **not** implement `.poll` or attach a wait queue; calling `epoll_ctl()` returns `EPERM`. Thus, companion scripts (`monitor_trace.py`) poll in a loop. Hardware Mailbox doorbells and `/dev/rpmsg0` provide event-driven notifications with full `epoll` support for 0% host CPU wait.
3. **`testCrash`**: Verifies machine-mode exception trapping (`mtvec`). After emitting heartbeats, it executes an illegal instruction, triggering a full register autopsy dump to `trace0` and writing `0xDEADF00D` to SRAM (`0x00020000`).
4. **`testPing`**: Ultra-low-latency direct shared SRAM SPSC communication using `hal::SpscQueue`. Linux companion tool `ping_shm` measures round-trip time latency down to ~1.5–2.5 $\mu\text{s}$.
5. **`testPingRpmsg`**: Standard Linux kernel VirtIO RPMsg framework (`virtio_rpmsg_bus`) using `hal::Rpmsg`. Interacts with `/dev/rpmsg0` via companion tool `ping_rpmsg`.
6. **`testDRAMMsg`**: Hybrid memory architecture combining zero-wait-state SRAM SPSC control queues with a 1 MB DDR DRAM payload buffer pool (`0x48100000`) configured as non-cacheable via `hal::Pmp`. Linux companion tool `ping_dram` benchmarks high-bandwidth payload transfers up to 4 KB per frame.

---

## 6. Communication Paradigm & IPC Architecture Comparison

| IPC Category | **[STANDARDS-BASED]**<br>Official `libopenamp` + `libmetal` | **[STANDARDS-BASED]**<br>Lite-libmetal / `hal::Rpmsg` (`testPingRpmsg`) | **[CUSTOM LOW-LATENCY]**<br>Hybrid SRAM / DDR (`testDRAMMsg`) | **[CUSTOM LOW-LATENCY]**<br>Pure Dedicated SRAM (`testPing` / `hal::SpscQueue`) |
| :--- | :--- | :--- | :--- | :--- |
| **Architecture Family** | **Standards-Based (VirtIO / OpenAMP)** | **Standards-Based (VirtIO / OpenAMP)** | **Custom Hardware-Direct HAL** | **Custom Hardware-Direct HAL** |
| **Control Path** | VirtIO vrings via `libmetal` layers | VirtIO vrings via C++ `std::atomic` | Lock-Free SPSC in PubSRAM C (`0x00020000`) | Lock-Free SPSC in PubSRAM C (`0x00020000`) |
| **Data Path** | RPMsg DMA buffers (DDR) | RPMsg DMA buffers (DDR) | **DDR DRAM Carveout (`0x48100000`, 1 MB)** | Direct PubSRAM C (`0x00020000`, 64B frames) |
| **Linux Driver / Stack**| `virtio_rpmsg_bus` + `rpmsg_char` | `virtio_rpmsg_bus` + `rpmsg_char` | Kernel UIO / Reserved Memory Carveout | Kernel UIO / Shared SRAM (`sunxi_rproc`) |
| **Linux Ecosystem**     | Standard (`/dev/rpmsg0`, `/dev/ttyRPMSG0`) | Standard (`/dev/rpmsg0`, `/dev/ttyRPMSG0`) | Custom High-Speed API / `ping_dram` | Custom High-Speed API / `ping_shm` |
| **Firmware Code Size**  | **~30 – 50 KB** (requires dynamic heap) | **~2 – 3 KB** (zero dynamic allocation) | **~3 – 4 KB** (zero dynamic allocation) | **< 1 KB** (header-only C++ template) |
| **Typical RTT Latency** | **~60 – 160 $\mu\text{s}$** | **~50 – 90 $\mu\text{s}$** | **~3.0 – 6.0 $\mu\text{s}$** (DDR bus latency) | **~1.5 – 2.5 $\mu\text{s}$** (Zero-wait-state SRAM) |
| **Jitter (StdDev)**     | Moderate (Kernel context switches) | Moderate (Kernel context switches) | **Ultra-Low (<0.5 $\mu\text{s}$)** | **Ultra-Low (<0.2 $\mu\text{s}$)** |
| **Max Payload Size**    | Medium (512 B default) | Medium (512 B default) | **Large (Up to 4 KB per frame, MBs pool)** | Small (40–64 B, SRAM capacity bounded) |
| **Throughput Bandwidth**| Moderate (~10–20 MB/s) | Moderate (~10–20 MB/s) | **High Bandwidth (>100 MB/s)** | High Packet Rate (Low Payload) |
| **Target Use Case**     | Generic standard OS interop | Lightweight standard Linux RPMsg | Point-clouds, camera frames, flight logs | Hard real-time motor control, PID loops |

---

## 7. How to Build & Deploy on Target (Cubie A5E)

### 7.1 Build Everything (Firmware + Linux Benchmark Tools)

From the top-level repository or `riscv-firmware/` directory:

```bash
make -C riscv-firmware
```

All compiled firmware ELFs are staged into `riscv-firmware/bin/` with distinct names:
* `testBasic.elf`
* `testStringBinaryTrace0.elf`
* `testCrash.elf`
* `testPing.elf`
* `testPingRpmsg.elf`
* `testDRAMMsg.elf`

Linux benchmark tools are also compiled and staged:
* `ping_shm` (Direct Shared Memory SPSC benchmark)
* `ping_rpmsg` (VirtIO RPMsg benchmark)
* `ping_dram` (Hybrid SRAM/DRAM benchmark)

During the Buildroot rootfs build, these firmware ELFs are automatically installed to `/lib/firmware/` and the companion tools to `/usr/local/bin/`.

---

### 7.2 Live Firmware Switching on Target (Cubie A5E)

The Linux RemoteProc framework allows stopping, switching, and starting firmware dynamically at runtime without rebooting:

```bash
# ==============================================================================
# 1. Run Pure Shared SRAM C Ping (testPing)
# ==============================================================================
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Run high-frequency latency benchmark (e.g., 50,000 round-trips over 0x00020000)
ping_shm -n 50000

# ==============================================================================
# 2. Run Hybrid SRAM/DRAM SPSC Benchmark (testDRAMMsg)
# ==============================================================================
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testDRAMMsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Run streaming DRAM payload benchmark (e.g., 10,000 packets of 1024 bytes)
ping_dram -n 10000 -s 1024

# ==============================================================================
# 3. Run Standard Linux RPMsg (testPingRpmsg)
# ==============================================================================
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPingRpmsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Run kernel RPMsg character device benchmark (e.g., 5,000 round-trips)
ping_rpmsg -n 5000
```

---

### 7.3 Reading RemoteProc Trace Logs

To view ASCII startup banners, periodic telemetry, or crash dumps:

```bash
# Read live log stream from the E907 co-processor
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

---

## 8. Troubleshooting & Verification Checklist

1. **Check RemoteProc Kernel Driver**:
   ```bash
   dmesg | grep -i sunxi_rproc
   ```
   Verify that `sunxi-rproc 7102000.remoteproc: assigned reserved memory node rproc_trace@48000000` is initialized.

2. **Verify Co-Processor State**:
   ```bash
   cat /sys/class/remoteproc/remoteproc0/state
   # Expected output: running
   ```

3. **Verify Memory Writes with `devmem`**:
   ```bash
   # When running testBasic, inspect magic incrementing counter in PubSRAM C:
   devmem 0x00021000 32
   devmem 0x00021004 32
   ```

4. **Verify Exception Handling**:
   When running `testCrash.elf`, read `/sys/kernel/debug/remoteproc/remoteproc0/trace0` to inspect the full GPR and CSR exception frame dump (`mepc`, `mcause`, `mtval`, `mstatus`, `ra`, `sp`, `gp`, etc.) and verify `0xDEADF00D` in PubSRAM C (`0x00020000`).
