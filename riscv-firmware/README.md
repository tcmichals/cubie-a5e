# RISC-V Co-Processor Firmware Overview & Architecture

This directory contains bare-metal firmware, runtime drivers, test applications, and Linux host companion tools for the **XuanTie E907 RISC-V Co-Processor (up to 200 MHz)** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**.

> [!TIP]
> ### 📖 THE SOURCE GO-TO GUIDE FOR TESTS: [`tests.md`](tests.md)
> If you are building, running, debugging, or benchmarking the RISC-V firmware tests (`testBasic`, `testStringBinaryTrace0`, `testCrash`, `testPing`, `testPingRpmsg`, `testDRAMMsg`), **use [`tests.md`](tests.md) as your primary source go-to guide**.
>
> While this `README.md` covers the broader architectural background and SoC interconnect overview, **[`tests.md`](tests.md)** provides the direct, concrete developer reference: exact memory maps, build commands, step-by-step target execution commands (`devmem2`, `trace0`, `ping_shm`, `ping_uio.py`), and crash dump forensics.

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
|  |  +-------------------------------------+   |  +-----------------------------------+  |  |
|  |  |             NPU Engine              |   |  +-----------------------------------+  |  |
|  |  |  - 2.0 TOPS VIP9000 (0x07122000)    |   |  | Hardware Message Box (Doorbell)   |  |  |
|  |  +-------------------------------------+   |  +-----------------------------------------+  |
|  |                                                                                         |
|  |  +-----------------------------------------------------------------------------------+  |
|  |  |                           Memory Hierarchy & Interconnect                         |  |
|  |  |  - 512 KB Dual-Bank On-Chip SRAM (0x3FFC0000 & 0x40000000) [Exclusive E907 Firmware]  |  |
|  |  |  - 128 KB HiFi4 DSP Local RAM (0x00020000) [DSP Instruction/Data RAM Only]        |  |
|  |  |  - 160 KB Secure SRAM A2 (0x00044000) [OP-TEE / TF-A BL31 Firewalled Memory]      |  |
|  |  |  - 4 KB RISC-V CFG Control Block (0x07130000) [STA_ADD_REG @ 0x204, WORK_MODE]   |  |
|  |  |  - 1 MB DDR DMA Payload Pool (0x48000000) [Non-Cacheable Streaming Payloads Only] |  |
|  |  |  - Up to 4 GiB LPDDR4/4X System RAM (0x40000000 Host Physical)                    |  |
|  |  +-----------------------------------------------------------------------------------+  |
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

## 2. Memory Map of XuanTie E907 on Allwinner T527 / A523 (Linux Perspective)

### 2.1 Memory Subsystem Mapping

| Memory Region | Linux Host (ARM64) Physical Address | E907 RISC-V Core Address | Size | Latency & Usage |
| :--- | :--- | :--- | :--- | :--- |
| **SRAM Space 0 (`r_sram`)** | **`0x07280000`** | **`0x3FFC0000`** | **256 KB** | **Primary E907 Boot & Execution Pool** (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`). Zero wait states. |
| **SRAM Space 1 (`r_sram1`)** | **`0x072C0000`** | **`0x40000000`** | **256 KB** | **Secondary High-Speed SRAM Bank** enabled via `REMAP_CTRL_REG[1] = 1`. Shared IPC/buffers. |
| **RISC-V CFG Control Block** | **`0x07130000`** | **`0x07130000`** | **4 KB** | Hardware registers: `0x0000` (`VER_REG`), `0x0204` (`STA_ADD_REG` Boot vector defaults to `0x3FFC0000`), `0x0248` (`WORK_MODE_REG`) |
| **DDR DRAM DMA Carveout** | **`0x48000000`** | **`0x48000000`** | **1 MB** | Dedicated high-bandwidth payload pool (`testDRAMMsg`, Device Tree `vdev@48000000`) |

> [!IMPORTANT]
> ### TRACE BUFFER LOCATION: STRICTLY ON-CHIP SRAM, NEVER DDR
> The RemoteProc trace buffer (`g_rproc_trace_buffer[4096]`, exposed to userspace as `/sys/kernel/debug/remoteproc/remoteproc0/trace0`) **must reside exclusively in on-chip SRAM (`SRAM_A3`)**, placed into the `.trace_buffer` section (`0x3FFC0000` in `e907_sram.ld` or `0x40000000` in `e907_ddr.ld`):
> 1. **Early Boot & Determinism**: The E907 logs boot vectors, clock status, and peripheral bring-up immediately upon reset—long before DDR is initialized, or even when DDR is powered down in low-power sleep.
> 2. **Zero Wait States**: On-chip SRAM guarantees single-cycle logging latency without DRAM bus contention, page misses, or memory refresh stalls.
> 3. **Crash Survivability**: When a fatal exception or illegal instruction trap occurs (`testCrash`), crash register dumps (`mepc`, `mcause`, `sp`) are safely preserved into SRAM even if the DDR controller has locked up or crashed.
> 4. **Host Read Access**: Linux `sunxi_rproc.c` maps `r_sram` via `devm_ioremap_wc` (normal non-cacheable memory on ARM64), allowing debugfs `rproc_trace_read()` to perform byte-level reads directly without external aborts.

> [!TIP]
> ### 🔍 STEP-BY-STEP: MOUNTING DEBUGFS TO ACCESS `trace0`
> On minimal root filesystems (such as Buildroot), `debugfs` is not always mounted automatically on boot. If `/sys/kernel/debug/remoteproc/` does not exist or appears empty:
> 
> 1. **Check if `debugfs` is mounted**:
>    ```bash
>    mount | grep debugfs
>    ```
> 2. **Mount `debugfs`**:
>    ```bash
>    mount -t debugfs none /sys/kernel/debug
>    ```
> 3. **Verify `trace0` is available**:
>    ```bash
>    ls -l /sys/kernel/debug/remoteproc/remoteproc0/trace0
>    ```
> 4. **Read co-processor log output**:
>    ```bash
>    cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
>    ```
> 5. *(Optional)* **Persist mount across reboots via `/etc/fstab`**:
>    ```text
>    debugfs  /sys/kernel/debug  debugfs  defaults  0  0
>    ```

### 2.2 Silicon Hardware Ownership & Off-Limits Regions

> [!IMPORTANT]
> ### HARDWARE TRUTH: 0x00020000 IS HIFI4 DSP MEMORY
> 1. **`0x00020000` (128 KB) is HiFi4 DSP Memory**: This physical silicon is wired directly to the Cadence HiFi4 DSP as its local Instruction/Data RAM. If the E907 attempts to boot or execute from here, it will collide with the DSP and corrupt DSP audio algorithms.
> 2. **`0x00044000` (160 KB) is OP-TEE / TrustZone Memory (`SRAM A2`)**: This memory is locked by the hardware firewall for secure booting, TF-A BL31, and OP-TEE.
> 3. **E907 Firmware Lives in On-Chip SRAM**: Continuous 512 KB SRAM window: Space 0 (`0x3FFC0000`) and Space 1 (`0x40000000`).

### 2.3 Allwinner On-Chip SRAM Partitioning & Hardware Allocation

| SRAM Bank | Physical Base (Host) | Core Address (E907) | Size | Hardware Owner | Primary Purpose & Usage | Allowed for RISC-V E907? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`BROM`** | `0x00000000` | Unmapped | 128 KB | SoC Hardware | Silicon Mask ROM; executes first instruction on power-on reset | ❌ **No** (BootROM) |
| **`DSP RAM` (`PubSRAM C`)** | `0x00020000` | Unmapped | 128 KB | **Cadence HiFi4 DSP** | **DSP Instruction/Data RAM**. Host IPC peek only via `REMAP_CTRL_REG[0]`. | ❌ **STRICTLY PROHIBITED** (DSP Collision) |
| **`SRAM A2`** | `0x00044000` | Unmapped | 160 KB | **Secure EL3 (TF-A) / OP-TEE** | **Secure World (TF-A BL31, OP-TEE, PSCI 1.1 power management, CPU suspend/hotplug)** | ❌ **STRICTLY PROHIBITED** (TrustZone Firewall) |
| **`SRAM Space 0` (`r_sram`)** | `0x07280000` | `0x3FFC0000` | 256 KB | **XuanTie E907** | **Primary zero-wait-state execution window (`.vectors`, `.text`, `.data`, `.stack`, `.trace_buffer`)** | ✅ **YES (Primary E907 Pool)** |
| **`SRAM Space 1` (`r_sram1`)** | `0x072c0000` | `0x40000000` | 256 KB | **XuanTie E907 / Shared** | **Secondary zero-wait-state SRAM bank enabled via REMAP_CTRL_REG[1]=1** | ✅ **YES (Secondary E907 Pool)** |
| **`DSP IRAM/DRAM`**| `0x00400000` | Unmapped | 128 KB | **Cadence HiFi4 DSP** | **Private DSP Execution & Audio Buffers** | ❌ **STRICTLY PROHIBITED** (DSP Local RAM) |
| **`CFG Regs`** | `0x07130000` | `0x07130000` | 4 KB | **Host & E907 Control** | Hardware version (`0x00`), Boot entry vector (`0x204`), Work Mode / Lockup status (`0x248`) | ✅ **YES (Registers Only, Not SRAM)** |
| **`dram_dma`**| `0x48100000`| `0x48100000` | 1 MB | Linux RemoteProc | Non-cacheable DDR DMA payload buffer pool strictly for streaming IPC payloads (`testDRAMMsg`) | ✅ **YES (Streaming Carveout Only)** |

### 2.4 Visual Address Translation Architecture

```
+===================================================================================+
|                     ALLWINNER T527 ADDRESS SPACE MAPPING                          |
+===================================================================================+

  LINUX HOST (ARM64) PHYSICAL VIEW                  XUANTIE E907 RISC-V CORE VIEW
  ================================                  =============================
  0x07280000 - 0x072BFFFF [ 256 KB ] ─────────────> 0x3FFC0000 - 0x3FFFFFFF (SRAM Space 0)
    (Mapped via RemoteProc "r_sram")                  (E907 Primary Boot, .vectors, .text, .data, stack, .trace_buffer)

  0x072C0000 - 0x072FFFFF [ 256 KB ] ─────────────> 0x40000000 - 0x4003FFFF (SRAM Space 1)
    (Mapped via RemoteProc "r_sram1")                 (SRAM Space 1 via REMAP_CTRL_REG[1] = 1, IPC, .trace_buffer)

  0x00020000 - 0x0003FFFF [ 128 KB ] ─────────────> [ CADENCE HIFI4 DSP ONLY - FORBIDDEN TO E907 ]
    (DSP Local Instruction/Data RAM)                  (Host IPC peek only via REMAP_CTRL_REG[0] = 1)

  0x00044000 - 0x00067FFF [ 160 KB ] ─────────────> [ OP-TEE / TRUSTZONE SRAM A2 - FORBIDDEN ]
    (Firewalled by TrustZone SPC)                     (TF-A BL31 & OP-TEE execution only)

  0x07130000 - 0x07130FFF [   4 KB ] ─────────────> 0x07130000 - 0x07130FFF (CFG Regs)
    (STA_ADD_REG 0x204, WORK_MODE 0x248)              (Control & Lockup Status)

  0x48100000 - 0x481FFFFF [   1 MB ] ─────────────> 0x48100000 - 0x481FFFFF (DDR DMA Pool)
    (DMA Reserved Memory Pool)                        (PMP Non-Cacheable Streaming Payloads Only)
+===================================================================================+
```

### 2.5 How Linux RemoteProc (`sunxi_rproc.c`) Routes Firmware ELFs

When Linux RemoteProc loads a firmware ELF:
1. **Dedicated SRAM Space 0 (`0x3FFC0000`)**:
   Core Device Address `0x3FFC0000` is translated by `sunxi_rproc_da_to_va()` directly into `priv->r_sram_va` (host physical `0x07280000`). This is where all `.vectors`, `.text`, `.rodata`, `.data`, `.bss`, `.stack`, and `.trace_buffer` reside.
2. **Dedicated SRAM Space 1 (`0x40000000`)**:
   Core Device Address `0x40000000` is translated directly into `priv->r_sram1_va` (host physical `0x072c0000`). RemoteProc un-gates this bank by writing `1` to `REMAP_CTRL_REG` Bit 1 prior to loading segments.
3. **Hard Error Guards for Forbidden Memory**:
   `sunxi_rproc_da_to_va()` rejects any attempt to load into `< 0x00020000` (BROM/fake TCM), `0x00020000`–`0x0003FFFF` (HiFi4 DSP memory), `0x00040000`–`0x00067FFF` (OP-TEE SRAM A2), or `0x00400000`–`0x0044FFFF` (DSP secondary RAM).
4. **RISC-V CFG Controller (`0x07130000`)**:
   Hardware control registers (not writable SRAM). On start, the driver writes the ELF entry point (`0x3FFC0000`) to `STA_ADD_REG` (`0x07130204`). Core status and lockup can be checked at `WORK_MODE_REG` (`0x07130248`).
5. **DDR Streaming DMA Carveout (`0x48100000`)**:
   Directly mapped into kernel virtual address space and accessed via non-cached DMA coherent mappings. Reserved strictly for bulk streaming payload transfers (`testDRAMMsg`). Control blocks, descriptors, and trace buffers (`trace0`) MUST NEVER be in DDR; they reside in deterministic on-chip SRAM.

### 2.6 Pure SRAM Architecture (No ITCM / No DTCM on E907)

1. **Zero TCM in Silicon**: Unlike older Allwinner chips (such as Allwinner D1 / V853) that had tightly-coupled memories at `0x00000000` (ITCM) and `0x00080000` (DTCM), the XuanTie E907 on the T527 / A527 **implements NO ITCM and NO DTCM**. Addresses `0x07110000` and `0x07120000` do not exist in silicon.
2. **Hardware Lockup Reality**: Setting `STA_ADD_REG` to `0x00000000` or legacy TCM addresses causes an immediate bus abort on instruction fetch, double-faulting into silicon **Hardware Lockup** (`WORK_MODE_REG 0x07130248 = 0x0000000B`, Bit 3 `BIT_LOCK_STA = 1`).
3. **Execution Windows**: The E907 runs out of continuous 512 KB on-chip SRAM: **Space 0 (`0x3FFC0000`)** and **Space 1 (`0x40000000`)**.

---

## 3. Test Applications Suite (`apps/`) & Performance Roadmap

> 💡 **For the complete, unabridged developer guide with all step-by-step terminal commands, memory addresses, and benchmark execution, refer directly to [`tests.md`](tests.md).**

The firmware test suite follows a progressive **"Walk -> Run"** architecture, starting with basic bring-up sanity and trace logging before advancing to high-throughput, low-latency IPC:

```text
apps/
├── [PHASE 1: WALK - BOOT, TRACE & TELEMETRY]
│   ├── testBasic/               # Minimal boot in on-chip SRAM (0x3FFC0000) & live counter increment
│   ├── testStringBinaryTrace0/  # SRAM trace0 buffer, mixed ASCII text + packed binary telemetry + FPU
│   ├── testCrash/               # Hardware exception trapping (mtvec), 0xDEADF00D signature & crash dump
│   └── exampleRiscv/            # Minimal reference template with HAL timer delay
│
└── [PHASE 2: RUN - GETTING FASTER CODE & REAL-TIME IPC]
    ├── testPing/                # Ultra-low-latency (~2us) Direct SRAM SPSC Queue + Linux benchmark
    │   └── linux/               # ping_shm and ping_uio Linux host companion benchmark tools
    ├── testPingRpmsg/           # Standards-based Linux VirtIO RPMsg (hal::Rpmsg) + Mailbox Doorbells
    │   └── linux/               # ping_rpmsg Linux host companion benchmark tool
    └── testDRAMMsg/             # High-throughput Hybrid SRAM Control + DDR DRAM DMA Payload Buffers
        └── linux/               # ping_dram Linux host companion benchmark tool
```

---

### App 1: `testBasic`
* **Purpose**: Basic bring-up, memory sanity, and linker-script layout validation.
* **Functionality**:
  - Boots into on-chip SRAM Space 0 `0x3FFC0000` (`e907_sram.ld`), configures stack at top of SRAM.
  - Maps variables into dedicated linker sections (`.sram_c_loc1`, `.sram_c_loc2`) within SRAM.
  - Continuously increments counters so host Linux can verify life via debugfs `trace0` or `devmem2 0x07287100 w` (Host physical on A527).

---

### App 2: `testStringBinaryTrace0` (Mixed String & Binary Telemetry)
* **Purpose**: Demonstrates structured telemetry streaming over `trace0` combining formatted ASCII strings with packed binary structures and hardware FPU computation.
* **Functionality**:
  - Declares `.resource_table` section exporting `trace0` buffer in on-chip SRAM Space 0 (`0x3FFC0000` + offset).
  - Completely eliminates DDR caching issues—trace writes are immediately visible to Linux without software cache flushes.
  - Utilizes single-precision (`float`) hardware FPU math (sine wave computation).
  - Populates a 32-byte packed binary `TelemetryPacket` in SRAM (`.sram_c`).
  - Interleaves three data streams directly into the `trace0` buffer:
    1. **ASCII String Log (`STRING:`)**: Human-readable log line with formatted float values.
    2. **Framed Binary Struct (`BINARY:`)**: Raw packed binary struct (`TelemetryPacket`) for high-speed programmatic ingestion.
    3. **Hex Dump (`HEXDUMP:`)**: Terminal-inspectable formatted hex dump of the binary packet.

#### Standard RemoteProc Python Monitor (`apps/testStringBinaryTrace0/monitor_trace.py`)
Run the standard debugfs monitor on the Linux host to stream and decode the mixed trace (requires debugfs mounted: `mount -t debugfs none /sys/kernel/debug`):

```bash
# Mount debugfs if not already mounted:
mount -t debugfs none /sys/kernel/debug 2>/dev/null || true

python3 /usr/local/bin/monitor_trace.py /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

#### Lite / Fast Direct SRAM Python Monitor (`apps/testStringBinaryTrace0/fast_sram_telemetry.py`)
For ultra-high-rate telemetry (>1,000 Hz) bypassing the kernel filesystem layer, `fast_sram_telemetry.py` reads SRAM directly via physical mmap:

```bash
python3 /usr/local/bin/fast_sram_telemetry.py
```

---

### App 3: `testCrash`
* **Purpose**: Fault simulation, trap vector validation, and crash autopsy reporting via RemoteProc.
* **Functionality**:
  - Configures `mtvec` to `default_trap_entry` in `startup.S`.
  - Emits 3 normal heartbeats before intentionally triggering an illegal instruction trap (`asm volatile(".word 0x00000000")` at `main.cpp:51`).
  - Trap handler captures `mepc`, `mcause`, `mtval`, `mstatus`, and full GPR dump (`ra`, `sp`, `gp`, `a0`..`a7`, `t0`..`t6`, `s0`..`s11`).
  - Writes fatal signature `0xDEADF00D` to the top 256 bytes of SRAM Space 0 (`0x3FFFFF00` Core DA / `0x072BFF00` Host physical on A527).
  - Formats an exhaustive crash autopsy and dumps it directly into `trace0`:
    ```text
    ################################################################
      FATAL HARDWARE EXCEPTION DETECTED ON XUANTIE E907 RISC-V CORE 
    ################################################################
      Cause Name : Illegal instruction
      mcause     : 0x00000002
      mepc (PC)  : 0x4000080e
      mtval      : 0x00000000
      mstatus    : 0x00001880

    --- General Purpose Register (GPR) Dump ---
      ra (x1) = 0x40000812  sp (x2) = 0x4003fea0  gp (x3) = 0x40008400
      ...
    ################################################################
      Core halted safely. Inspect /sys/.../trace0 or SRAM (0x3FFFFF00)
    ################################################################
    ```

---

### App 4: `testPing` (Fast Direct Shared Memory / Lite-libmetal UIO Style)
* **Purpose**: Ultra-low-latency, zero-copy, deterministic inter-processor communication.
* **Functionality**:
  - Direct lock-free shared SRAM channel (`ShmPingChannel` in on-chip SRAM Space 0 `0x3FFC0000`).
  - Dual operational modes:
    1. **Event-Driven UIO Doorbell Mode (Recommended)**: Enabled via `cubie-a5e-uio` overlay in `/boot/config.txt`. Converts the hardware Mailbox (`0x03003000`) into a generic UIO device (`/dev/uio0`). Linux host blocks asynchronously on `select.epoll()` with **0% idle CPU burn**; XuanTie E907 pulses GIC SPI 147 interrupt to wake host.
    2. **Direct Memory Polling Baseline**: Reads SRAM directly via kernel UIO mapping and polls on memory flags for theoretical raw bus latency measurements.
* **Linux Companion Tools**:
  - **`ping_uio.py`**: Event-driven Python Lite-libmetal client using `select.epoll()`. Maps `map0` (Mailbox MMIO) and `map1` (MCU SRAM) directly from `/dev/uio0` (no root privileges required).
  - **`ping_shm`**: C++ high-precision latency benchmarking tool using `clock_gettime(CLOCK_MONOTONIC_RAW)`.

```bash
# Mode 1: Event-driven UIO Doorbell (0% idle CPU burn via epoll)
ping_uio.py -n 50000

# Mode 2: Direct Shared SRAM memory-polling baseline
ping_shm -n 100000
```

---

### App 5: `testPingRpmsg` (Standard Linux VirtIO RPMsg over Coherent DDR Buffers)
* **Purpose**: Standard Linux kernel RPMsg framework communication (`virtio_rpmsg_bus`) using hardware Mailbox Channel 8 doorbells.
* **Functionality**:
  - RemoteProc resource table declares `RSC_VDEV` (VirtIO ID 7) with 2 vrings (16 descriptors each) using `.da = FW_RSC_ADDR_ANY`.
  - Allocated dynamically by Linux kernel (`dma_alloc_coherent`) in **physical DDR DRAM** (`0xf2f80000` Vring 0, `0xf2f82000` Vring 1, `0xf2f84000` payload buffers).
  - Dynamically announces Name Service endpoint `"rpmsg-ping-channel"` (address 1024), automatically spawning `/dev/rpmsg0`.
  - Processes incoming ping packets from Linux `/dev/rpmsg0` and responds with pong packets in DDR buffers.
* **Linux Companion Tool**: `ping_rpmsg`
  - Connects to `/dev/rpmsg0` or creates endpoint via `/dev/rpmsg_ctrl0`.
  - Evaluates standard kernel RPMsg driver latency and throughput (116.68 $\mu$s avg RTT, 3,207 msgs/sec).

```bash
# Run 1,000 iterations over Linux RPMsg
ping_rpmsg -n 1000
```

---

### App 6: `testDRAMMsg` (Hybrid SRAM SPSC Queue / DDR DRAM Carveout Streaming)
* **Purpose**: High-throughput message streaming moving large payload buffers to dedicated DDR DRAM while keeping SPSC control structures in ultra-low-latency on-chip SRAM.
* **Architecture**:
```text
+-----------------------------------------------------------------------------+
|                          HYBRID MEMORY IPC ARCHITECTURE                     |
|                                                                             |
|   +---------------------------------------------------------------------+   |
|   |         ON-CHIP SRAM (0x3FFF2000 / 0x072B2000) - CONTROL PATH       |   |
|   |  - SPSC Head & Tail Pointers (Atomic single-word updates)           |   |
|   |  - Producer/Consumer Doorbells & Monotonic Sequence Counters        |   |
|   |  - 16-slot TX/RX Descriptor Rings (Holds DRAM Buffer Offsets & Len) |   |
|   |  - Zero-wait-state access for sub-microsecond descriptor exchanges  |   |
|   +---------------------------------------------------------------------+   |
|                                     │ (Payload pointers & offsets)          |
|                                     ▼                                       |
|   +---------------------------------------------------------------------+   |
|   |             DDR DRAM CARVEOUT (0x48000000) - DATA PATH              |   |
|   |  - Non-Cacheable DMA Carveout / Reserved Memory Window (1 MB)       |   |
|   |  - 16x Host->RISC-V Payload Buffers (Up to 4 KB each)               |   |
|   |  - 16x RISC-V->Host Payload Buffers (Up to 4 KB each)               |   |
|   |  - PMP / Memory Fences configured for zero cache stalls             |   |
|   +---------------------------------------------------------------------+   |
+-----------------------------------------------------------------------------+
```
* **Functionality**:
  - Control block (`DramSpscControlBlock`) in fast on-chip SRAM Space 0 (`0x3FFF2000`).
  - 1 MB payload buffer pool in dedicated DDR DRAM Carveout (`0x48000000`).
  - Configures RISC-V Physical Memory Protection (PMP) and memory barriers (`fence rw, rw`) for DMA-coherent access.
* **Linux Companion Tool**: `ping_dram`
  - Measures throughput (MB/sec), latency, and jitter for variable payload sizes (64B to 4096B).

```bash
# Run 1,000 iterations with 2048-byte payloads over DDR DRAM
ping_dram -n 1000 -s 2048
```

---

## 4. Communication Paradigm & IPC Architecture Comparison

| IPC Category | **[STANDARDS-BASED]**<br>Lite-libmetal / `hal::Rpmsg` (`testPingRpmsg`) | **[CUSTOM STREAMING]**<br>Hybrid SRAM / DDR (`testDRAMMsg`) | **[CUSTOM LOW-LATENCY]**<br>Dedicated MCU SRAM (`testPing` / `hal::SpscQueue`) |
| :--- | :--- | :--- | :--- |
| **Architecture Family** | **Standards-Based (VirtIO / OpenAMP)** | **Custom Hardware-Direct HAL** | **Custom Hardware-Direct HAL** |
| **Control Path** | VirtIO vrings in DDR DRAM (`0xf2f80000` / `0xf2f82000`) | Lock-Free SPSC in SRAM (`0x3FFF2000` / `0x072B2000`) | Lock-Free SPSC in SRAM (`0x3FFC0000` / `0x07280000`) |
| **Data Path** | **RPMsg DMA buffers in DDR (`0xf2f84000`)** | **DDR DRAM Carveout (`0x48000000`, 1 MB)** | MCU Dedicated SRAM (64B frames) |
| **Linux Driver / Stack**| `virtio_rpmsg_bus` + `rpmsg_char` | Kernel UIO / Reserved Memory Carveout | Direct memory mmap / `uio_pdrv_genirq` |
| **Linux Ecosystem**     | Standard (`/dev/rpmsg0`, `/dev/ttyRPMSG0`) | Custom High-Speed API / `ping_dram` | Event-driven `ping_uio.py` (`select.epoll()`) / `ping_shm` |
| **Firmware Code Size**  | **~3 KB** (zero dynamic allocation) | **~3 KB** (zero dynamic allocation) | **< 1 KB** (header-only C++ template) |
| **Typical RTT Latency** | **116.68 $\mu\text{s}$ avg** (Min: 107.5 $\mu\text{s}$) | **381.84 $\mu\text{s}$** (2 KB payload) | **3.985 $\mu\text{s}$ avg** (Min: 3.54 $\mu\text{s}$) |
| **Jitter (StdDev)**     | **13.91 $\mu\text{s}$** (PREEMPT_RT kernel) | **58.20 $\mu\text{s}$** | **1.12 $\mu\text{s}$** (Hardware SRAM determinism) |
| **Max Payload Size**    | Medium (512 B default) | **Large (Up to 4 KB per frame, 1 MB pool)** | Small (44–64 B, SRAM capacity bounded) |
| **Throughput Bandwidth**| **3,207.2 msgs/sec** (~376 KB/s) | **2,374.2 msgs/sec** (4.64 MB/s @ 2KB) | **189,733.8 msgs/sec** (8.35 MB/s) |
| **Success Rate (1,000 pings)** | **100% (0 timeouts)** | **100% (0 timeouts)** | **100% (0 timeouts)** |
| **Target Use Case**     | Generic standard OS interop | Point-clouds, camera frames, flight logs | Hard real-time motor control, PID loops |

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

Once logged into the board (via serial or SSH), all tools and ELFs are in your system `$PATH`.

#### Step 0: Ensure `debugfs` is Mounted (Required to Access `trace0`)
On minimal root filesystems (like Buildroot), `debugfs` must be mounted to access RemoteProc trace logs:
```bash
# Check if debugfs is already mounted; mount it if not:
mount | grep debugfs || mount -t debugfs none /sys/kernel/debug
```

#### Step 1: Switch and Run Firmware Applications

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

# View raw trace0 buffer directly:
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# Stream and decode via standard RemoteProc debugfs trace:
monitor_trace.py

# Or high-rate zero-copy direct SRAM reader:
fast_sram_telemetry.py
```
