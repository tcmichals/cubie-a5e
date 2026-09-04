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
|  |  - RemoteProc Kernel Driver       |   |    @ 600 MHz                              |  |
|  +-----------------+-----------------+   +---------------------+---------------------+  |
|                    │                                           │                        |
|                    ▼                                           ▼                        |
|  +-----------------------------------------------------------------------------------+  |
|  |               SHARED SYSTEM INTERCONNECT & ON-CHIP SRAM BUS                       |  |
|  |  - 64 KB ITCM (0x00000000) & 64 KB DTCM (0x00080000) [E907 Zero-Wait-State Local]  |  |
|  |  - 208 KB Shared SRAM A2 (0x00040000) [Zero-Wait-State Low-Latency Control & IPC]  |  |
|  |  - 256 KB Dedicated MCU SRAM C (0x07130000)                                       |  |
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
* **TCM (Tightly Coupled Memory):** Equipped with **64 KB ITCM** (Instruction) and **64 KB DTCM** (Data), providing zero-wait-state access (1-cycle latency) for 100% deterministic execution of critical interrupts and control loops.
* **Toolchain / ABI:** Target `-march=rv32imafdc_zicsr_zifencei -mabi=ilp32d -mcmodel=medany`.

---

## 2. Memory Map of XuanTie E907 on Allwinner T527 (Linux Perspective)

### 2.1 Memory Subsystem Mapping

| Memory Region | Linux Host (ARM64) Physical Address | E907 RISC-V Core Address | Size | Latency & Usage |
| :--- | :--- | :--- | :--- | :--- |
| **Instruction TCM (ITCM)** | **`0x07110000`** | **`0x00000000`** | **64 KB** | Zero-wait-state instruction execution (`.text`, `.vectors`) |
| **Data TCM (DTCM)** | **`0x07120000`** | **`0x00080000`** | **64 KB** | Zero-wait-state data, stack (`.stack`), and `.resource_table` |
| **Shared System SRAM A2** | **`0x00040000`** | **`0x00040000`** | **208 KB** | 1:1 Identity mapped; ultra-low-latency direct SPSC IPC (`testPing`) |
| **Dedicated MCU SRAM (SRAM C)** | **`0x07130000`** | **`0x07130000`** | **256 KB** | 1:1 Identity mapped; fast scratchpad / DSP / MCU shared memory |
| **Reserved R_SRAM** | **`0x07280000`** | **`0x07280000`** | **256 KB** | Standby / system power-management SRAM |
| **DDR Trace Buffer (`trace0`)** | **`0x48000000`** | **`0x48000000`** | **4 KB** | RemoteProc ASCII & binary log buffer (`/sys/.../trace0`) |
| **DDR DRAM DMA Carveout** | **`0x48100000`** | **`0x48100000`** | **1 MB** | PMP non-cacheable high-bandwidth payload pool (`testDRAMMsg`) |

### 2.2 Control, Peripheral & Inter-Core Registers

| Peripheral Block | Linux Host Physical Address | E907 RISC-V Address | Description & Hardware Usage |
| :--- | :--- | :--- | :--- |
| **MCU CCU & Core Control** | **`0x07102000`** | **`0x07102000`** | Co-processor clock gate (`0x07102000`), reset (`0x07102004`), boot entry vector register (`0x07102204`) |
| **Main SoC CCU** | **`0x02001000`** | **`0x02001000`** | Root DSP/Co-processor clock gate (`0x02001c70`) |
| **Hardware MSGBOX (Mailbox)** | **`0x03003000`** | **`0x03003000`** | Hardware doorbell FIFO: <br>• **Channel 0**: RISC-V $\rightarrow$ Linux (GIC SPI 147)<br>• **Channel 1**: Linux $\rightarrow$ RISC-V (PLIC IRQ 25) |
| **Main PIO (GPIO B–K)** | **`0x02000000`** | **`0x02000000`** | 1:1 mapped GPIO pin control registers |
| **UART0 (Debug Console)** | **`0x02500000`** | **`0x02500000`** | Shared serial console |
| **UART2 (Co-processor Port)** | **`0x02500800`** | **`0x02500800`** | High-speed serial / RC receiver interface |
| **SPI0 Controller** | **`0x04025000`** | **`0x04025000`** | Direct high-speed peripheral bus |

### 2.3 Visual Address Translation Architecture

```
+===================================================================================+
|                     ALLWINNER T527 ADDRESS SPACE MAPPING                          |
+===================================================================================+

  LINUX HOST (ARM64) PHYSICAL VIEW                  XUANTIE E907 RISC-V CORE VIEW
  ================================                  =============================
  0x00040000 - 0x00073FFF [ 208 KB ] ─────────────> 0x00040000 - 0x00073FFF (SRAM A2)
    (Shared System SRAM / testPing SPSC)              (Direct Identity Mapped)

  0x07110000 - 0x0711FFFF [  64 KB ] ─────────────> 0x00000000 - 0x0000FFFF (ITCM)
    (Mapped via devm_ioremap_wc)                      (Vector Table & .text execution)

  0x07120000 - 0x0712FFFF [  64 KB ] ─────────────> 0x00080000 - 0x0008FFFF (DTCM)
    (Mapped via devm_ioremap_wc)                      (Data, Stack & .resource_table)

  0x07130000 - 0x0716FFFF [ 256 KB ] ─────────────> 0x07130000 - 0x0716FFFF (SRAM C)
    (MCU Dedicated SRAM)                              (Direct Identity Mapped)

  0x48000000 - 0x48000FFF [   4 KB ] ─────────────> 0x48000000 - 0x48000FFF (trace0)
    (RemoteProc Trace Carveout)                       (Direct Identity Mapped)

  0x48100000 - 0x481FFFFF [   1 MB ] ─────────────> 0x48100000 - 0x481FFFFF (DDR Carveout)
    (DMA Reserved Memory Pool)                        (PMP Non-Cacheable Payload Buffers)
+===================================================================================+
```

### 2.4 How Linux RemoteProc (`sunxi_rproc.c`) Routes Firmware ELFs

When Linux RemoteProc loads a firmware ELF:
1. **ITCM Segments (`0x00000000`–`0x0000FFFF`)**:
   `sunxi_rproc_da_to_va()` offsets device address by `0x07110000` and copies code directly into ITCM via `memcpy_toio()`.
2. **DTCM Segments (`0x00080000`–`0x0008FFFF`)**:
   `sunxi_rproc_da_to_va()` offsets device address by `0x07120000` and initializes `.data` and `.resource_table` via `memcpy_toio()`.
3. **Shared SRAM A2 (`0x00040000`) & SRAM C (`0x07130000`)**:
   1:1 Identity mapped — both Linux and RISC-V read and write the identical physical address.
4. **DDR Carveouts (`0x48000000` & `0x48100000`)**:
   Directly mapped into kernel virtual address space and accessed via non-cached DMA coherent mappings.

---

## 3. Firmware Layout, Linker Script & Bootstrap Sequence

### Unified Linker Script (`riscv-firmware/common/arch_riscv/firmware_t527.ld`)

The unified linker script maps execution code into zero-wait-state **ITCM (`0x00000000`)**, data/stack into **DTCM (`0x00080000`)**, and shared structures into **SRAM A2 (`0x00040000`)**:

```ld
OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    ITCM (rx)    : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx)   : ORIGIN = 0x00080000, LENGTH = 64K
    SRAM_A2 (rwx): ORIGIN = 0x00040000, LENGTH = 208K
    SRAM_C (rwx) : ORIGIN = 0x07130000, LENGTH = 256K
}

SECTIONS
{
    /* Exception vector table must be 64-byte aligned for RISC-V mtvec */
    .vectors :
    {
        . = ALIGN(64);
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > ITCM

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > ITCM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > ITCM

    /* RemoteProc Resource Table (Parsed by Linux on ELF Load) */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > DTCM

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
    } > DTCM AT > ITCM
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
    } > DTCM

    /* DTCM 8KB Stack */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x2000;
        _stack_top = .;
    } > DTCM
}
```

### Bootstrap Sequence (`riscv-firmware/common/arch_riscv/startup.S`)

1. **Disable Interrupts**: `csrw mie, zero`, `csrw mip, zero`.
2. **Setup Stack Pointer**: `la sp, _stack_top` in DTCM (`0x00080000 + 0x2000`).
3. **Setup Global Pointer**: `la gp, __global_pointer$` for relaxed linker addressing.
4. **Enable Hardware FPU**: `csrs mstatus, (3 << 13)` (Sets `mstatus.FS = 0b11` to enable single/double precision FPU).
5. **Configure Trap Vector**: `csrw mtvec, _vectors` (aligned to 64 bytes in ITCM `0x00000000`).
6. **Copy Initialized Data**: Copies `.data` from ITCM (LMA) to DTCM (VMA).
7. **Zero BSS**: Clears `.bss` variables in DTCM.
8. **Call Global C++ Constructors**: Calls `__libc_init_array` if present.
9. **Jump to Application**: Executes `call main`.

---

## 4. Modern Zero-Allocation C++ HAL Modules (`common/hal/`)

The firmware architecture uses a modular, zero-allocation C++ HAL suite located under [`riscv-firmware/common/hal/`](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/common/hal/):

* **`hal::Rpmsg` (`hal/rpmsg.hpp`, `hal/rpmsg.cpp`)**:
  - Zero-allocation VirtIO vring and OpenAMP RPMsg driver.
  - Implements VirtIO split vrings, descriptor tables, available/used rings, and Name Service Announcements.
  - Interoperates cleanly with Linux kernel `virtio_rpmsg_bus` and `rpmsg_char`.
* **`hal::SpscQueue` (`hal/spsc_queue.hpp`)**:
  - Lock-free, zero-allocation Single-Producer Single-Consumer circular ring buffer.
  - Utilizes C++11 atomic acquire-release memory fences for synchronization between ARM64 Linux and RISC-V E907 without locking.
* **`hal::Pmp` (`hal/pmp.hpp`, `hal/pmp.cpp`)**:
  - Configures XuanTie E907 Physical Memory Protection (PMP) CSRs (`pmpaddr*`, `pmpcfg*`).
  - Marks external DDR DMA payload buffers (`0x48100000`) as non-cacheable to eliminate cache invalidation/flush overhead.
* **`hal::Trace` (`hal/trace.hpp`)**:
  - Zero-allocation ASCII string and packed binary ring-buffer logger.
  - Formats telemetry, heartbeats, and sensor readings directly into the RemoteProc debugfs `trace0` buffer.
* **`hal::Crash` (`hal/crash.hpp`)**:
  - Machine-mode exception and trap autopsy handler.
  - Captures all 31 GPRs (`x1`–`x31`) and CSRs (`mepc`, `mcause`, `mtval`, `mstatus`) upon fatal faults, outputting structured crash logs to `trace0`.
* **`hal::Timer` (`hal/timer.hpp`)**:
  - Calibrated 64-bit microsecond counter and busy-wait delay for the 200 MHz core (`TICKS_PER_US = 200`).

---

## 5. Test Applications Suite (`apps/`)

Under [`riscv-firmware/apps/`](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/apps/), seven progressive test applications validate core functionality, memory mapping, telemetry, exception handling, and three inter-processor communication paradigms:

```text
apps/
├── testBasic/               # Minimal boot, ITCM execution, and multi-SRAM address writes
├── testBasicTrace0/         # RemoteProc resource table, ASCII startup banner & 1s periodic trace
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

1. **`testBasic`**: Boots into ITCM `0x00000000` and continuously writes magic counters to Shared SRAM A2 (`0x00040000`), DTCM (`0x00081000`), and MCU SRAM C (`0x07130000`) for sanity testing via `devmem`.
2. **`testBasicTrace0`**: Registers a `.resource_table` with a 4 KB `trace0` buffer in DDR carveout (`0x48000000`) and emits 1 Hz heartbeat logs viewable via Linux debugfs.
3. **`testStringBinaryTrace0`**: Combines double-precision hardware FPU math (sine wave computation) with a 32-byte packed binary `TelemetryPacket` in Shared SRAM A2 (`0x00041000`) and formatted ASCII log output in `trace0`.
4. **`testCrash`**: Verifies machine-mode exception trapping (`mtvec`). After emitting heartbeats, it executes an illegal instruction, triggering a full register autopsy dump to `trace0` and writing `0xDEADF00D` to SRAM A2.
5. **`testPing`**: Ultra-low-latency direct shared SRAM SPSC communication using `hal::SpscQueue`. Linux companion tool `ping_shm` measures round-trip time latency down to ~1.5–2.5 $\mu\text{s}$.
6. **`testPingRpmsg`**: Standard Linux kernel VirtIO RPMsg framework (`virtio_rpmsg_bus`) using `hal::Rpmsg`. Interacts with `/dev/rpmsg0` via companion tool `ping_rpmsg`.
7. **`testDRAMMsg`**: Hybrid memory architecture combining zero-wait-state SRAM A2 SPSC control queues with a 1 MB DDR DRAM payload buffer pool configured as non-cacheable via `hal::Pmp`. Linux companion tool `ping_dram` benchmarks high-bandwidth payload transfers up to 4 KB per frame.

---

## 6. Communication Paradigm & IPC Architecture Comparison

| IPC Category | **[STANDARDS-BASED]**<br>Official `libopenamp` + `libmetal` | **[STANDARDS-BASED]**<br>Lite-libmetal / `hal::Rpmsg` (`testPingRpmsg`) | **[CUSTOM LOW-LATENCY]**<br>Hybrid SRAM / DDR (`testDRAMMsg`) | **[CUSTOM LOW-LATENCY]**<br>Pure Shared SRAM (`testPing` / `hal::SpscQueue`) |
| :--- | :--- | :--- | :--- | :--- |
| **Architecture Family** | **Standards-Based (VirtIO / OpenAMP)** | **Standards-Based (VirtIO / OpenAMP)** | **Custom Hardware-Direct HAL** | **Custom Hardware-Direct HAL** |
| **Control Path** | VirtIO vrings via `libmetal` layers | VirtIO vrings via C++ `std::atomic` | Lock-Free SPSC in SRAM A2 (`0x00040000`) | Lock-Free SPSC in SRAM A2 (`0x00040000`) |
| **Data Path** | RPMsg DMA buffers (DDR) | RPMsg DMA buffers (DDR) | **DDR DRAM Carveout (`0x48100000`, 1 MB)** | Direct SRAM A2 (`0x00040000`, 64B frames) |
| **Linux Driver / Stack**| `virtio_rpmsg_bus` + `rpmsg_char` | `virtio_rpmsg_bus` + `rpmsg_char` | Direct MMIO (`/dev/mem`) + PMP coherent | Direct MMIO (`/dev/mem`) |
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
* `testBasicTrace0.elf`
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
# 1. Run Pure Shared SRAM Ping (testPing)
# ==============================================================================
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Run high-frequency latency benchmark (e.g., 50,000 round-trips)
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
   # When running testBasic, inspect magic incrementing counter in SRAM A2:
   devmem 0x00040000 32
   ```

4. **Verify Exception Handling**:
   When running `testCrash.elf`, read `/sys/kernel/debug/remoteproc/remoteproc0/trace0` to inspect the full GPR and CSR exception frame dump (`mepc`, `mcause`, `mtval`, `mstatus`, `ra`, `sp`, `gp`, etc.).
