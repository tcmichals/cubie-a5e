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
|  |  - 64 KB ITCM (0x00000000) & 64 KB DTCM (0x00080000) [E907 Zero-Wait-State Local]  |  |
|  |  - 256 KB Dedicated MCU SRAM C (0x07130000) [Zero-Wait-State Low-Latency Control]  |  |
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
| **Instruction TCM (ITCM)** | **`0x07110000`** | **`0x00000000`** | **64 KB** | Zero-wait-state instruction execution (`.text`, `.vectors`) |
| **Data TCM (DTCM)** | **`0x07120000`** | **`0x00080000`** | **64 KB** | Zero-wait-state data, stack (`.stack`), and `.resource_table` |
| **Dedicated MCU SRAM (SRAM C)** | **`0x07130000`** | **`0x07130000`** | **256 KB** | 1:1 Identity mapped; ultra-low-latency direct SPSC IPC (`testPing`, `testDRAMMsg`) |
| **Reserved R_SRAM** | **`0x07280000`** | **`0x07280000`** | **256 KB** | Standby / DSP scratchpad SRAM |
| **DDR Trace Buffer (`trace0`)** | **`0x48000000`** | **`0x48000000`** | **4 KB** | RemoteProc ASCII & binary log buffer (`/sys/.../trace0`) |
| **DDR DRAM DMA Carveout** | **`0x48100000`** | **`0x48100000`** | **1 MB** | PMP non-cacheable high-bandwidth payload pool (`testDRAMMsg`) |

> [!WARNING]
> ### CRITICAL HARDWARE & SECURITY WARNING: DO NOT USE SRAM A2 (`0x00040000`) FOR RISC-V
> **SRAM A2 (`0x00040000`–`0x00073FFF`) is strictly reserved for Secure World / TrustZone and must NEVER be mapped or written to by RISC-V firmware or Linux RemoteProc:**
> 1. **ARM TrustZone Secure World (TF-A / BL31 / PSCI)**: On Allwinner ARM64 SoCs, `SRAM A2` is the **Secure SRAM (CPUS SRAM)**. ARM Trusted Firmware (TF-A BL31) runs at Secure EL3 and places its secure monitor runtime data, secure stacks, and **PSCI 1.1 CPU power-management state machines** in `SRAM A2`. The hardware TrustZone Memory Adapter (TZMA) firewalls `SRAM A2` for **Secure Access Only**; any non-secure write attempt by Linux or an external core triggers an immediate **hardware Synchronous External Abort** (bus fault).
> 2. **A733 Hardware Power Management Collision**: On the A733 SoC, `SRAM A2` is hardwired in silicon as the boot address of the Always-On E902 CPUS core running vendor **`scp.fex`**. Overwriting `0x00040000` destroys `scp.fex` and powers off system PMIC voltage rails.
> 3. **Correct Memory for E907 IPC**: XuanTie E907 firmware and RemoteProc IPC must strictly use **Dedicated MCU SRAM C (`0x07130000`, 256 KB)**, **ITCM (`0x00000000`, 64 KB)**, and **DTCM (`0x00080000`, 64 KB)** in the independent MCU domain (`0x07100000`+).

### 2.2 Allwinner On-Chip SRAM Partitioning & Hardware Allocation

| SRAM Bank | Physical Base | Size | Hardware Owner | Primary Purpose & Usage | Allowed for RISC-V E907? |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`BROM`** | `0x00000000` | 128 KB | SoC Hardware | Silicon Mask ROM; executes first instruction on power-on reset | ❌ **No** (Read-Only BootROM) |
| **`SRAM A1`** | `0x00020000` | 32 KB | Bootloader (SPL) | Loaded by BROM for `boot0` / SPL execution and DRAM PHY training | ❌ **No** (Bootloader Only) |
| **`SRAM A2`** | `0x00040000` | 208 KB | **Secure EL3 (TF-A) / CPUS** | **Secure World (TF-A BL31, OP-TEE, PSCI 1.1 power management, CPU suspend/hotplug), or A733 `scp.fex` PMIC core** | ❌ **STRICTLY PROHIBITED** (TrustZone Firewall) |
| **`ITCM`** | `0x07110000` (E907 `0x00000000`) | 64 KB | **XuanTie E907** | **Zero-wait-state 1-cycle instruction execution (`.text`, `.vectors`)** | ✅ **YES** (Core Instruction TCM) |
| **`DTCM`** | `0x07120000` (E907 `0x00080000`) | 64 KB | **XuanTie E907** | **Zero-wait-state 1-cycle data, stack (`.stack`), `.bss`, `.resource_table`** | ✅ **YES** (Core Data TCM) |
| **`SRAM C`** | `0x07130000` (E907 `0x07130000`) | 256 KB | **MCU / DSP Domain** | **1:1 Identity mapped non-secure shared SRAM for lock-free SPSC IPC, crash dump, and telemetry** | ✅ **YES** (Dedicated MCU IPC SRAM) |
| **`R_SRAM`** | `0x07280000` (E907 `0x07280000`) | 256 KB | MCU / DSP Domain | Standby / DSP scratchpad SRAM | ✅ **YES** (Scratchpad / DSP) |
| **`trace0`** | `0x48000000` | 4 KB | Linux RemoteProc | RemoteProc debugfs trace buffer (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`) | ✅ **YES** (Logging Carveout) |
| **`dram_dma`**| `0x48100000`| 1 MB | Linux RemoteProc | Non-cacheable DDR DMA payload buffer pool for high-bandwidth IPC (`testDRAMMsg`) | ✅ **YES** (Streaming Carveout) |

### 2.3 Control, Peripheral & Inter-Core Registers

| Peripheral Block | Linux Host Physical Address | E907 RISC-V Address | Description & Hardware Usage |
| :--- | :--- | :--- | :--- |
| **MCU CCU & Core Control** | **`0x07102000`** | **`0x07102000`** | Co-processor clock gate (`0x07102000`), reset (`0x07102004`), boot entry vector register (`0x07102204`) |
| **Main SoC CCU** | **`0x02001000`** | **`0x02001000`** | Root DSP/Co-processor clock gate (`0x02001c70`) |
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
  0x07110000 - 0x0711FFFF [  64 KB ] ─────────────> 0x00000000 - 0x0000FFFF (ITCM)
    (Mapped via devm_ioremap_wc)                      (Vector Table & .text execution)

  0x07120000 - 0x0712FFFF [  64 KB ] ─────────────> 0x00080000 - 0x0008FFFF (DTCM)
    (Mapped via devm_ioremap_wc)                      (Data, Stack, .resource_table, .dtcm_scratch)

  0x07130000 - 0x07130FFF [   4 KB ] ─────────────> 0x07130000 - 0x07130FFF (.trace_buffer)
    (RemoteProc Trace0 in on-chip SRAM)               (Direct Identity Mapped, Zero-Wait)

  0x07131000 - 0x0716FFFF [ 252 KB ] ─────────────> 0x07131000 - 0x0716FFFF (.sram_c)
    (MCU Dedicated SRAM / testPing SPSC)              (Direct Identity Mapped, Zero-Wait)

  0x48100000 - 0x481FFFFF [   1 MB ] ─────────────> 0x48100000 - 0x481FFFFF (DDR Carveout)
    (DMA Reserved Memory Pool)                        (PMP Non-Cacheable Payload Buffers)
+===================================================================================+
```

> [!TIP]
> **Why Trace in SRAM avoids DDR Cache Hazards:**
> When the trace buffer is in external DDR, the E907 core buffers writes in its private L1 Data Cache (write-back). Without PMP or explicit cache cleaning (`dcache_clean_range`), writes stay dirty in L1 and do not reach physical DDR DRAM, making them invisible to the Linux host. Moving `.trace_buffer` into on-chip `SRAM_C` (`0x07130000`) completely bypasses the L1 cache writeback issue because on-chip SRAM is mapped as zero-wait-state strongly-ordered memory.

### 2.5 How Linux RemoteProc (`sunxi_rproc.c`) Routes Firmware ELFs

When Linux RemoteProc loads a firmware ELF:
1. **ITCM Segments (`0x00000000`–`0x0000FFFF`)**:
   `sunxi_rproc_da_to_va()` offsets device address by `0x07110000` and copies code directly into ITCM via `memcpy_toio()`.
2. **DTCM Segments (`0x00080000`–`0x0008FFFF`)**:
   `sunxi_rproc_da_to_va()` offsets device address by `0x07120000` and initializes `.data` and `.resource_table` via `memcpy_toio()`.
3. **Dedicated MCU SRAM C (`0x07130000`)**:
   1:1 Identity mapped — both Linux and RISC-V read and write the identical physical address. The linker script places `.trace_buffer` at `0x07130000` and `.sram_c` at `0x07131000`.
4. **DDR Carveouts (`0x48100000`)**:
   Directly mapped into kernel virtual address space and accessed via non-cached DMA coherent mappings.

---

## 3. Test Applications Suite (`apps/`)

## 3. Test Applications Suite (`apps/`) & Performance Roadmap

The firmware test suite follows a progressive **"Walk -> Run"** architecture, starting with basic bring-up sanity and trace logging before advancing to high-throughput, low-latency IPC:

```text
apps/
├── [PHASE 1: WALK - BOOT, TRACE & TELEMETRY]
│   ├── testBasic/               # Minimal boot, ITCM execution, linker-mapped SRAM C & DTCM sanity
│   ├── testStringBinaryTrace0/  # SRAM trace0 (0x07130000), Mixed ASCII text + packed binary telemetry + FPU
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
  - Boots into ITCM `0x00000000`, configures stack in DTCM `0x00080000`.
  - Maps variables into dedicated linker sections (`.sram_c_loc1`, `.sram_c_loc2`, `.dtcm_scratch`) without hardcoded pointer macros.
  - Continuously increments counters so host Linux can verify life via `devmem 0x07131000 32` or via `trace0`.

---

### App 2: `testStringBinaryTrace0` (Mixed String & Binary Telemetry)
* **Purpose**: Demonstrates structured telemetry streaming over `trace0` combining formatted ASCII strings with packed binary structures and hardware FPU computation.
* **Functionality**:
  - Declares `.resource_table` section exporting `trace0` buffer in Dedicated MCU SRAM C (`0x07130000`).
  - Completely eliminates DDR caching issues—trace writes are immediately visible to Linux without software cache flushes.
  - Utilizes single-precision (`float`) and double-precision (`double`) hardware FPU math (sine wave computation).
  - Populates a 36-byte packed binary `TelemetryPacket` in Dedicated MCU SRAM C (`.sram_c` @ `0x07131000`).
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

[ASCII]   [TELM #1] Accel: (+0.015, -0.008, +9.811) | FPU Sin: +0.0998 | SRAM: 0x07131000
[STRUCT]  Seq #1     | Up: 500   ms | Accel: (+0.015, -0.008, +9.811) | FPU Sin: +0.0998 | Csum: 0xA5A4
[HEXDUMP]
         0x00000000: 4D 4C 45 54 01 00 00 00 F4 01 00 00 7B 14 AE 3C |MLE.........{..<|
         0x00000010: 2F DD 03 BC 25 1B 1D 41 84 FC 3B 21 00 00 00 00 |/...%..A..;!....|
         0x00000020: A4 A5 AA 55                                     |...U            |
```

#### Lite / Fast Direct SRAM Python Monitor (`apps/testStringBinaryTrace0/fast_sram_telemetry.py`)
For ultra-high-rate telemetry (>1,000 Hz) bypassing the kernel filesystem layer, `fast_sram_telemetry.py` memory-maps the Dedicated MCU SRAM C (`0x07131000`) directly via `/dev/mem` for zero-copy polling:

```bash
sudo python3 apps/testStringBinaryTrace0/fast_sram_telemetry.py
```

Example high-rate output:
```text
=== Direct Zero-Copy SRAM Telemetry Reader (Lite/Fast) ===
Device: /dev/mem | Physical Target: 0x07131000 | Packet Size: 36 bytes
Mode: Direct physical mmap (bypasses debugfs / kernel filesystem layers)

[SRAM-DIRECT] Seq #124   | Up: 62000 ms | Accel: (+1.860, -0.992, +9.814) | FPU Sin: +0.1542 | Rate: 1042.5 Hz
[SRAM-DIRECT] Seq #125   | Up: 62500 ms | Accel: (+1.875, -1.000, +9.809) | FPU Sin: +0.0544 | Rate: 1040.1 Hz
```

> [!NOTE]
> **Why `epoll` Cannot Be Used on `trace0` or `/dev/mem` (The Polling Trade-Off)**:
> In the Linux kernel RemoteProc subsystem (`drivers/remoteproc/remoteproc_debugfs.c`), `trace0` is a simple debugfs file implementing only `.read`, `.open`, and `.llseek`. It has **no `.poll` method and no wait-queue**; attempting to register it with `epoll_ctl()` immediately returns `EPERM` (*Operation not permitted*). Likewise, direct SRAM mapping via `/dev/mem` provides raw physical memory with no event notification.
>
> Consequently, both `monitor_trace.py` and `fast_sram_telemetry.py` must **poll** in user-space, consuming host CPU cycles. This is the baseline in Phase 1 ("Walk"). Phase 2 ("Run") introduces hardware Mailbox doorbells and `/dev/rpmsg0` where the kernel's `virtio_rpmsg_bus` implements `.poll`, enabling true event-driven `epoll` with **0% idle CPU utilization**.

---

### App 3: `testCrash`
* **Purpose**: Fault simulation, trap vector validation, and crash autopsy reporting via RemoteProc.
* **Functionality**:
  - Configures `mtvec` to custom exception trap handler.
  - Emits 3 normal heartbeats before intentionally triggering an illegal instruction trap.
  - Trap handler captures `mepc`, `mcause`, `mtval`, `mstatus`, and full GPR dump (`ra`, `sp`, `gp`, `a0`..`a7`, `t0`..`t6`, `s0`..`s11`).
  - Writes fatal signature `0xDEADF00D` to MCU SRAM C (`0x07130000`).
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
      ra (x1) = 0x00000184  sp (x2) = 0x0008FFF0  gp (x3) = 0x00080800
      ...
    ```

---

### App 4: `testPing` (Fast Direct Shared Memory / Lite-libmetal Style)
* **Purpose**: Ultra-low-latency, zero-copy, deterministic inter-processor communication.
* **Functionality**:
  - Direct lock-free shared SRAM channel (`ShmPingChannel` @ `0x07130000`).
  - Memory-barrier-synchronized doorbell registers (`host_doorbell`, `riscv_doorbell`).
  - Sub-microsecond response latency (~1.5–2.5 $\mu\text{s}$ Round-Trip Time).
* **Linux Companion Tool**: `apps/testPing/linux/ping_shm`
  - High-precision latency benchmarking using `clock_gettime(CLOCK_MONOTONIC_RAW)`.
  - Computes min, avg, max latency, jitter (standard deviation), percentiles (p50, p90, p99, p99.9), and message throughput.

```bash
# Run 100,000 iterations over Dedicated MCU SRAM C
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
|   |         DEDICATED MCU SRAM C (0x07130000) - CONTROL PATH            |   |
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
  - Control block (`DramSpscControlBlock` @ `0x07130000`) in fast zero-wait-state SRAM C.
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

| IPC Category | **[STANDARDS-BASED]**<br>Official `libopenamp` + `libmetal` | **[STANDARDS-BASED]**<br>Lite-libmetal / `hal::Rpmsg` (`testPingRpmsg`) | **[CUSTOM LOW-LATENCY]**<br>Hybrid SRAM / DDR (`testDRAMMsg`) | **[CUSTOM LOW-LATENCY]**<br>Pure Dedicated SRAM (`testPing` / `hal::SpscQueue`) |
| :--- | :--- | :--- | :--- | :--- |
| **Architecture Family** | **Standards-Based (VirtIO / OpenAMP)** | **Standards-Based (VirtIO / OpenAMP)** | **Custom Hardware-Direct HAL** | **Custom Hardware-Direct HAL** |
| **Control Path** | VirtIO vrings via `libmetal` layers | VirtIO vrings via C++ `std::atomic` | Lock-Free SPSC in SRAM C (`0x07130000`) | Lock-Free SPSC in SRAM C (`0x07130000`) |
| **Data Path** | RPMsg DMA buffers (DDR) | RPMsg DMA buffers (DDR) | **DDR DRAM Carveout (`0x48100000`, 1 MB)** | Direct SRAM C (`0x07130000`, 64B frames) |
| **Linux Driver / Stack**| `virtio_rpmsg_bus` + `rpmsg_char` | `virtio_rpmsg_bus` + `rpmsg_char` | Direct MMIO (`/dev/mem`) + PMP coherent | Direct MMIO (`/dev/mem`) |
| **Linux Ecosystem**     | Standard (`/dev/rpmsg0`, `/dev/ttyRPMSG0`) | Standard (`/dev/rpmsg0`, `/dev/ttyRPMSG0`) | Custom High-Speed API / `ping_dram` | Custom High-Speed API / `ping_shm` |
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
| **Host Python Direct Poller** | `/usr/bin/fast_sram_telemetry.py` | Direct `/dev/mem` zero-copy physical SRAM reader (>1 kHz) |
| **SRAM Ping Benchmark** | `/usr/bin/ping_shm` | Shared memory lock-free SPSC latency benchmark tool |
| **RPMsg Ping Benchmark** | `/usr/bin/ping_rpmsg` | Standard Linux `/dev/rpmsg` round-trip test tool |
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

### Live Firmware Switching on Target (Cubie A5E)

Once logged into the board (via serial or SSH), all tools and ELFs are in your system `$PATH`:

```bash
# 1. Run Pure Shared SRAM Ping (testPing)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
ping_shm -n 50000

# 2. Run Hybrid SRAM/DRAM SPSC Benchmark (testDRAMMsg)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testDRAMMsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
ping_dram -n 10000 -s 1024

# 3. Run Standard Linux RPMsg (testPingRpmsg)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPingRpmsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
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
