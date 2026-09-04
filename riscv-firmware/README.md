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
|  +-------------------------------------+   |  | (RV32IMAFDC + Double FPU + DSP)   |  |  |
|                                            |  | Clock: Up to 200 MHz (MCU_PRCM)   |  |  |
|  +-------------------------------------+   |  +-----------------------------------+  |  |
|  |             NPU Engine              |   |  +-----------------------------------+  |  |
|  |  - 2.0 TOPS VIP9000 (0x07122000)    |   |  | Hardware Message Box (Doorbell)   |  |  |
|  +-------------------------------------+   +-----------------------------------------+  |
|                                                                                         |
|  +-----------------------------------------------------------------------------------+  |
|  |                           Memory Hierarchy & Interconnect                         |  |
|  |  - 64 KB ITCM (0x00000000) & 64 KB DTCM (0x00080000) [E907 Zero-Wait-State Local]  |  |
|  |  - 208 KB Shared SRAM A2 (0x00040000) [Zero-Wait-State Low-Latency Control & IPC]  |  |
|  |  - 320 KB Dedicated MCU SRAM C (0x07130000)                                       |  |
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
* **Memory Subsystem**:
  - **64 KB ITCM** (Instruction Tightly-Coupled Memory @ `0x00000000`, 0 wait states)
  - **64 KB DTCM** (Data Tightly-Coupled Memory @ `0x00080000`, 0 wait states)
  - **256 KB Dedicated MCU SRAM** (`0x07100000`)
  - **Shared System SRAM A2** (`0x00040000`–`0x00073FFF`)
* **Toolchain / ABI**: Target `-march=rv32imafdc_zicsr_zifencei -mabi=ilp32d -mcmodel=medany`.

---

## 2. Test Applications Suite (`apps/`)

Under `apps/`, seven progressive test applications validate core functionality, memory mapping, telemetry, exception handling, and three inter-processor communication paradigms:


```text
apps/
├── testBasic/               # Minimal boot, ITCM execution, and multi-SRAM address writes
├── testBasicTrace0/         # RemoteProc resource table, ASCII startup banner & 1s periodic trace
├── testStringBinaryTrace0/  # Combined ASCII text + packed binary telemetry with hardware FPU
├── testCrash/               # Hardware exception trapping (mtvec) & full register crash dump
├── testPing/                # Fast, low-jitter Direct Shared Memory (Lite-libmetal style) + Linux benchmark
│   └── linux/               # ping_shm Linux host companion benchmark tool
├── testPingRpmsg/           # Standard Linux VirtIO RPMsg (OpenAMP) echo firmware + Linux benchmark
│   └── linux/               # ping_rpmsg Linux host companion benchmark tool
└── testDRAMMsg/             # Hybrid SRAM SPSC Queue + DDR DRAM Payload Buffers + PMP non-cacheable
    └── linux/               # ping_dram Linux host companion benchmark tool
```

---

### App 1: `testBasic`
* **Purpose**: Basic bring-up and memory sanity verification.
* **Functionality**:
  - Boots into ITCM `0x00000000`, configures stack in DTCM `0x00080000`.
  - Writes magic signatures to multiple SRAM locations:
    - SRAM A2: `0x00040000` (`0xDEADBEEF` + counter)
    - SRAM A2: `0x00050000` (`0x52495343` "RISC" + counter)
    - DTCM: `0x00081000` (`0xCAFE1234` + counter)
    - SRAM C: `0x07130000` (`0xAA55AA55` + counter)
  - Continuously increments counters so host Linux can verify life via `devmem 0x00040000 32`.

---

### App 2: `testBasicTrace0`
* **Purpose**: RemoteProc resource table and debugfs trace buffer verification.
* **Functionality**:
  - Declares `.resource_table` section exporting `trace0` buffer.
  - Emits ASCII startup banner.
  - Runs a 1-second periodic loop outputting heartbeat logs:
    ```text
    [E907 Trace0] Heartbeat #1 | Uptime: 1s | Status: OK | SRAM: 0x00000001
    [E907 Trace0] Heartbeat #2 | Uptime: 2s | Status: OK | SRAM: 0x00000002
    ```
  - Read output on Linux target:
    ```bash
    cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
    ```

---

### App 3: `testStringBinaryTrace0`
* **Purpose**: Demonstrates structured telemetry combining formatted ASCII strings with packed binary structures and hardware FPU computation.
* **Functionality**:
  - Utilizes single-precision (`float`) and double-precision (`double`) hardware FPU math (sine wave computation).
  - Populates a 32-byte packed binary `TelemetryPacket` in Shared SRAM A2 (`0x00041000`).
  - Interleaves formatted ASCII telemetry with decoded float values into `trace0`:
    ```text
    [TELM #1] Accel: (0.015, -0.008, 9.811) | FPU Sin: 0.0998 | SRAM: 0x54454C4D
    [TELM #2] Accel: (0.030, -0.016, 9.816) | FPU Sin: 0.1986 | SRAM: 0x54454C4D
    ```

---

### App 4: `testCrash`
* **Purpose**: Fault simulation, trap vector validation, and crash autopsy reporting via RemoteProc.
* **Functionality**:
  - Configures `mtvec` to custom exception trap handler.
  - Emits 3 normal heartbeats before intentionally triggering an illegal instruction trap.
  - Trap handler captures `mepc`, `mcause`, `mtval`, `mstatus`, and full GPR dump (`ra`, `sp`, `gp`, `a0`..`a7`, `t0`..`t6`, `s0`..`s11`).
  - Writes fatal signature `0xDEADF00D` to SRAM A2 (`0x00040000`).
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

### App 5: `testPing` (Fast Direct Shared Memory / Lite-libmetal Style)
* **Purpose**: Ultra-low-latency, zero-copy, deterministic inter-processor communication.
* **Functionality**:
  - Direct lock-free shared SRAM channel (`ShmPingChannel` @ `0x00040000`).
  - Memory-barrier-synchronized doorbell registers (`host_doorbell`, `riscv_doorbell`).
  - Sub-microsecond response latency (~1.5–2.5 $\mu\text{s}$ Round-Trip Time).
* **Linux Companion Tool**: `apps/testPing/linux/ping_shm`
  - High-precision latency benchmarking using `clock_gettime(CLOCK_MONOTONIC_RAW)`.
  - Computes min, avg, max latency, jitter (standard deviation), percentiles (p50, p90, p99, p99.9), and message throughput.

```bash
# Run 100,000 iterations over shared SRAM
sudo ./apps/testPing/linux/ping_shm -n 100000
```

---

### App 6: `testPingRpmsg` (Standard Linux VirtIO RPMsg)
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

### App 7: `testDRAMMsg` (Hybrid SRAM SPSC Queue / DDR DRAM Payload Buffers)
* **Purpose**: High-throughput message streaming moving payload buffers to DDR DRAM while keeping SPSC control structures in ultra-low-latency SRAM.
* **Architecture**:
```text
+-----------------------------------------------------------------------------+
|                          HYBRID MEMORY IPC ARCHITECTURE                     |
|                                                                             |
|   +---------------------------------------------------------------------+   |
|   |         SHARED SYSTEM SRAM A2 (0x00040000) - CONTROL PATH           |   |
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
  - Control block (`DramSpscControlBlock` @ `0x00040000`) in fast zero-wait-state SRAM A2.
  - 1 MB payload buffer pool in DDR DRAM Carveout (`0x48100000`).
  - Configures RISC-V Physical Memory Protection (PMP) and XuanTie Cache maintenance (`mhcr`, `mcor`, `dcache.iva`, `dcache.cpa`) for DMA-coherent uncached/strongly-ordered access.
* **Linux Companion Tool**: `apps/testDRAMMsg/linux/ping_dram`
  - Measures throughput (MB/sec), latency, and jitter for variable payload sizes (64B to 4096B).

```bash
# Run 10,000 iterations with 1024-byte payloads over DDR DRAM
sudo ./apps/testDRAMMsg/linux/ping_dram -n 10000 -s 1024
```


---

## 3. Communication Paradigm & IPC Architecture Comparison

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

## 4. How to Build

### Build Everything (Firmware + Linux Tools)
```bash
# From riscv-firmware directory:
make

# Or from apps directory:
make -C apps
```

### Build a Specific Firmware Application
```bash
make -C apps/testPing
make -C apps/testDRAMMsg
make -C apps/testPingRpmsg
```

### Build Linux Host Companion Tools
```bash
make -C apps/testPing/linux
make -C apps/testDRAMMsg/linux
make -C apps/testPingRpmsg/linux
```

---

## 5. How to Deploy & Run on Target (Cubie A5E)

All compiled firmware ELF files are staged into `riscv-firmware/bin/` with distinct names:
* `testBasic.elf`
* `testBasicTrace0.elf`
* `testStringBinaryTrace0.elf`
* `testCrash.elf`
* `testPing.elf`
* `testPingRpmsg.elf`
* `testDRAMMsg.elf`


### 1. Deploy All Firmware ELFs to Target
```bash
# Push all firmware ELFs and host benchmark tools to running board:
./project-cubie-a5e/scripts/push-riscv-firmware.sh <TARGET_IP>
```
Or copy manually via SCP:
```bash
scp riscv-firmware/bin/*.elf root@<TARGET_IP>:/lib/firmware/
scp riscv-firmware/bin/ping_shm riscv-firmware/bin/ping_rpmsg root@<TARGET_IP>:/usr/local/bin/
```

### 2. Switch Between Firmware Images on Target
On the Linux target shell (`root@cubie-a5e`), switch between any of the installed ELFs at runtime:

```bash
# Stop current firmware
echo stop > /sys/class/remoteproc/remoteproc0/state

# 1. Load testBasicTrace0
echo "testBasicTrace0.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# 2. Switch to testPing (Direct Shared Memory)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
ping_shm -n 50000

# 3. Switch to testPingRpmsg (Linux RPMsg)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testPingRpmsg.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
ping_rpmsg -n 5000

# 4. Switch to testCrash (Exception Trap Autopsy)
echo stop > /sys/class/remoteproc/remoteproc0/state
echo "testCrash.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

