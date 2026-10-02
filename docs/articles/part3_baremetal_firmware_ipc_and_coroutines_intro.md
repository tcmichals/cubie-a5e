# Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Part 3): Inter-Processor Communication (IPC) Deep Dive

In **[Part 1](part1_heterogeneous_riscv_intro_architecture.md)** and **[Part 2](part2_building_remoteproc_and_hardware_proof.md)**, we established the heterogeneous architecture, built the Linux `remoteproc` driver for the **Allwinner T527 / A527** (`sun55i`), and proved hardware execution with the `riscv-firmware/apps` verification suite.

In this article (**Part 3**), we go deep on the most critical engineering question for any heterogeneous system:

**How do the XuanTie E906 RISC-V co-processor and the ARM64 Linux host talk to each other?**

This article covers the three distinct IPC paradigms implemented in this repository, grounded in real production source code:

1. **Paradigm 1 — Ultra-Low-Latency**: Lock-free Shared SRAM ring buffer + Hardware Mailbox Doorbell (`testPing`)
2. **Paradigm 2 — Standard Linux Integration**: VirtIO RPMsg over virtual queues (`testPingRpmsg`)
3. **Paradigm 3 — High-Bandwidth Hybrid**: SRAM control + DDR DRAM bulk payload pool (`testDRAMMsg`)

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

---

## 1. The IPC Hardware Layer: Allwinner Hardware Message Box

Before examining each paradigm, we need to understand the hardware interrupt primitive that all three paradigms share: the **Allwinner Hardware Message Box (MSGBOX)**.

```text
┌──────────────────────────────────────────────────────────────────┐
│            Allwinner T527 Hardware Message Box (0x03003000)      │
│                                                                  │
│  ARM64 Linux (Cortex-A55)          XuanTie E906 (RISC-V)         │
│  ─────────────────────────         ─────────────────────────     │
│  TX to RISC-V → ch0 FIFO           RX from ARM → ch1 FIFO       │
│  RX from RISC-V ← ch0 FIFO        TX to ARM   → ch0 FIFO       │
│                                                                  │
│  ARM GIC SPI IRQ 147               RISC-V PLIC IRQ 25           │
└──────────────────────────────────────────────────────────────────┘
```

The `hal::MsgBox` class ([`riscv-firmware/common/hal/msgbox.hpp`](../../riscv-firmware/common/hal/msgbox.hpp)) provides the complete RISC-V-side driver:

```cpp
namespace hal {

class MsgBox {
public:
    enum class Channel : uint8_t { Channel0 = 0, Channel1 = 1, Channel2 = 2, Channel3 = 3 };

    static void init() noexcept;

    // Non-blocking send: returns false if Tx FIFO is full
    static bool send(Channel ch, uint32_t data) noexcept;

    // Non-blocking receive: returns std::nullopt if Rx FIFO is empty
    static std::optional<uint32_t> receive(Channel ch) noexcept;

    [[nodiscard]] static bool is_rx_pending(Channel ch) noexcept;
    [[nodiscard]] static bool is_tx_ready(Channel ch) noexcept;

    static void enable_rx_irq(Channel ch, bool enable) noexcept;
    static void clear_irq_status(Channel ch) noexcept;
};

} // namespace hal
```

The MSGBOX is the **doorbell primitive**: it signals the remote core that something is ready in shared memory. The data itself is always transferred via shared memory — the MSGBOX carries only a notification token.

---

## 2. Paradigm 1: Ultra-Low-Latency Shared SRAM + Hardware Mailbox (`testPing`)

**Goal**: Sub-microsecond round-trip latency for high-frequency ping-pong between the RISC-V core and Linux — the foundation for sensor data acquisition loops, real-time telemetry, and control command delivery.

**Source code**: [`riscv-firmware/apps/testPing/`](../../riscv-firmware/apps/testPing/)

### 2.1 The Shared Memory Channel Protocol

The shared channel is defined in [`riscv-firmware/common/include/shm_ping_protocol.h`](../../riscv-firmware/common/include/shm_ping_protocol.h):

```c
#define SHM_PING_SRAM_ADDR  0x07131000UL  /* Dedicated MCU SRAM (offset past trace buffer) */
#define SHM_PING_SRAM_SIZE  0x1000UL      /* 4 KB channel window */

/* 64-byte ping/pong packet */
struct __attribute__((packed, aligned(4))) ShmPingPacket {
    uint32_t magic;          // SHM_PING_MAGIC (0x50494E47 "PING") or SHM_PONG_MAGIC
    uint32_t seq;            // Monotonic sequence counter
    uint64_t host_tx_ts_ns;  // Linux CLOCK_MONOTONIC timestamp (ns)
    uint64_t riscv_cycles;   // RISC-V mcycle hardware counter
    uint32_t payload_len;
    char     payload[40];
};

/* Bidirectional channel layout in SRAM */
struct __attribute__((packed, aligned(4))) ShmPingChannel {
    volatile uint32_t host_doorbell;   // Written by Linux: 1 = new ping ready
    volatile uint32_t riscv_doorbell;  // Written by RISC-V: 1 = new pong ready
    volatile uint32_t total_pings;
    volatile uint32_t total_pongs;
    ShmPingPacket     ping_pkt;        // Host -> RISC-V direction
    ShmPingPacket     pong_pkt;        // RISC-V -> Host direction
};
```

Key design decisions:
- **Identity-mapped**: `0x07131000` is accessible at the same physical address from both ARM64 and RISC-V without address translation.
- **Volatile flags** (`host_doorbell`, `riscv_doorbell`): Prevent compiler optimization from caching the doorbell state in a register.
- **Hardware timestamps**: `riscv_cycles` captures the `mcycle` CSR in the pong handler — enabling precise round-trip latency measurement from the Linux host.

### 2.2 RISC-V Firmware: The Pong Response Loop

```cpp
/* apps/testPing/main.cpp */
#define SHM_CHANNEL ((volatile ShmPingChannel *)SHM_PING_SRAM_ADDR)

while (1) {
    // Check for incoming ping via SRAM flag OR Hardware Mailbox IRQ from Linux
    bool ping_ready = (SHM_CHANNEL->host_doorbell == 1);
    if (hal::MsgBox::is_rx_pending(hal::MsgBox::Channel::Channel1)) {
        (void)hal::MsgBox::receive(hal::MsgBox::Channel::Channel1);
        ping_ready = true;
    }

    if (ping_ready) {
        uint64_t current_cycles = hal::Timer::get_ticks();

        // Echo sequence and timestamps into pong packet
        SHM_CHANNEL->pong_pkt.magic         = SHM_PONG_MAGIC;
        SHM_CHANNEL->pong_pkt.seq           = SHM_CHANNEL->ping_pkt.seq;
        SHM_CHANNEL->pong_pkt.host_tx_ts_ns = SHM_CHANNEL->ping_pkt.host_tx_ts_ns;
        SHM_CHANNEL->pong_pkt.riscv_cycles  = current_cycles;

        // Clear host doorbell, set RISC-V doorbell
        SHM_CHANNEL->host_doorbell  = 0;
        SHM_CHANNEL->riscv_doorbell = 1;

        // Trigger ARM GIC SPI 147 via Hardware Mailbox
        hal::MsgBox::send(hal::MsgBox::Channel::Channel0, 0x01);
        SHM_CHANNEL->total_pongs++;
    }
}
```

### 2.3 Linux Host: Two Companion Tool Modes

The `linux/` directory provides **two tools** for different use cases:

**`ping_shm` — Direct Shared Memory Polling** ([`testPing/linux/ping_shm.cpp`](../../riscv-firmware/apps/testPing/linux/ping_shm.cpp)):
- Maps the SRAM window via `/dev/mem`.
- Busy-polls `riscv_doorbell` — maximum throughput, consumes 100% of one CPU core.
- Use case: Benchmarking raw channel capacity and baseline latency floor.

**`ping_uio` — Event-Driven UIO Doorbell** ([`testPing/linux/ping_uio.cpp`](../../riscv-firmware/apps/testPing/linux/ping_uio.cpp)):
- Opens `/dev/uio0` (exposed by the `cubie-a5e-uio.dtbo` overlay).
- Blocks in `epoll_wait()` — **zero CPU burn** while waiting for the RISC-V doorbell.
- When RISC-V fires MSGBOX Channel 0 → ARM GIC SPI 147 → UIO interrupt → `epoll` wakes → host reads pong from SRAM.

```bash
# Event-driven: 50,000 round trips with zero idle CPU utilization
ping_uio -n 50000
# Results: 1.5 to 2.5 µs round-trip latency, ~0% Linux CPU usage

# Direct polling: measure raw latency floor
ping_shm -n 100000
```

### 2.4 The Lock-Free SPSC Queue Primitive

For sustained high-rate telemetry (not just one-shot ping-pong), [`hal::SpscQueue<T, Capacity>`](../../riscv-firmware/common/hal/spsc_queue.hpp) provides a correct, wait-free ring queue using C++ acquire-release memory ordering:

```cpp
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    // Producer enqueue (RISC-V side)
    bool push(const T& item) noexcept {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if ((head - tail) >= Capacity) return false; // Full
        buffer_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer dequeue (ARM64 Linux side)
    bool pop(T& item) noexcept {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false; // Empty
        item = buffer_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<uint32_t> head_;  // Cache-line isolated
    alignas(64) std::atomic<uint32_t> tail_;  // to prevent false sharing
    T buffer_[Capacity];
};
```

The `alignas(64)` placement on separate 64-byte cache lines is critical — without this, ARM64 and RISC-V would invalidate each other's caches on every read, collapsing throughput to near-DRAM latency.

---

## 3. Paradigm 2: Standard Linux VirtIO RPMsg (`testPingRpmsg`)

**Goal**: Full integration with the mainline Linux kernel messaging ecosystem — enabling standard `open()`, `read()`, `write()` file descriptor semantics on `/dev/rpmsg0`.

**Source code**: [`riscv-firmware/apps/testPingRpmsg/`](../../riscv-firmware/apps/testPingRpmsg/)

### 3.1 How VirtIO RPMsg Works

RPMsg is built on top of the VirtIO transport baked into `remoteproc`. The RISC-V firmware's `.resource_table` declares two VirtIO `vring` descriptor rings in DDR memory. The Linux `virtio_rpmsg_bus` driver discovers and maps these rings, then automatically creates `/dev/rpmsg0`.

```text
RISC-V Firmware                         Linux Kernel
─────────────────                       ────────────────────────────────
resource_table:                         virtio_rpmsg_bus driver:
  VDEV0 (VirtIO device, type=7)  →        Creates /dev/rpmsg0
  vring0: TX descriptor ring     →        ARM64 reads RISC-V packets
  vring1: RX descriptor ring     ←        ARM64 writes packets to RISC-V
```

### 3.2 RISC-V Firmware: RPMsg Echo Loop

```cpp
/* apps/testPingRpmsg/main.cpp */
hal::Rpmsg::init();

// Announce the endpoint — triggers /dev/rpmsg0 creation on Linux
hal::Rpmsg::announce_channel("rpmsg-ping-channel");

uint8_t buf[256];
while (1) {
    int len = hal::Rpmsg::recv(buf, sizeof(buf));
    if (len > 0) {
        hal::Rpmsg::send(buf, len);  // Echo back
    }
}
```

### 3.3 Linux Host Tool

```bash
# Run 5,000 round-trip RPMsg echo tests
ping_rpmsg -n 5000
# Results: ~50-200 µs round-trip latency
```

The higher latency compared to Paradigm 1 comes from VirtIO descriptor ring protocol overhead, kernel scheduling jitter, and the Linux `virtio_rpmsg_bus` consumer waking on interrupt.

### 3.4 When to Use RPMsg vs. Shared SRAM

| Requirement | Paradigm 1 (SPSC + Mailbox) | Paradigm 2 (RPMsg) |
| :--- | :--- | :--- |
| Latency requirement | **< 5 µs** | 50–200 µs acceptable |
| Linux CPU impact | **Near-zero** (epoll/UIO) | Standard kernel scheduling |
| Protocol portability | Custom structs | Standard Linux `remoteproc` |
| Payload size | Small fixed frames | Variable, up to 512 B |
| Tooling | Custom host tools | Works with existing `/dev/rpmsg` utils |

---

## 4. Paradigm 3: Hybrid SRAM Control + DDR Bulk Payload Pool (`testDRAMMsg`)

**Goal**: High-bandwidth streaming (camera frames, large telemetry batches, flight logs) that exceeds on-chip SRAM capacity, while retaining deterministic control-plane latency.

**Source code**: [`riscv-firmware/apps/testDRAMMsg/`](../../riscv-firmware/apps/testDRAMMsg/)

### 4.1 Architecture: Separated Control and Data Planes

The key design principle is **keeping the fast control plane in SRAM while pushing only bulk payload into DDR**:

```text
FAST CONTROL PLANE — Dedicated MCU SRAM (0x07130000, 4 KB)
  DramSpscControlBlock:
    • host_head / riscv_tail ring pointers (volatile uint32_t)
    • riscv_head / host_tail ring pointers (volatile uint32_t)
    • host_doorbell / riscv_doorbell     (volatile uint32_t)
    • DramSpscDesc tx_ring[16] / rx_ring[16]
        └── dram_buf_offset → points into DDR pool
            └── payload_len, seq, flags, timestamps

BULK DATA PLANE — DDR DRAM Carveout (0x48100000, 1 MB)
    16 slots × 4 KB = 64 KB max in-flight per direction
    Uncached: D-Cache disabled (mhcr.DE = 0) / no-map in ARM64 DT
```

#### Why This Split is Essential for Efficiency
Because the co-processor runs with D-Cache disabled for deterministic RemoteProc
coherency, **accessing DDR DRAM incurs substantial bus latency** (external AXI
interconnect arbitration and memory controller cycles).

If the SPSC queue control structures (head/tail ring pointers, status flags,
timestamps, and descriptor states) were located in DRAM:
* Every queue check, head/tail pointer advance, and doorbell poll would stall
  the RISC-V execution pipeline waiting on external memory controller bus turns.
* Small 4-byte synchronization writes would suffer the full penalty of DRAM
  latency, collapsing throughput.

By separating the architecture:
1. **SPSC in SRAM**: High-frequency ring updates, descriptor state transitions,
   and pointer increments execute in **zero-wait-state on-chip SRAM** (single-
   cycle access, ~5 ns at 200 MHz) with zero bus arbitration contention.
2. **Buffers in DRAM**: External DDR is accessed **only** when reading or
   writing bulk payload buffers (or when handled via DMA).

This gives the system the best of both worlds: ultra-fast, jitter-free queue
synchronization combined with megabyte-scale payload streaming capacity.

### 4.2 Protocol Definition

From [`riscv-firmware/common/include/dram_spsc_protocol.h`](../../riscv-firmware/common/include/dram_spsc_protocol.h):

```c
#define DRAM_SPSC_SRAM_ADDR    0x07130000UL   /* Control block base */
#define DRAM_SPSC_DRAM_ADDR    0x48100000UL   /* DDR payload pool */
#define DRAM_SPSC_DRAM_SIZE    0x00100000UL   /* 1 MB pool */
#define DRAM_SPSC_RING_ENTRIES 16             /* 16 descriptor slots */
#define DRAM_SPSC_MAX_BUF_LEN  4096UL         /* 4 KB max per slot */

/* SRAM-resident descriptor: points into DDR pool */
struct DramSpscDesc {
    uint32_t dram_buf_offset;  // Byte offset into 0x48100000 pool
    uint32_t payload_len;      // Valid byte count
    uint32_t seq;              // Monotonic sequence number
    uint32_t flags;            // 0=Empty, 1=Ready, 2=Ack
    uint64_t host_tx_ts_ns;    // Linux TX timestamp
    uint64_t riscv_cycles;     // RISC-V mcycle timestamp
};

/* Bidirectional control block (entirely in SRAM) */
struct DramSpscControlBlock {
    volatile uint32_t magic;
    volatile uint32_t ring_size;
    volatile uint32_t dram_pool_phys;
    volatile uint32_t dram_pool_size;
    volatile uint32_t host_head;
    volatile uint32_t riscv_tail;
    volatile uint32_t host_doorbell;
    volatile uint32_t riscv_head;
    volatile uint32_t host_tail;
    volatile uint32_t riscv_doorbell;
    volatile uint32_t total_pings_recv;
    volatile uint32_t total_pongs_sent;
    volatile uint64_t total_bytes_transferred;
    DramSpscDesc tx_ring[DRAM_SPSC_RING_ENTRIES];
    DramSpscDesc rx_ring[DRAM_SPSC_RING_ENTRIES];
};
```

### 4.3 Cache Coherency: PMP Permissions vs. Hardwired SYSMAP vs. mhcr.DE

When designing shared-memory IPC between Linux and an auxiliary RISC-V core,
memory coherency is the single biggest engineering pitfall. The XuanTie E906
implements a specific three-tier memory architecture that governs coherency:

#### 1. Standard RISC-V PMP: Access Control Only
Standard RISC-V Physical Memory Protection (PMP) strictly manages access control
and security isolation via Machine-mode CSRs (`pmpcfg0`–`pmpcfg3` and
`pmpaddr0`–`pmpaddr15`):
* **Configured Attributes**: Read (R), Write (W), and Execute (X) permissions.
* **Hardware Limitation**: PMP contains **zero** bits for cacheability or
  bufferability. It does not know or control whether data passes through the
  L1 Data Cache or goes straight to the system bus.

```cpp
// ⚠️ PMP configures Read/Write access permissions ONLY:
hal::Pmp::set_napot_entry(1, DRAM_SPSC_DRAM_ADDR, DRAM_SPSC_DRAM_SIZE,
                          hal::PmpFlags::Read | hal::PmpFlags::Write);
```

#### 2. T-Head SYSMAP: Hardwired Physical Memory Attributes (PMAs)
Cache properties (Cacheable, Bufferable, Strongly Ordered) are governed by the
T-Head **SYSMAP** (System Memory Map) unit, NOT by PMP:
* In application-class XuanTie cores with an MMU and the MAEE (Memory Attribute
  Enhanced Extension), page table entries can configure cache attributes at
  runtime.
* **On Microcontroller Cores (No MMU / No MAEE)**: On the XuanTie E906 core in
  Allwinner T527/A523 silicon, MAEE is omitted. Consequently, the 8 SYSMAP
  region descriptors are **completely hardwired at ASIC synthesis time** and
  cannot be modified by software at runtime.
* **Hardware-Enforced Routing**: Peripheral MMIO registers are hardwired as
  Strongly Ordered (Non-cacheable), while external DDR DRAM and internal SRAM
  are hardwired as **Normal Cacheable Memory**. Runtime non-cacheable RAM
  carveouts are physically impossible in silicon.

#### 3. Core Cache Control: mhcr.DE = 0 vs. mhcr.DE = 1
Because SYSMAP forces all DDR RAM transactions to be cacheable, L1 Data Cache
behavior is governed entirely by the Data Cache Enable bit (`DE`) in the
Machine Hardware Control Register (`mhcr.DE`, CSR `0x7C1` bit 1):

* **When `mhcr.DE = 0` (Our Reference Testbed `riscv-firmware`)**:
  The L1 Data Cache is entirely disabled. Every memory operation bypasses cache
  lines completely, achieving 100% deterministic, uncached coherency with Linux
  DMA at the cost of peak computational throughput. This is the optimal,
  foolproof choice for validating Linux RemoteProc bring-up.

* **When `mhcr.DE = 1` (The AbstractX Production Flight Stack)**:
  All RAM reads and writes hit the L1 cache. Software cache maintenance via
  custom T-Head instructions (`dcache.cpa` to clean, `dcache.iva` to
  invalidate) is strictly required to synchronize shared memory buffers before
  and after DMA transfers.

#### 4. The XuanTie Instruction Trap (`CSR_MXSTATUS`)
If `mhcr.DE = 1` is used, executing vendor cache opcodes (`dcache.cpa` or
`dcache.iva`) without unlocking the CPU triggers an immediate **Illegal
Instruction exception (`mcause = 2`)**!

On XuanTie cores, vendor instructions are disabled at reset. As proven in the
Allwinner Tina SDK (`rtos/arch/risc-v/e90x/cache.c`), software must first set
**`CSR_MXSTATUS` (0x7C0) bit 22 (`THEADISAEE = 1`)** before executing cache
management instructions.

In **[Part 4](part4_deep_dive_baremetal_cpp_coroutines.md)**, we walk through
how **AbstractX** implements this complete high-performance cache pipeline.

### 4.4 Linux Host Tool

```bash
# Transfer 4 KB payloads, 1,000 iterations
ping_dram -n 1000 -s 4096
# Results: > 100 MB/s throughput, ~10-30 µs per 4 KB frame
```

---

## 5. IPC Paradigm Comparison

| Dimension | SRAM IPC | RPMsg | Hybrid DRAM |
| :--- | :---: | :---: | :---: |
| **Latency** | **1.5–2.5 µs** | 50–200 µs | 10–30 µs |
| **Throughput** | Small frames | < 512 B/msg | **> 100 MB/s** |
| **Max Payload** | 4 KB (SRAM) | 512 B | **4 KB × 16** |
| **Host CPU** | **Near-zero** | Sched context | DMA-offloaded |
| **Complexity** | Low | Standard | Medium ring |
| **Coherency** | N/A (SRAM) | N/A (SRAM) | Uncached (`DE=0`)|
| **Best For** | Control loops| Linux apps | Bulk data |
| **Test App** | `testPing` | `testPingRpmsg`| `testDRAMMsg` |

---

## 6. Summary

In this article we covered the three IPC paradigms deployed on the Allwinner T527 / A527 heterogeneous platform:

1. **Lock-Free Shared SRAM + Hardware Mailbox** (`testPing`): Sub-microsecond latency using `ShmPingChannel` at `0x07131000`, `hal::MsgBox` doorbells, and event-driven UIO epoll on the Linux host. The `hal::SpscQueue<T,N>` template enables sustained high-rate telemetry streams.
2. **VirtIO RPMsg** (`testPingRpmsg`): Standard Linux `/dev/rpmsg0` integration via `hal::Rpmsg`, enabling conventional file descriptor semantics and existing `remoteproc` ecosystem tooling.
3. **Hybrid SRAM/DDR** (`testDRAMMsg`): High-bandwidth bulk payload streaming with `DramSpscControlBlock` descriptors in fast SRAM pointing into a 1 MB DDR carveout — coherency guaranteed via uncached bus access (`mhcr.DE = 0` on RISC-V, `no-map` reserved memory on ARM64).

All three paradigms share the same underlying hardware: the **Allwinner Hardware Message Box (MSGBOX)** at `0x03003000` for doorbell interrupts, and the HAL drivers in [`riscv-firmware/common/hal/`](../../riscv-firmware/common/hal/).

---

### Series Navigation
* **[Part 1: Architecture and Memory-Mapped Debugging](part1_heterogeneous_riscv_intro_architecture.md)**
* **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](part2_building_remoteproc_and_hardware_proof.md)**
* **Part 3: Inter-Processor Communication (IPC) Deep Dive** *(You are here)*
* **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E906](part4_deep_dive_baremetal_cpp_coroutines.md)**

---

#EmbeddedSystems #RISCV #Linux #RemoteProc #IPC #Allwinner #BareMetal #RealTime #SharedMemory
