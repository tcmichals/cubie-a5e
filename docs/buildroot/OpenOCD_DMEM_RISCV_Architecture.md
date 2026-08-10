# On-Chip Direct Memory-Mapped Debug Access (DMEM) Architecture for ARM & RISC-V SoCs

## Executive Summary

This document details the technical architecture for **JTAG-less, on-chip debugging** of real-time co-processors (such as the XuanTie E907 RISC-V core on the Allwinner T527 / Radxa Cubie A5E) directly from the primary ARM Linux host OS.

By leveraging **Direct Memory-Mapped I/O (MMIO)** via `/dev/mem`, the ARM Cortex-A55 host reads and writes hardware Debug Module registers directly over the SoC's internal system bus. This eliminates the need for external physical JTAG hardware probes (e.g., SEGGER J-Link or T-Head CK-Link), breadboard wiring, or hardware header soldering.

---

## 1. Architectural Overview

In heterogeneous asymmetric multiprocessing (AMP) SoCs, the primary Linux CPU and real-time co-processors share an internal system interconnect (AHB/AXI bus).

```text
  ┌────────────────────────────────────────────────────────┐
  │                    GDB Debugger                        │
  │   (e.g. riscv-none-elf-gdb / gdb set arch riscv:rv32)  │
  └───────────────────────────┬────────────────────────────┘
                              │ 
                              │  GDB Remote Serial Protocol (RSP)
                              │  (TCP Port 3333)
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │                        OpenOCD                         │
  │    (Translates GDB commands into RISC-V DM actions)   │
  └───────────────────────────┬────────────────────────────┘
                              │ 
                              │  remote_bitbang TCP Protocol (Port 9999)
                              │  OR Native OpenOCD `dmem` Driver
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │             rbb_server (C++20 Real-Time Daemon)        │
  │     On-Chip Direct MMIO Debug Access Bridge (/dev/mem) │
  └───────────────────────────┬────────────────────────────┘
                              │ 
                              │  Physical AHB Bus Access (/dev/mem)
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │     XuanTie E907 RISC-V Hardware Debug Module (DM)     │
  │                (Physical Address 0x07090000)           │
  └────────────────────────────────────────────────────────┘
```

---

## 2. Direct Memory-Mapped Access (`/dev/mem` / `devmem`)

### What is `/dev/mem`?
`/dev/mem` (short for **Device Memory**) is a special Linux character device file that provides raw access to physical memory addresses on the SoC bus.

### How `mmap()` Bypasses Kernel Overhead
When `rbb_server` or OpenOCD opens `/dev/mem` and calls `mmap()`:
```cpp
// 1. Open raw physical memory device
int fd = open("/dev/mem", O_RDWR | O_SYNC);

// 2. Map physical base address 0x07090000 into virtual memory pointer
volatile uint32_t* dm_base = (volatile uint32_t*)mmap(
    NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x07090000
);

// 3. Direct 32-bit MMIO bus access (executes raw ARM64 LDR/STR instructions)
uint32_t status = dm_base[0x11 / 4]; // Read dmstatus register (offset 0x11)
```
Once mapped, reading and writing to pointer offsets executes raw ARM64 bus instructions (`LDR`/`STR`) at **zero syscall latency**.

---

## 3. Comparison: OpenOCD `--enable-dmem` vs. RISC-V `rbb_server`

### OpenOCD `--enable-dmem` Driver (ARM CoreSight)
OpenOCD contains a built-in driver enabled via `./configure --enable-dmem`:
- **Target**: Created by TI/Linaro for TI K3 / AM62x / BeagleBoard platforms.
- **Protocol**: Maps **ARM CoreSight DAPBUS** registers via `/dev/mem` (base address `0x2B000000`).
- **Commands**: `dmem device /dev/mem`, `dmem base_address 0x2B000000`.

### `rbb_server` (RISC-V 0.13 Debug Module Bridge)
Created for this repository to bridge OpenOCD and the XuanTie E907 RISC-V core:
- **Target**: XuanTie E907 RISC-V Co-processor (Allwinner T527 / Radxa Cubie A5E).
- **Protocol**: Maps **RISC-V 0.13 Debug Module (DM)** registers via `/dev/mem` (base address `0x07090000`).
- **Real-Time Features**:
  - Pinned to isolated CPU core 7 (`pthread_setaffinity_np`).
  - POSIX Real-Time Priority (`SCHED_FIFO` priority 90).
  - Memory locking (`mlockall(MCL_CURRENT | MCL_FUTURE)`) to prevent page fault jitter.

---

## 4. Standard RISC-V 0.13 Debug Module (DM) Registers

The RISC-V External Debug Specification 0.13 standardizes the Debug Module register layout across all compliant RISC-V cores:

| Register Name | Offset | Function |
| :--- | :--- | :--- |
| **`data0` .. `data3`** | `0x04` .. `0x07` | Transfers data / register values between GDB and RISC-V core |
| **`dmcontrol`** | `0x10` | Asserts `haltreq` (pause core) or `resumereq` (resume core) |
| **`dmstatus`** | `0x11` | Reads core execution state (e.g., `0x00004010` = halted & active) |
| **`abstractcs`** | `0x16` | Abstract command control and status |
| **`command`** | `0x17` | Executes abstract commands to read/write GPRs (`x0`–`x31`), `pc`, and CSRs |

---

## 5. Verification Commands on Target Board

### Step 1: Boot RISC-V Core via RemoteProc
```bash
echo "riscv-firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
```

### Step 2: Un-gate MCU CCU Clocks & Probe Debug Module
```bash
probe_riscv_debug.sh
# Verification: devmem 0x07090000 32 returns 0x00004010
```

### Step 3: Launch Debug Bridge & OpenOCD
```bash
rbb_server 0x07090000 &
openocd -f /etc/openocd/openocd_t527_local.cfg &
```

### Step 4: Interactive GDB Debugging
```bash
gdb /lib/firmware/riscv-firmware.elf
```
```gdb
(gdb) set architecture riscv:rv32
(gdb) target remote localhost:3333
(gdb) break main
(gdb) info registers
(gdb) continue
```

---

## 6. Upstream Roadmap & Hackster.io Article Plan

### Hackster.io Article Pitch
- **Title**: *"JTAG Without Wires: Live Debugging a RISC-V Co-Processor from ARM Linux on the Allwinner T527"*
- **Key Highlight**: Demonstrating On-Chip Direct MMIO Debug Access on cheap hybrid SoCs.
- **Industry Context**: Comparing BeagleBoard TI AM62x ARM CoreSight MMIO vs. Allwinner T527 RISC-V MMIO.

### OpenOCD Upstream Contribution (`riscv_mmio`)
Extend OpenOCD's existing `--enable-dmem` driver (`src/jtag/drivers/dmem.c`) or create a native `riscv_mmio` transport driver (`src/jtag/drivers/riscv_mmio.c`) to allow native RISC-V MMIO debug module access in OpenOCD:

```tcl
adapter driver riscv_mmio
riscv_mmio base 0x07090000
target create e907.cpu riscv -dap dmem
```
