# RISC-V Co-Processor Firmware & Test Suite Guide

This repository contains bare-metal firmware, runtime drivers, test applications, and Linux host companion tools for the **XuanTie E907 RISC-V Co-Processor (up to 200 MHz)** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**.

---

## 1. Hardware Architecture (Allwinner T527 XuanTie E907)

```text
+-----------------------------------------------------------------------------------------+
|                                    ALLWINNER T527 (sun55i)                              |
|                                                                                         |
|  +-------------------------------------+   +-----------------------------------------+  |
|  |             CPUX Cluster            |   |               Co-Processors             |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | 8x ARM Cortex-A55 @ 1.80 GHz  |  |   |  | Cadence Tensilica HiFi4 Audio DSP |  |  |
|  |  | (Main Linux Kernel / OS)      |  |   |  | Clock: 600 MHz (PLL_AUDIO/PLL_DSP)|  |  |
|  |  +-------------------------------+  |   |  +-----------------------------------+  |  |
|  |  | DynamIQ Shared Unit (DSU)     |  |   |  +-----------------------------------+  |  |
|  |  | L3 Cache: 512 KB              |  |   |  | XuanTie E907 RISC-V Co-Processor  |  |  |
|  |  +-------------------------------------+   |  | (RV32IMAFDC + Double FPU + DSP)   |  |  |
|  |                                            |  | Clock: Up to 200 MHz (MCU_PRCM)   |  |  |
|  +-------------------------------------+   |  +-----------------------------------+  |  |
|  |             NPU Engine              |   |  +-----------------------------------+  |  |
|  |  - 2.0 TOPS VIP9000 (0x07122000)    |   |  | Hardware Message Box (Doorbell)   |  |  |
|  +-------------------------------------+   +-----------------------------------------+  |
|                                                                                         |
|  +-----------------------------------------------------------------------------------+  |
|  |                           Memory Hierarchy & Interconnect                         |  |
|  |  - 128 KB Shared PubSRAM C (0x00020000) [Default RemoteProc Firmware & Vector]    |  |
|  |  - 256 KB Dedicated High SRAM (0x3ffc0000 Core / 0x07280000 Host) [Zero-Wait]    |  |
|  |  - 4 KB RISC-V CFG Control Block (0x07130000) [STA_ADD_REG @ 0x204, WORK_MODE]   |  |
|  |  - 4 KB DDR RemoteProc Trace Carveout (0x48000000)                                |  |
|  |  - 1 MB DDR DMA Payload Pool (0x48100000)                                         |  |
|  |  - Up to 4 GiB LPDDR4/4X System RAM (0x40000000)                                  |  |
|  +-----------------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------------+
```

The auxiliary co-processor on the Allwinner T527 is an enterprise-grade 32-bit RISC-V core:
* **Architecture**: **RV32IMAFDC** (RV32GC)
  - **I**: 32 standard 32-bit General Purpose Registers (`x0`–`x31`).
  - **M**: Hardware Integer Multiplication and Division.
  - **A**: Atomic Memory Operations (`lr.w`, `sc.w`, `amo*`).
  - **F**: Single-precision IEEE 754 Hardware Floating Point Unit.
  - **D**: Double-precision IEEE 754 Hardware Floating Point Unit (`f0`–`f31` 64-bit float registers).
  - **C**: Compressed 16-bit instructions for high code density.
  - **DSP / RVP**: Packed SIMD & DSP extensions for accelerated digital signal processing and audio/sensor filtering.
  - **_zicsr**: Standard Control & Status Register manipulation.
* **Pipeline & Speed**: 5-stage dual-issue in-order pipeline operating at **up to 200 MHz** (managed by `mcu_ccu` @ `0x07102000`). *(Note: The companion Cadence Tensilica HiFi4 Audio DSP on T527 operates at 600 MHz).*
* **Toolchain / ABI**: Target `-march=rv32imafdc_zicsr_zifencei -mabi=ilp32d -mcmodel=medany`.

---

## 2. Memory Map of XuanTie E907 on Allwinner T527 (Linux Perspective)

### 2.1 Memory Subsystem Mapping

| Memory Region | Linux Host (ARM64) Physical Address | E907 RISC-V Core Address | Size | Latency & Usage |
| :--- | :--- | :--- | :--- | :--- |
| **Shared PubSRAM C** | **`0x00020000`** | **`0x00020000`** | **128 KB** | 1:1 Identity mapped; default boot memory (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`) |
| **Dedicated MCU SRAM (R_SRAM)** | **`0x07280000`** | **`0x3ffc0000`** | **256 KB** | Zero-wait-state high-performance dedicated SRAM space 0 (3x faster execution) |
| **RISC-V CFG Control Block** | **`0x07130000`** | **`0x07130000`** | **4 KB** | Hardware registers: `0x0000` (`VER_REG`), `0x0204` (`STA_ADD_REG` Boot vector), `0x0248` (`WORK_MODE_REG`) |
| **DDR Trace Buffer (`trace0`)** | **`0x48000000`** | **`0x48000000`** | **4 KB** | RemoteProc ASCII & binary log buffer (`/sys/.../trace0`) |
| **DDR DRAM DMA Carveout** | **`0x48100000`** | **`0x48100000`** | **1 MB** | PMP non-cacheable high-bandwidth payload pool (`testDRAMMsg`) |

> [!WARNING]
> ### CRITICAL HARDWARE & SECURITY WARNING: DO NOT USE SRAM A2 (`0x00040000`) FOR RISC-V
> **SRAM A2 (`0x00040000`–`0x00073FFF`) is strictly reserved for Secure World / TrustZone and must NEVER be mapped or written to by RISC-V firmware or Linux RemoteProc:**
> 1. **ARM TrustZone Secure World (TF-A / BL31 / PSCI)**: On Allwinner ARM64 SoCs, `SRAM A2` is the **Secure SRAM (CPUS SRAM)**. ARM Trusted Firmware (TF-A BL31) runs at Secure EL3 and places its secure monitor runtime data, secure stacks, and **PSCI 1.1 CPU power-management state machines** in `SRAM A2`. The hardware TrustZone Memory Adapter (TZMA) firewalls `SRAM A2` for **Secure Access Only**; any non-secure write attempt by Linux or an external core triggers an immediate **hardware Synchronous External Abort** (bus fault).
> 2. **A733 Hardware Power Management Collision**: On the A733 SoC, `SRAM A2` is hardwired in silicon as the boot address of the Always-On E902 CPUS core running vendor **`scp.fex`**. Overwriting `0x00040000` destroys `scp.fex` and powers off system PMIC voltage rails.

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

### 2.4 Visual Memory Architecture Diagram

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

## 3. Test Applications Suite (`apps/`) & Performance Roadmap

The firmware test suite follows a progressive **"Walk -> Run"** architecture, starting with basic bring-up sanity and trace logging before advancing to high-throughput, low-latency IPC:

```text
apps/
├── [PHASE 1: WALK - BOOT, TRACE & TELEMETRY]
│   ├── testBasic/               # Minimal boot in PubSRAM C (0x00020000) & live counter increment
│   ├── testStringBinaryTrace0/  # SRAM trace0 buffer, Mixed ASCII text + packed binary telemetry + FPU
│   └── testCrash/               # Hardware exception trapping (mtvec) & full register crash dump
│
└── [PHASE 2: RUN - GETTING FASTER CODE & REAL-TIME IPC]
    ├── testPing/                # Ultra-low-latency (~2us) Direct SRAM SPSC Queue + Linux benchmark
    │   └── linux/               # ping_shm Linux host companion benchmark tool
    ├── testDRAMMsg/             # High-throughput Hybrid SRAM Control + DDR DRAM DMA Payload Buffers
    │   └── linux/               # ping_dram Linux host companion benchmark tool
    └── testPingRpmsg/           # Standards-based Linux VirtIO RPMsg (hal::Rpmsg) + Mailbox Doorbells
        └── linux/               # ping_rpmsg Linux host companion benchmark tool
```

---

### App 1: `testBasic`
* **Purpose**: Basic bring-up, memory sanity, and linker-script layout validation.
* **Functionality**:
  - Boots into PubSRAM C `0x00020000`, configures stack in SRAM (`0x00022000`).
  - Maps variables into dedicated linker sections (`.sram_c_loc1`, `.sram_c_loc2`) without hardcoded pointer macros.
  - Continuously increments counters so host Linux can verify life via `devmem 0x00021000 32` or `devmem 0x00021004 32`.

---

### App 2: `testStringBinaryTrace0` (Mixed String & Binary Telemetry)
* **Purpose**: Demonstrates structured telemetry streaming over `trace0` combining formatted ASCII strings with packed binary structures and hardware FPU computation.
* **Functionality**:
  - Declares `.resource_table` section exporting `trace0` buffer in PubSRAM C (`0x00020000`).
  - Completely eliminates DDR caching issues—trace writes are immediately visible to Linux without software cache flushes.
  - Utilizes single-precision (`float`) and double-precision (`double`) hardware FPU math (sine wave computation).
  - Populates a 36-byte packed binary `TelemetryPacket` in SRAM (`.sram_c` @ `0x00021000`).
  - Interleaves three data streams directly into the `trace0` buffer:
    1. **ASCII String Log (`STRING:`)**: Human-readable log line with formatted float values.
    2. **Framed Binary Struct (`BINARY:`)**: Raw packed binary struct (`TelemetryPacket`) for high-speed programmatic ingestion.
    3. **Hex Dump (`HEXDUMP:`)**: Terminal-inspectable formatted hex dump of the binary packet.

#### Standard RemoteProc Python Monitor (`apps/testStringBinaryTrace0/monitor_trace.py`)
Run the standard debugfs monitor on the Linux host to stream and decode the mixed trace:

```bash
python3 apps/testStringBinaryTrace0/monitor_trace.py
```

Example decoded output:
```text
=== RemoteProc Trace0 Live Monitor (T527 E907) ===
Target: /sys/kernel/debug/remoteproc/remoteproc0/trace0 | Packet Size: 36 bytes | Poll: 50ms

[ASCII]   [TELM #1] Accel: (+0.015, -0.008, +9.811) | FPU Sin: +0.0998 | SRAM: 0x00021000
[STRUCT]  Seq #1     | Up: 500   ms | Accel: (+0.015, -0.008, +9.811) | FPU Sin: +0.0998 | Csum: 0xA5A4
[HEXDUMP]
         0x00000000: 4D 4C 45 54 01 00 00 00 F4 01 00 00 7B 14 AE 3C |MLE.........{..<|
         0x00000010: 2F DD 03 BC 25 1B 1D 41 84 FC 3B 21 00 00 00 00 |/...%..A..;!....|
         0x00000020: A4 A5 AA 55                                     |...U            |
```

#### Lite / Fast Direct SRAM Python Monitor (`apps/testStringBinaryTrace0/fast_sram_telemetry.py`)
For ultra-high-rate telemetry (>1,000 Hz) bypassing the kernel filesystem layer, `fast_sram_telemetry.py` reads SRAM (`0x00021000`) directly for zero-copy polling:

```bash
sudo python3 apps/testStringBinaryTrace0/fast_sram_telemetry.py
```

Example high-rate output:
```text
=== Direct Zero-Copy SRAM Telemetry Reader (Lite/Fast) ===
Physical Target: 0x00021000 | Packet Size: 36 bytes
Mode: Direct physical mmap (bypasses debugfs / kernel filesystem layers)

[SRAM-DIRECT] Seq #124   | Up: 62000 ms | Accel: (+1.860, -0.992, +9.814) | FPU Sin: +0.1542 | Rate: 1042.5 Hz
[SRAM-DIRECT] Seq #125   | Up: 62500 ms | Accel: (+1.875, -1.000, +9.809) | FPU Sin: +0.0544 | Rate: 1040.1 Hz
```

> [!NOTE]
> **Why `epoll` Cannot Be Used on `trace0` (The Polling Trade-Off)**:
> In the Linux kernel RemoteProc subsystem (`drivers/remoteproc/remoteproc_debugfs.c`), `trace0` is a simple debugfs file implementing only `.read`, `.open`, and `.llseek`. It has **no `.poll` method and no wait-queue**; attempting to register it with `epoll_ctl()` immediately returns `EPERM` (*Operation not permitted*).
>
> Consequently, `monitor_trace.py` must **poll** in user-space, consuming host CPU cycles. This is the baseline in Phase 1 ("Walk"). Phase 2 ("Run") introduces hardware Mailbox doorbells and `/dev/rpmsg0` where the kernel's `virtio_rpmsg_bus` implements `.poll`, enabling true event-driven `epoll` with **0% idle CPU utilization**.

---

### App 3: `testCrash`
* **Purpose**: Fault simulation, trap vector validation, and crash autopsy reporting via RemoteProc.
* **Functionality**:
  - Configures `mtvec` to custom exception trap handler.
  - Emits 3 normal heartbeats before intentionally triggering an illegal instruction trap.
  - Trap handler captures `mepc`, `mcause`, `mtval`, `mstatus`, and full GPR dump (`ra`, `sp`, `gp`, `a0`..`a7`, `t0`..`t6`, `s0`..`s11`).
  - Writes fatal signature `0xDEADF00D` to SRAM (`0x00020000`).
  - Formats an exhaustive crash autopsy and dumps it directly into `trace0`:
    ```text
    ################################################################
      FATAL HARDWARE EXCEPTION DETECTED ON XUANTIE E907 RISC-V CORE 
    ################################################################
      Cause Name : Illegal instruction
      mcause     : 0x00000002
      mepc (PC)  : 0x00000188
      mtval      : 0x00000000
      mstatus    : 0x00001800

    --- General Purpose Register (GPR) Dump ---
      ra (x1) = 0x00020184  sp (x2) = 0x00022FF0  gp (x3) = 0x00020800
      ...
    ```

---

### App 4: `testPing` (Fast Direct Shared Memory / Lite-libmetal UIO Style)
* **Purpose**: Ultra-low-latency, zero-copy, deterministic inter-processor communication.
* **Functionality**:
  - Direct lock-free shared SRAM channel (`ShmPingChannel` in PubSRAM C / dedicated MCU SRAM).
  - Dual operational modes:
    1. **Event-Driven UIO Doorbell Mode (Recommended)**: Enabled via `cubie-a5e-uio` overlay in `/boot/config.txt`. Converts the hardware Mailbox (`0x03003000`) into a generic UIO device (`/dev/uio0`). Linux host blocks asynchronously on `select.epoll()` with **0% idle CPU burn**; XuanTie E907 pulses GIC SPI 147 interrupt to wake host.
    2. **Direct Memory Polling Baseline**: Reads SRAM directly via kernel UIO mapping and polls on memory flags for theoretical raw bus latency measurements.
* **Linux Companion Tools**:
  - **`apps/testPing/linux/ping_uio.py`**: Event-driven Python Lite-libmetal client using `select.epoll()`. Maps `map0` (Mailbox MMIO) and `map1` (MCU SRAM) directly from `/dev/uio0` (no root privileges required).
  - **`apps/testPing/linux/ping_shm`**: C++ high-precision latency benchmarking tool using `clock_gettime(CLOCK_MONOTONIC_RAW)`.
  - Computes min, avg, max latency, jitter (standard deviation), percentiles (p50, p90, p99, p99.9), and message throughput.

```bash
# Mode 1: Event-driven UIO Doorbell (0% idle CPU burn via epoll)
python3 apps/testPing/linux/ping_uio.py -n 50000

# Mode 2: Direct Shared SRAM memory-polling baseline
sudo ./apps/testPing/linux/ping_shm -n 100000
```

---

### App 5: `testPingRpmsg` (Standard Linux VirtIO RPMsg)
* **Purpose**: Standard Linux kernel RPMsg framework communication (`virtio_rpmsg_bus`).
* **Functionality**:
  - RemoteProc resource table with `RSC_VDEV` (VirtIO ID 7) and 2 vrings (16 descriptors each).
  - Announces Name Service endpoint `"rpmsg-ping-channel"` (address 1024).
  - Processes incoming RPMsg packets from Linux `/dev/rpmsg0` and responds with pong packets.
* **Linux Companion Tool**: `apps/testPingRpmsg/linux/ping_rpmsg`
  - Connects to `/dev/rpmsg0` or creates endpoint via `/dev/rpmsg_ctrl0`.
  - Evaluates standard kernel RPMsg driver latency and throughput.

```bash
# Run 1,000 iterations over Linux RPMsg
sudo ./apps/testPingRpmsg/linux/ping_rpmsg -n 1000
```

---

### App 6: `testDRAMMsg` (Hybrid SRAM SPSC Queue / DDR DRAM Payload Buffers)
* **Purpose**: High-throughput message streaming moving payload buffers to DDR DRAM while keeping SPSC control structures in ultra-low-latency SRAM.
* **Architecture**:
```text
+-----------------------------------------------------------------------------+
|                          HYBRID MEMORY IPC ARCHITECTURE                     |
|                                                                             |
|   +---------------------------------------------------------------------+   |
|   |         SHARED PUBSRAM C (0x00020000) - CONTROL PATH                |   |
|   |  - SPSC Head & Tail Pointers (Atomic single-word updates)           |   |
|   |  - Producer/Consumer Doorbells & Monotonic Sequence Counters        |   |
|   |  - 16-slot TX/RX Descriptor Rings (Holds DRAM Buffer Offsets & Len) |   |
|   |  - Zero-wait-state access for sub-microsecond descriptor exchanges  |   |
|   +---------------------------------------------------------------------+   |
|                                     │ (Payload pointers & offsets)          |
|                                     ▼                                       |
|   +---------------------------------------------------------------------+   |
|   |             DDR DRAM CARVEOUT (0x48100000) - DATA PATH              |   |
|   |  - Non-Cacheable DMA Carveout / Reserved Memory Window              |   |
|   |  - 16x Host->RISC-V Payload Buffers (Up to 4 KB each)               |   |
|   |  - 16x RISC-V->Host Payload Buffers (Up to 4 KB each)               |   |
|   |  - PMP / XuanTie Cache attributes configured for zero cache stalls  |   |
|   +---------------------------------------------------------------------+   |
+-----------------------------------------------------------------------------+
```
* **Functionality**:
  - Control block (`DramSpscControlBlock` @ `0x00020000`) in fast shared PubSRAM C.
  - 1 MB payload buffer pool in DDR DRAM Carveout (`0x48100000`).
  - Configures RISC-V Physical Memory Protection (PMP) and XuanTie Cache maintenance (`mhcr`, `mcor`, `dcache.iva`, `dcache.cpa`) for DMA-coherent uncached/strongly-ordered access.
* **Linux Companion Tool**: `apps/testDRAMMsg/linux/ping_dram`
  - Measures throughput (MB/sec), latency, and jitter for variable payload sizes (64B to 4096B).

```bash
# Run 10,000 iterations with 1024-byte payloads over DDR DRAM
sudo ./apps/testDRAMMsg/linux/ping_dram -n 10000 -s 1024
```

---

## 4. Communication Paradigm & IPC Architecture Comparison

| IPC Category | **[STANDARDS-BASED]**<br>Official `libopenamp` + `libmetal` | **[STANDARDS-BASED]**<br>Lite-libmetal / `hal::Rpmsg` (`testPingRpmsg`) | **[CUSTOM LOW-LATENCY]**<br>Hybrid SRAM / DDR (`testDRAMMsg`) | **[CUSTOM LOW-LATENCY]**<br>Dedicated MCU SRAM + UIO (`testPing` / `hal::SpscQueue`) |
| :--- | :--- | :--- | :--- | :--- |
| **Architecture Family** | **Standards-Based (VirtIO / OpenAMP)** | **Standards-Based (VirtIO / OpenAMP)** | **Custom Hardware-Direct HAL** | **Custom Hardware-Direct HAL** |
| **Control Path** | VirtIO vrings via `libmetal` layers | VirtIO vrings via C++ `std::atomic` | Lock-Free SPSC in PubSRAM C (`0x00020000`) | Lock-Free SPSC in Dedicated MCU SRAM |
| **Data Path** | RPMsg DMA buffers (DDR) | RPMsg DMA buffers (DDR) | **DDR DRAM Carveout (`0x48100000`, 1 MB)** | MCU Dedicated SRAM (64B frames) |
| **Linux Driver / Stack**| `virtio_rpmsg_bus` + `rpmsg_char` | `virtio_rpmsg_bus` + `rpmsg_char` | Kernel UIO / Reserved Memory Carveout | `uio_pdrv_genirq` (`/dev/uio0`) |
| **Linux Ecosystem**     | Standard (`/dev/rpmsg0`, `/dev/ttyRPMSG0`) | Standard (`/dev/rpmsg0`, `/dev/ttyRPMSG0`) | Custom High-Speed API / `ping_dram` | Event-driven `ping_uio.py` (`select.epoll()`) / `ping_shm` |
| **Firmware Code Size**  | **~30 – 50 KB** (requires dynamic heap) | **~2 – 3 KB** (zero dynamic allocation) | **~3 – 4 KB** (zero dynamic allocation) | **< 1 KB** (header-only C++ template) |
| **Typical RTT Latency** | **~60 – 160 $\mu\text{s}$** | **~50 – 90 $\mu\text{s}$** | **~3.0 – 6.0 $\mu\text{s}$** (DDR bus latency) | **~1.5 – 2.5 $\mu\text{s}$** (Zero-wait-state SRAM) |
| **Jitter (StdDev)**     | Moderate (Kernel context switches) | Moderate (Kernel context switches) | **Ultra-Low (<0.5 $\mu\text{s}$)** | **Ultra-Low (<0.2 $\mu\text{s}$)** |
| **Max Payload Size**    | Medium (512 B default) | Medium (512 B default) | **Large (Up to 4 KB per frame, MBs pool)** | Small (40–64 B, SRAM capacity bounded) |
| **Throughput Bandwidth**| Moderate (~10–20 MB/s) | Moderate (~10–20 MB/s) | **High Bandwidth (>100 MB/s)** | High Packet Rate (Low Payload) |
| **Target Use Case**     | Generic standard OS interop | Lightweight standard Linux RPMsg | Point-clouds, camera frames, flight logs | Hard real-time motor control, PID loops |

---

## 5. How to Build & Deploy on Target (Cubie A5E)

### Automated RootFS Installation During Build

During a standard Buildroot build or via `push-riscv-firmware.sh`, all firmware ELFs, host benchmark binaries, and Python companion scripts are automatically packaged directly into the target root filesystem (`rootfs.ext4` / `rootfs.tar`):

| Component | Target RootFS Location | Description |
| :--- | :--- | :--- |
| **Firmware Suite** | `/lib/firmware/*.elf` | All 8 compiled XuanTie E907 bare-metal ELFs |
| **Default Active Firmware** | `/lib/firmware/riscv-firmware.elf` | Bootstrapped at boot by `/etc/init.d/S60riscv` |
| **Host Python Trace Monitor** | `/usr/bin/monitor_trace.py` | RemoteProc `trace0` debugfs mixed ASCII/Binary decoder |
| **Host Python Direct Poller** | `/usr/bin/fast_sram_telemetry.py` | Direct zero-copy physical SRAM reader (>1 kHz) |
| **Event-Driven UIO Client** | `/usr/bin/ping_uio.py` | Lite-libmetal Python client using `select.epoll()` on `/dev/uio0` (0% CPU) |
| **SRAM Ping Benchmark** | `/usr/bin/ping_shm` | Shared memory lock-free SPSC latency benchmark tool |
| **Python RPMsg Benchmark** | `/usr/bin/ping_rpmsg.py` | Event-driven Python RPMsg round-trip test tool |
| **RPMsg Ping Benchmark** | `/usr/bin/ping_rpmsg` | Standard Linux `/dev/rpmsg` round-trip test tool (C++) |
| **DDR DRAM Ping Benchmark** | `/usr/bin/ping_dram` | Hybrid SRAM control / DDR payload throughput benchmark |

---

### Hot-Sync & Deploy Helper Script

To recompile all firmware and push to rootfs-overlay, active buildroot directories, and optionally hot-deploy to a running board over SSH:

```bash
# Sync locally to rootfs overlay and active buildroot target:
./project-cubie-a5e/scripts/push-riscv-firmware.sh

# Or hot-deploy directly over the network to the board IP:
./project-cubie-a5e/scripts/push-riscv-firmware.sh 192.168.1.150 testStringBinaryTrace0.elf
```

---

### Dynamic IPC Paradigm Selection via `/boot/config.txt` (Raspberry Pi Style)

On the Cubie A5E and Cubie A7A, you do not need to rebuild monolithic device trees or compile U-Boot scripts to change peripheral and IPC bindings. Edit `/boot/config.txt` directly on the target or by mounting the SD card on your development workstation:

```ini
# /boot/config.txt - Hardware & IPC Overlay Configuration

# ------------------------------------------------------------------------------
# Paradigm A: Standard Linux VirtIO RPMsg (testPingRpmsg)
# ------------------------------------------------------------------------------
dtoverlay=cubie-a5e-flight-stack

# ------------------------------------------------------------------------------
# Paradigm B: Hard Real-Time Lite-libmetal UIO Doorbell (testPing / ping_uio.py)
# ------------------------------------------------------------------------------
# dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio

# Optional: Real-time CPU core isolation (removes OS jitter on CPU 7):
extra_bootargs=isolcpus=7 nohz_full=7 rcu_nocbs=7
```

U-Boot automatically loads `/boot/config.txt`, parses `dtoverlay`, resolves `.dtbo` extensions, expands the FDT buffer (`fdt resize 65536`), and sequentially applies each overlay dynamically.

---

### Live Firmware Switching on Target (Cubie A5E)

Once logged into the board (via serial or SSH), all tools and ELFs are in your system `$PATH`:

```bash
# 1. Run Lite-libmetal Shared SRAM Ping (testPing)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Mode A: Event-driven UIO Doorbell (0% idle CPU burn via epoll on /dev/uio0)
# (Requires 'cubie-a5e-uio' overlay in /boot/config.txt)
ping_uio.py -n 50000

# Mode B: Direct Shared SRAM memory-polling baseline
ping_shm -n 50000

# 2. Run Hybrid SRAM/DRAM SPSC Benchmark (testDRAMMsg)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testDRAMMsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
ping_dram -n 10000 -s 1024

# 3. Run Standard Linux RPMsg (testPingRpmsg)
# (Requires standard RPMsg mailbox binding in /boot/config.txt)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPingRpmsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
ping_rpmsg.py -n 5000
# Or C++ tool:
ping_rpmsg -n 5000

# 4. Run Mixed String & Binary Telemetry (testStringBinaryTrace0)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testStringBinaryTrace0.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Stream and decode via standard RemoteProc debugfs trace:
monitor_trace.py

# Or high-rate zero-copy direct SRAM reader:
fast_sram_telemetry.py
```
