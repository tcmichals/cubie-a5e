# AbstractX XuanTie E906 Target Specification & Architecture Matrix

This document is the **Single Source of Truth (SSOT)** for all architectural
contracts, memory layouts, hardware cache rules, and driver specifications
for the **Allwinner T527 / A523 XuanTie E906 RISC-V Co-Processor Target**
(`targets/allwinner_e906/`) in AbstractX.

Each requirement carries a unique **Design ID (`[SPEC-E906-*]`)** that is
directly traceable by implementation source files via `// @impl [SPEC-E906-*]`.

---

## 1. System Architecture Specifications (`SPEC-E906-ARCH`)

### `[SPEC-E906-ARCH-01]` Processor Core Architecture
* **Target Core**: T-Head / XuanTie E906 (32-bit RISC-V: RV32IMAFDC).
* **Operating Frequency**: Up to 200 MHz driven by the MCU CCU PLL.
* **Privilege Modes**: Machine (M) and User (U) modes only.
* **MMU Absence**: The E906 does NOT implement Supervisor mode virtual memory
  (no `satp` register, no Sv32/Sv39 page tables).
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`

### `[SPEC-E906-ARCH-02]` Fixed Hardware System Memory Map (SYSMAP)
* **Hardware PMA Invariant**: Physical Memory Attributes (PMAs) are hardwired
  into ASIC logic gates at silicon synthesis time (from RTL `sysmap.h`).
* **Attributes**:
  * Peripheral MMIO (`< 0x3FFC0000`): Hardwired to **Strongly Ordered**
    (Non-cacheable). Automatically bypasses L1 cache in silicon.
  * Internal SRAM (`0x3FFC0000`–`0x40080000`):
    Hardwired to **Normal / Cacheable**.
  * External DDR DRAM (`0x40000000`–`0xC0000000`):
    Hardwired to **Normal / Cacheable**.
* **Immutability**: Memory attributes cannot be modified via instructions or
  runtime CSRs on this processor.
* **Implementation Target**: `targets/allwinner_e906/include/memory_map.h`

---

## 2. Memory Architecture & Determinism (`SPEC-E906-MEM`)

### `[SPEC-E906-MEM-01]` Pure Dedicated MCU SRAM Architecture (Zero TCM)
* **Silicon Reality**: There is **NO ITCM and NO DTCM** on the T527 E906.
  Addresses `0x00000000` (ITCM) and `0x00080000` (DTCM) do not exist in silicon.
* **Execution Window**: E906 executes out of dedicated on-chip **SRAM A3**:
  * **SRAM Space 0**: `0x3FFC0000`–`0x40000000` (256 KB Dedicated Slice,
    Host PA `0x07130000`). Reset vector and default boot window.
  * **SRAM Space 1**: `0x40040000`–`0x40080000` (256 KB Switchable Slice,
    enabled via `REMAP_CTRL_REG[1] = 1`).
* **Implementation Target**: `targets/allwinner_e906/bsp/e906_sram.ld`

### `[SPEC-E906-MEM-02]` Control vs. Data Plane Memory Split
* **Control Plane in SRAM**: SPSC ring pointers, descriptor states, and
  doorbell status flags MUST reside in on-chip SRAM
  (`0x3FFC0000` / `0x07130000`) for single-cycle synchronization.
  for zero-wait-state single-cycle synchronization.
* **Data Plane in DDR**: Large streaming payload buffers (4 KB to multi-MB)
  reside in external DDR DRAM carveouts (`0x48000000`+).
* **Target**: `targets/allwinner_e906/include/dram_spsc_protocol.h`

---

## 3. Hardware Cache Pipeline (`SPEC-E906-CACHE`)

### `[SPEC-E906-CACHE-01]` Vendor Instruction Extension Unlock
* **Requirement**: T-Head custom instruction extensions MUST be unlocked at boot
  to prevent Illegal Instruction exceptions (`mcause = 2`) during cache ops.
* **Mechanism**: In `startup.S`, set bit 22 (`THEADISAEE = 1`) and bit 15
  (`MM = 1`) in `CSR_MXSTATUS` (0x7C0).
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`

### `[SPEC-E906-CACHE-02]` Full L1 Cache Activation
* **Requirement**: AbstractX operates with full L1 Instruction and Data caches
  enabled for maximum computational throughput.
* **Mechanism**: Invalidate cache lines via `CSR_MCOR` (0x7C2 bit 6 and 17),
  then configure `CSR_MHCR` (0x7C1) with value `0x7177` (`IE=1, DE=1, WB=1,
  WA=1, RS=1, BPE=1, BTB=1`).
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`

### `[SPEC-E906-CACHE-03]` Verified Machine-Code Cache Maintenance
* **Requirement**: Software cache maintenance must use verified XuanTie line
  operations (32-byte cache lines) matching the Allwinner Tina SDK.
* **Operations**:
  * Clean (Write-Back): `.word 0x0297800b` (`dcache.cpa a5`)
  * Invalidate: `.word 0x02a7800b` (`dcache.iva a5`)
  * Clean & Invalidate: `.word 0x02b7800b` (`dcache.civa a5`)
* **Implementation Target**: `targets/allwinner_e906/src/pmp.cpp`

---

## 4. Polymorphic Inter-Processor Communication (`SPEC-E906-IPC`)

### `[SPEC-E906-IPC-01]` Pure Virtual `IRpmsg` Contract
* **Requirement**: Application tasks, coroutines, and telemetry modules must
  interface with IPC exclusively through the pure virtual `IRpmsg` interface:
  * `init(const struct rpmsg_resource_table *rsc)`
  * `is_driver_ready()`
  * `register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data)`
  * `announce_service(const char *name, uint32_t addr)`
  * `poll()`
  * `reply(const RpmsgMessage &msg, const void *payload, uint16_t len)`
  * `is_rx_pending()`
* **Implementation Target**: `targets/allwinner_e906/include/hal/rpmsg.hpp`

### `[SPEC-E906-IPC-02]` Standard Linux VirtIO RPMsg (`class Rpmsg`)
* **Role**: Driver for Linux `virtio_rpmsg_bus` (`/dev/rpmsg0`).
* **Target Memory**: DDR DRAM carveouts (`0x48000000`+).
* **Cache Coherency Policy**:
  * Invalidate `avail` ring and incoming packet buffers via `dcache.iva` on RX.
  * Clean (write-back) response buffers and `used` ring via
    `dcache.cpa` on TX before kicking MSGBOX Channel 0.
    on TX before kicking MSGBOX Channel 0.
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

### `[SPEC-E906-IPC-03]` Lite-Metal Direct Driver (`class RpmsgLiteMetal`)
* **Role**: Direct driver for shared SRAM (`0x3FFC0000`) or UIO.
* **Target Memory**: On-chip SRAM Space 0 / Space 1.
* **Cache Policy**: Zero cache operations (NOP). Directly accesses
  memory at bus speed with zero cache maintenance overhead.
  at bus speed with zero cache maintenance overhead.
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

---

## 5. Event-Driven Coroutines & ISR Binding (`SPEC-E906-CORO`)

### `[SPEC-E906-CORO-01]` Zero-Polling Asynchronous RPMsg Awaiter
* **Requirement**: Tasks waiting for incoming RPMsg packets MUST NOT busy-wait
  in polling loops.
* **Mechanism**: Tasks suspend via `co_await rpmsg.async_receive()`:
  * If packet is already pending: `await_ready()` returns true (0 ns latency).
  * If queue is empty: Stores coroutine handle in `s_rpmsg_coroutine_handle`,
    enables MSGBOX Channel 1 RX interrupt, and suspends in ~18 ns.
* **Implementation Target**: `targets/allwinner_e906/include/hal/rpmsg.hpp`

### `[SPEC-E906-CORO-02]` Hardware MSGBOX ISR Direct Waking
* **Requirement**: Linux doorbell kicks on MSGBOX Channel 1 must wake the
  suspended coroutine in under 25 ns without thread context switches.
* **Mechanism**: PLIC IRQ 48 executes `fc_msgbox_doorbell_isr()`:
  1. Clears hardware MSGBOX interrupt status.
  2. Resumes `s_rpmsg_coroutine_handle` directly in top-half ISR.
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

---

## 6. Threshold-Balanced Peripheral HAL (`SPEC-E906-HAL`)

### `[SPEC-E906-HAL-01]` Direct FIFO vs. DMA Threshold Balancing
* **Requirement**: Peripheral drivers must balance latency vs. bandwidth:
  * Payloads below threshold use direct CPU FIFO writes (zero DMA overhead).
  * Payloads above threshold arm the dedicated co-processor DMA controller
    (Channels 8..15) and suspend calling coroutines.
* **Thresholds**:
  * `hal::Spi`: `<= 4 bytes` (Fast FIFO) vs. `> 4 bytes` (DMA Awaiter)
  * `hal::I2c`: `<= 4 bytes` (Fast FIFO) vs. `> 4 bytes` (DMA Awaiter)
  * `hal::Uart`: `<= 32 bytes` (Fast FIFO) vs. `> 32 bytes` (DMA Awaiter)
* **Implementation Target**: `targets/allwinner_e906/src/{spi,i2c,uart,dma}.cpp`

---

## 7. Traceability Matrix

| Design Requirement | Description | Primary Implementation File |
| :--- | :--- | :--- |
| `[SPEC-E906-ARCH-01]` | E906 RV32IMAFDC Core (No MMU) | `bsp/startup.S` |
| `[SPEC-E906-ARCH-02]` | Hardwired ASIC SYSMAP PMAs | `include/memory_map.h` |
| `[SPEC-E906-MEM-01]`  | Dedicated MCU SRAM (Zero TCM) | `bsp/e906_sram.ld` |
| `[SPEC-E906-MEM-02]`  | SPSC in SRAM / DDR Buffers | `dram_spsc_protocol.h` |
| `[SPEC-E906-CACHE-01]`| THEADISAEE Vendor Opcode Unlock | `bsp/startup.S` |
| `[SPEC-E906-CACHE-02]`| L1 I/D-Cache Full Enable (0x7177)| `bsp/startup.S` |
| `[SPEC-E906-CACHE-03]`| Tina SDK Machine Opcodes | `src/pmp.cpp` |
| `[SPEC-E906-IPC-01]`  | Pure Virtual `IRpmsg` | `include/hal/rpmsg.hpp` |
| `[SPEC-E906-IPC-02]`  | Linux VirtIO RPMsg with D-Cache | `src/rpmsg.cpp` |
| `[SPEC-E906-IPC-03]`  | Lite-Metal Direct SRAM Driver | `src/rpmsg.cpp` |
| `[SPEC-E906-CORO-01]` | Async `async_receive()` | `include/hal/rpmsg.hpp` |
| `[SPEC-E906-CORO-02]` | MSGBOX Doorbell ISR Waking | `src/rpmsg.cpp` |
| `[SPEC-E906-HAL-01]`  | FIFO vs. DMA Balancing | `src/{spi,uart,i2c}.cpp` |
