# XuanTie E907 Firmware Test Applications Suite & Performance Guide

This document details the bare-metal test suite (`apps/`) for the **XuanTie E907 RISC-V Co-Processor (up to 200 MHz)** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**, outlining memory layouts, execution flows, and host Linux benchmarking procedures.

---

## 1. Test Applications Architecture (Walk -> Run Roadmap)

The firmware test suite follows a progressive **"Walk -> Run"** methodology, validating core bootstrap and diagnostic telemetry before advancing to hard real-time inter-processor communication (IPC):

```text
riscv-firmware/apps/
├── [PHASE 1: WALK - BOOT, TRACE & DIAGNOSTICS]
│   ├── testBasic/               # Minimal boot in on-chip SRAM (0x3FFC0000) & live counter increment
│   ├── testStringBinaryTrace0/  # SRAM trace0 buffer, mixed ASCII text + packed binary telemetry + hardware FPU
│   ├── testCrash/               # Hardware exception trapping (mtvec), 0xDEADF00D signature & crash dump
│   └── exampleRiscv/            # Minimal reference template with HAL timer delay
│
└── [PHASE 2: RUN - LOW-LATENCY IPC & HIGH-THROUGHPUT STREAMING]
    ├── testPing/                # Ultra-low-latency (~2us) Direct SRAM SPSC Queue + UIO Doorbell benchmark
    │   └── linux/               # ping_shm and ping_uio Linux host companion benchmark tools
    ├── testPingRpmsg/           # Standards-based Linux VirtIO RPMsg (hal::Rpmsg) + Mailbox Doorbells
    │   └── linux/               # ping_rpmsg Linux host companion benchmark tool
    └── testDRAMMsg/             # High-throughput Hybrid SRAM Control + DDR DRAM DMA Payload Buffers
        └── linux/               # ping_dram Linux host companion benchmark tool
```

---

## 1.1 Upstream Linux Kernel Submission (`linux-sunxi`) Validation Sequence

When proposing the `sunxi_rproc.c` driver to upstream maintainers (e.g., `linux-sunxi`, `linux-remoteproc`), submitters must prove driver compliance across four tiers:

| Tier | Test App | Upstream Driver Capability Verified | Expected Kernel / Diagnostic Output |
| :--- | :--- | :--- | :--- |
| **Tier 1: Lifecycle & ELF Loading** | `testBasic` | Clean `start` $\rightarrow$ `stop` cycle, SRAM Space 0 loading (`0x3FFC0000`), no bus lockups (`WORK_MODE_REG = 0x00000003`) | `[testBasic] Heartbeat #N \| MISA=0x40901125` via debugfs `trace0` |
| **Tier 2: Sustained Streaming & FPU** | `testStringBinaryTrace0` | Sustained `trace0` ring buffer streaming without memory corruption; hardware single-precision FPU math | Live sine telemetry via `monitor_trace.py` or debugfs `trace0` |
| **Tier 3: Standard VirtIO RPMsg (Gold Standard)** | `testPingRpmsg` | `virtio_rpmsg_bus` probing, vring parsing from `.resource_table`, Name Service announcement, `/dev/rpmsg0` | `dmesg`: `registered virtio0 (type 7)` and `ping_rpmsg -n 1000` succeeds |
| **Tier 4: Crash Isolation** | `testCrash` | Post-mortem register autopsy captured in SRAM; ARM host kernel remains stable without panicking | `trace0`: `mcause=0x00000002` dump; Linux OS continues running |

---

## 2. Hardware Memory Map for Firmware Tests

All tests execute strictly from on-chip `SRAM` and dedicated DDR carveouts. Memory regions `0x00020000` (HiFi4 DSP) and `0x00044000` (OP-TEE TrustZone) are strictly off-limits:

| Memory Region | Core Address (DA) | Host Address (A527) | Host Address (T527) | Size | Usage in Test Suite |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **SRAM Space 0** | `0x3FFC0000` | `0x07280000` | `0x07200000` | 256 KB | Primary boot, `.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer` |
| **SRAM Space 1** | `0x40000000` | `0x072c0000` | `0x07280000` | 256 KB | Secondary SRAM bank enabled via `REMAP_CTRL_REG[1]=1`, shared IPC |
| **Crash Signature** | `0x3FFFFF00` | `0x072BFF00` | `0x0723FF00` | 256 B | Top 256 bytes of SRAM Space 0 (`0xDEADF00D` post-mortem block) |
| **DDR DMA Pool**    | `0x48100000` | `0x48100000` | `0x48100000` | 1 MB | Non-cacheable payload buffer pool for bulk streaming (`testDRAMMsg`) |

---

## 3. Phase 1: Walk — Boot, Trace & Diagnostics

### App 1: `testBasic` (Minimal Bring-Up & Counter Sanity)
* **Directory**: `riscv-firmware/apps/testBasic/`
* **Purpose**: Validates toolchain output, linker script layout, core reset de-assertion, and memory bus access.
* **Firmware Behavior**:
  - Boots cleanly at `0x3FFC0000` using `e907_sram.ld`.
  - Initializes the HAL trace subsystem (`hal::Trace::init()`).
  - Sets up two 32-bit heartbeat counters in `.sram_c_loc1` and `.sram_c_loc2` within SRAM.
  - Enters an infinite loop incrementing the heartbeat counters and logging counter values every second.
* **Host Verification Commands**:
  ```bash
  # Ensure debugfs is mounted to access trace0:
  mount | grep debugfs || mount -t debugfs none /sys/kernel/debug

  # Deploy and boot
  echo stop > /sys/class/remoteproc/remoteproc0/state
  echo "testBasic.elf" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state

  # Check live incrementing log stream via debugfs
  cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

  # Or peek directly at the physical memory counters (Host physical 0x07287100 on A527 / 0x07207100 on T527):
  devmem2 0x07287100 w
  devmem2 0x07287104 w
  ```

---

### App 2: `testStringBinaryTrace0` (Mixed Text + Binary Telemetry & FPU)
* **Directory**: `riscv-firmware/apps/testStringBinaryTrace0/`
* **Purpose**: Demonstrates high-density telemetry combining formatted ASCII strings with packed binary structures and single/double-precision hardware FPU math (`fsin`/`fcos`).
* **Firmware Behavior**:
  - Exports a 4 KB `.trace_buffer` section via the ELF `.resource_table` in on-chip SRAM_A3.
  - Computes floating-point sine wave values using the hardware FPU unit.
  - Populates a 36-byte packed binary `TelemetryPacket` in SRAM_A3 (`.sram_c`).
  - Interleaves three data streams directly into `trace0`:
    1. `STRING:` Human-readable ASCII log with formatted floating-point values.
    2. `BINARY:` Raw 36-byte packed struct (`TelemetryPacket`) with header, sequence, uptime, accel, and checksum.
    3. `HEXDUMP:` Terminal-inspectable formatted hex dump of the binary packet.
* **Host Verification & Monitoring**:
  ```bash
  # Deploy and boot
  echo stop > /sys/class/remoteproc/remoteproc0/state
  echo "testStringBinaryTrace0.elf" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state

  # Option A: Standard debugfs monitor (polls trace0)
  python3 /usr/local/bin/monitor_trace.py /sys/kernel/debug/remoteproc/remoteproc0/trace0

  # Option B: Ultra-fast direct physical SRAM telemetry reader (>1 kHz, zero kernel FS overhead)
  python3 /usr/local/bin/fast_sram_telemetry.py
  ```

---

### App 3: `testCrash` (Hardware Exception Trapping & Autopsy Dump)
* **Directory**: `riscv-firmware/apps/testCrash/`
* **Purpose**: Validates exception vectoring (`mtvec`), ensures the core halts safely in low-power `wfi` without double-faulting into silicon lockup, and captures full register state.
* **Firmware Behavior**:
  - Initializes `mtvec` to `default_trap_entry` in `startup.S`.
  - Emits 3 normal countdown heartbeats.
  - Purposely triggers an illegal instruction exception (`asm volatile(".word 0x00000000")` at `main.cpp:51`).
  - Exception pipeline captures `mepc`, `mcause`, `mtval`, `mstatus`, and all 31 GPRs (`ra`, `sp`, `gp`, `tp`, `t0`-`t6`, `s0`-`s11`, `a0`-`a7`).
  - `hal::CrashHandler::handle()` writes persistent signature `0xDEADF00D`, `mepc`, `mcause`, `mtval` to the top 256 bytes of SRAM Space 0 (`0x3FFFFF00`).
  - Streams full autopsy report to `trace0` and halts in a permanent `wfi` loop.
* **Host Verification & Crash Debugging**:
  ```bash
  # Disable automatic kernel restart so the crash state can be examined post-mortem
  echo disabled > /sys/kernel/debug/remoteproc/remoteproc0/recovery

  # Run testCrash
  echo stop > /sys/class/remoteproc/remoteproc0/state
  echo "testCrash.elf" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state

  # 1. Read autopsy report from debugfs
  cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

  # 2. Inspect physical crash signature in SRAM (Host 0x072BFF00 on A527 / 0x0723FF00 on T527):
  devmem2 0x072BFF00 w 4

  # 3. Resolve exact faulting source line with addr2line (resolves to main.cpp:51):
  riscv-none-elf-addr2line -e testCrash.elf -a -f -C 0x3ffc080e

  # 4. Confirm clean HAL parking (WORK_MODE_REG 0x07130248 = 0x00000003, Bit 3 lockup = 0):
  devmem2 0x07130248 w
  ```

---

### App 4: `exampleRiscv` (Minimal Reference Template)
* **Directory**: `riscv-firmware/apps/exampleRiscv/`
* **Purpose**: Clean starting template for custom user applications.
* **Firmware Behavior**:
  - Implements standard `.resource_table` with `trace0`.
  - Initializes `hal::Timer` and blinks/toggles diagnostic state with `hal::Timer::delay_ms(1000)`.

---

## 4. Phase 2: Run — High-Speed Real-Time IPC

### App 5: `testPing` (Fast Direct Shared SRAM / UIO Doorbell)
* **Directory**: `riscv-firmware/apps/testPing/`
* **Purpose**: Deterministic, zero-copy, sub-3 microsecond inter-processor communication over on-chip SRAM.
* **Architecture**:
  - Lock-free Single-Producer Single-Consumer (SPSC) circular ring buffers in on-chip SRAM (`0x3FFC0000`).
  - Two operational modes:
    1. **Event-Driven UIO Doorbell Mode (Recommended)**: Converts the hardware Message Box (`0x03003000`) into a generic UIO device (`/dev/uio0`). Host blocks asynchronously on `select.epoll()` with **0% idle CPU utilization**; E907 pulses GIC SPI 147 interrupt to wake host.
    2. **Direct Memory Polling Baseline**: Reads SRAM directly via physical mmap, achieving theoretical maximum bus transfer rate.
* **Host Benchmark Tools**:
  - **`ping_uio.py`**: Python client using `select.epoll()` on `/dev/uio0`.
  - **`ping_shm`**: C++ high-precision latency benchmarking tool using `clock_gettime(CLOCK_MONOTONIC_RAW)`. Computes min, avg, max, jitter (stddev), and p50/p90/p99/p99.9 latency.
* **Host Execution Commands**:
  ```bash
  # Deploy and boot
  echo stop > /sys/class/remoteproc/remoteproc0/state
  echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state

  # Mode 1: Event-driven UIO Doorbell (0% idle CPU burn via epoll)
  ping_uio.py -n 50000

  # Mode 2: Direct Shared SRAM memory-polling latency test (100,000 round-trips)
  ping_shm -n 100000
  ```

---

### App 6: `testPingRpmsg` (Standards-Based Linux VirtIO RPMsg)
* **Directory**: `riscv-firmware/apps/testPingRpmsg/`
* **Purpose**: Standard Linux kernel RPMsg framework communication (`virtio_rpmsg_bus`).
* **Firmware Behavior**:
  - Resource table declares `RSC_VDEV` (VirtIO ID 7) with 2 vrings (16 descriptors each).
  - Publishes Name Service announcement for endpoint `"rpmsg-ping-channel"` (address 1024).
  - Receives ping packets from Linux `/dev/rpmsg0` and replies immediately with pong responses.
* **Host Benchmark Tool (`ping_rpmsg`)**:
  - Communicates directly with `/dev/rpmsg0` (or dynamically creates endpoint via `/dev/rpmsg_ctrl0`).
  - Measures standard kernel VirtIO latency and packet throughput.
* **Host Execution Commands**:
  ```bash
  echo stop > /sys/class/remoteproc/remoteproc0/state
  echo "testPingRpmsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state

  # Run 5,000 round-trips over Linux RPMsg character device
  ping_rpmsg -n 5000
  ```

---

### App 7: `testDRAMMsg` (Hybrid SRAM Control / DDR DRAM Payload Streaming)
* **Directory**: `riscv-firmware/apps/testDRAMMsg/`
* **Purpose**: High-throughput bulk data streaming (point-clouds, camera frames, flight telemetry) moving payload buffers to DDR DRAM while keeping SPSC control structures in ultra-low-latency on-chip SRAM.
* **Architecture**:
```text
+-----------------------------------------------------------------------------+
|                          HYBRID MEMORY IPC ARCHITECTURE                     |
|                                                                             |
|   +---------------------------------------------------------------------+   |
|   |         ON-CHIP SRAM (0x3FFC0000 / 0x40000000) - CONTROL PATH       |   |
|   |  - SPSC Head & Tail Pointers (Atomic single-word updates)           |   |
|   |  - Producer/Consumer Doorbells & Monotonic Sequence Counters        |   |
|   |  - 16-slot TX/RX Descriptor Rings (Holds DRAM Buffer Offsets & Len) |   |
|   |  - Zero-wait-state access for sub-microsecond descriptor exchanges  |   |
|   +---------------------------------------------------------------------+   |
|                                     │ (Payload pointers & offsets)          |
|                                     ▼                                       |
|   +---------------------------------------------------------------------+   |
|   |             DDR DRAM CARVEOUT (0x48100000) - DATA PATH              |   |
|   |  - Non-Cacheable DMA Carveout / Reserved Memory Window (1 MB)       |   |
|   |  - 16x Host->RISC-V Payload Buffers (Up to 4 KB each)               |   |
|   |  - 16x RISC-V->Host Payload Buffers (Up to 4 KB each)               |   |
|   |  - PMP / XuanTie Cache attributes configured for zero cache stalls  |   |
|   +---------------------------------------------------------------------+   |
+-----------------------------------------------------------------------------+
```
* **Firmware Behavior**:
  - Control block (`DramSpscControlBlock`) mapped in fast on-chip SRAM.
  - 1 MB payload pool in DDR DRAM Carveout (`0x48100000`).
  - Configures RISC-V Physical Memory Protection (PMP) and XuanTie Cache maintenance (`mhcr`, `mcor`, `dcache.iva`, `dcache.cpa`) for DMA-coherent access without software cache flushes.
* **Host Benchmark Tool (`ping_dram`)**:
  - Measures throughput (MB/s), latency, and jitter across variable payload sizes (64 B to 4096 B).
* **Host Execution Commands**:
  ```bash
  echo stop > /sys/class/remoteproc/remoteproc0/state
  echo "testDRAMMsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state

  # Stream 10,000 packets of 1024 bytes each over DDR DRAM
  ping_dram -n 10000 -s 1024
  ```

---

## 5. IPC Architecture Comparison Matrix

| IPC Mechanism | **[STANDARDS-BASED]**<br>Standard VirtIO RPMsg (`testPingRpmsg`) | **[CUSTOM STREAMING]**<br>Hybrid SRAM / DDR (`testDRAMMsg`) | **[CUSTOM LOW-LATENCY]**<br>SRAM SPSC + UIO (`testPing`) |
| :--- | :--- | :--- | :--- |
| **Control Path** | VirtIO vrings in SRAM | Lock-Free SPSC in SRAM (`0x3FFC0000` / `0x40000000`) | Lock-Free SPSC in SRAM (`0x3FFC0000` / `0x40000000`) |
| **Data Path** | RPMsg DMA buffers (DDR) | **DDR DRAM Carveout (`0x48100000`, 1 MB)** | MCU SRAM_A3 (64 B frames) |
| **Linux Driver / Stack** | `virtio_rpmsg_bus` + `rpmsg_char` | UIO / Non-cacheable reserved memory | `uio_pdrv_genirq` (`/dev/uio0`) |
| **User Space API** | `/dev/rpmsg0` | `ping_dram` companion tool | `ping_uio.py` (`select.epoll()`) / `ping_shm` |
| **Firmware Code Size** | **~2 – 3 KB** (zero dynamic heap) | **~3 – 4 KB** (zero dynamic heap) | **< 1 KB** (header-only C++ template) |
| **Round-Trip Latency** | **~50 – 90 $\mu\text{s}$** | **~3.0 – 6.0 $\mu\text{s}$** (DDR bus latency) | **~1.5 – 2.5 $\mu\text{s}$** (Zero-wait-state SRAM) |
| **Jitter (StdDev)** | Moderate (Kernel scheduler) | **Ultra-Low (<0.5 $\mu\text{s}$)** | **Ultra-Low (<0.2 $\mu\text{s}$)** |
| **Max Payload Size** | Medium (512 B default) | **Large (Up to 4 KB per frame, MB pool)** | Small (40–64 B, SRAM bounded) |
| **Throughput Bandwidth** | Moderate (~10–20 MB/s) | **High Bandwidth (>100 MB/s)** | High Packet Rate (Low Payload) |
| **Target Use Case** | Standard OS interop, ROS nodes | Camera frames, point clouds, logging | Flight PID loop, motor PWM control |

---

## 6. How to Build, Stage and Switch Tests

### Compiling All Firmware Applications
```bash
make -C riscv-firmware clean all
```
All compiled ELFs and Linux host tools are staged into `riscv-firmware/bin/`:
* `testBasic.elf`
* `testStringBinaryTrace0.elf`
* `testCrash.elf`
* `testPing.elf`
* `testPingRpmsg.elf`
* `testDRAMMsg.elf`
* `exampleRiscv.elf`
* `ping_shm`, `ping_uio`, `ping_uio.py`, `ping_rpmsg`, `ping_rpmsg.py`, `ping_dram`, `monitor_trace.py`, `fast_sram_telemetry.py`

### Live Firmware Switching on Target
```bash
# 1. Stop current firmware
echo stop > /sys/class/remoteproc/remoteproc0/state

# 2. Select firmware ELF
echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware

# 3. Start execution
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. Verify running state
cat /sys/class/remoteproc/remoteproc0/state
# Output: running
```

---

## 7. Silicon Core Identification (E906 vs E907 Verification)

### Background: Why Both Names Appear
* **Board & Marketing Layer**: Radxa Cubie A5E promotional specs, product briefs, and technical writeups refer to the co-processor as the **T-Head XuanTie E907**.
* **Silicon RTL & Vendor BSP Layer**: Allwinner's internal hardware register maps (`0x07130000`), device trees (`sun55iw3p1.dtsi`), and Linux 5.15 BSP drivers refer to it as the **XuanTie E906** (`E906_VER_REG`, `E906_STA_ADD_REG`, `CONFIG_AW_REMOTEPROC_E906_BOOT`).
* **Binary Compatibility**: Both cores share the same 32-bit RV32IMAFDC 5-stage pipeline architecture. Binaries compiled with `-march=rv32imafdc -mabi=ilp32d` execute identically on both.

To verify the exact silicon core implemented on physical hardware, run the following tests:

### Method 1: Query the `misa` CSR (Bit 15 — 'P' Extension)
The primary architectural difference between an E906 and an E907 is the presence of T-Head's packed-SIMD / DSP extension (**`P`**):
* **E906**: Implements `RV32IMAFDC` (Standard Integer, Multiply, Atomic, Float, Double, Compressed; bit 15 is 0).
* **E907**: Implements `RV32IMAFDCP` (Adds T-Head Packed-SIMD / DSP math; bit 15 is 1).

In bare-metal C/C++ firmware:
```cpp
uint32_t misa;
asm volatile("csrr %0, misa" : "=r"(misa));

if (misa & (1 << 15)) {
    hal::Trace::printf("[CPU-ID] misa=0x%08x -> XuanTie E907 (DSP / 'P' extension enabled)\n", misa);
} else {
    hal::Trace::printf("[CPU-ID] misa=0x%08x -> XuanTie E906 (RV32IMAFDC standard without 'P')\n", misa);
}
```

### Method 2: Query Machine Architecture ID (`marchid`) & T-Head Model (`mprid`)
Standard RISC-V and Alibaba T-Head custom identification CSRs can be read directly:
```cpp
uint32_t marchid, mprid;
asm volatile("csrr %0, marchid" : "=r"(marchid));
asm volatile("csrr %0, 0xfc0"   : "=r"(mprid)); // T-Head custom CPU ID register

hal::Trace::printf("[CPU-ID] marchid=0x%08x | mprid=0x%08x\n", marchid, mprid);
```
Alibaba T-Head encodes the core family and silicon revision in `mprid` bits `[23:16]` and `[15:0]`.

### Method 3: Query Allwinner Silicon Version Register via Linux Host
From the Linux terminal on the Radxa Cubie A5E board:
```bash
# Read offset 0x0000 (E906_VER_REG) in the RISC-V CFG block:
devmem2 0x07130000 w
```
The register value identifies the Allwinner synthesis version of the RISC-V configuration block.

