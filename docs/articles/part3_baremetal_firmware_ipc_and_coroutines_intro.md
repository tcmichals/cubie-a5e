<!--
================================================================================
HACKSTER.IO PROJECT SETTINGS (Copy & Paste into Hackster Form)
================================================================================
Project Title: Ultra-Low-Latency IPC: Connecting Linux and RISC-V on Allwinner T527 (Part 3)
Elevator Pitch: Achieving 1.5 µs round-trip latency and >100 MB/s streaming between ARM64 Linux and the auxiliary XuanTie E906 RISC-V co-processor on the Radxa Cubie A5E.
Hardware: Radxa Cubie A5E (Allwinner T527 / A527)
Software: Linux 6.x/7.x, RISC-V GCC, Allwinner Tina SDK
Tags: #RISCV #Linux #Embedded #RemoteProc #IPC #RealTime #Allwinner
================================================================================
-->

# Ultra-Low-Latency IPC: Connecting Linux and RISC-V on Allwinner T527 (Part 3)

*Part 3 of Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Radxa Cubie A5E)*

In **[Part 1](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063)** and **[Part 2](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8)**, we established the heterogeneous architecture, built the Linux `remoteproc` driver for the **Allwinner T527 / A527** (`sun55i`), and proved hardware execution with the `riscv-firmware/apps` verification suite.

Now we tackle the single most critical engineering question for any heterogeneous SoC:

> **How do the XuanTie E906 RISC-V co-processor and the ARM64 Linux host talk to each other without burning CPU or collapsing real-time determinism?**

Instead of burying the results at the end, here is the architectural reality up front: there is no single "best" IPC mechanism. Depending on whether your system needs hard real-time latency, Linux ecosystem compatibility, or multi-megabyte throughput, we developed and validated **three distinct IPC paradigms** running on actual silicon.

---

## 1. The Three IPC Paradigms at a Glance

| Dimension | Paradigm 1: Shared SRAM + Mailbox | Paradigm 2: VirtIO RPMsg | Paradigm 3: Hybrid SRAM / DDR |
| :--- | :---: | :---: | :---: |
| **Round-Trip Latency** | **1.5 – 2.5 µs** | 50 – 200 µs | 10 – 30 µs |
| **Max Throughput** | High rate, small frames | < 512 B / msg | **> 100 MB/s** |
| **Max Payload** | 4 KB (Dedicated SRAM) | 512 B standard buffer | **4 KB × 16 (1 MB Carveout)** |
| **Linux Host CPU** | **~0% (UIO epoll)** | Sched context switch | DMA-offloaded |
| **Memory Region** | Dedicated MCU SRAM (`0x07131000`) | DDR DRAM carveout | Fast SRAM descriptors + DDR payload |
| **Coherency Burden** | Zero (SRAM bypasses cache) | Cache invalidation required | Uncached bus access (`mhcr.DE = 0`) |
| **Best Used For** | Fast sensor/motor control loops | Standard Linux CLI / utilities | Telemetry logs, audio, camera frames |
| **Reference Code** | [`testPing`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testPing) | [`testPingRpmsg`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testPingRpmsg) | [`testDRAMMsg`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testDRAMMsg) |

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

---

## 2. The Shared Hardware Layer: Allwinner Message Box (MSGBOX)

All three paradigms share a common hardware primitive for inter-core signaling: the **Allwinner Hardware Message Box (MSGBOX)** located at physical address `0x03003000`.

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

The MSGBOX is purely a **doorbell mechanism**. Passing bulk data through hardware FIFOs causes FIFO overflows and stalls the bus. Instead, the MSGBOX carries a simple 32-bit token signaling: *"There is new work waiting for you in shared memory."*

The RISC-V hardware driver ([`riscv-firmware/common/hal/msgbox.hpp`](https://github.com/tcmichals/cubie-a5e/blob/main/firmware/common/hal/msgbox.hpp)) provides a non-blocking API:

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

---

## 3. Paradigm 1: Ultra-Low-Latency Shared SRAM + Mailbox (`testPing`)

**Goal**: Achieve low-microsecond round-trip latency (1.5 – 2.5 µs) between ARM64 and RISC-V with **0% Linux host CPU burn**.

### 3.1 Memory Layout & Protocol
The channel is mapped directly into Dedicated MCU SRAM at `0x07131000` (past the firmware trace buffer). Because this SRAM window is identity-mapped on both cores, no address translation is needed:

```c
#define SHM_PING_SRAM_ADDR  0x07131000UL  /* Dedicated MCU SRAM */
#define SHM_PING_SRAM_SIZE  0x1000UL      /* 4 KB channel window */

/* 64-byte ping/pong packet aligned to cache line */
struct __attribute__((packed, aligned(4))) ShmPingPacket {
    uint32_t magic;          // "PING" (0x50494E47) or "PONG"
    uint32_t seq;            // Monotonic sequence counter
    uint64_t host_tx_ts_ns;  // Linux CLOCK_MONOTONIC timestamp (ns)
    uint64_t riscv_cycles;   // RISC-V mcycle hardware counter
    uint32_t payload_len;
    char     payload[40];
};

struct __attribute__((packed, aligned(4))) ShmPingChannel {
    volatile uint32_t host_doorbell;   // 1 = Ping ready (written by Linux)
    volatile uint32_t riscv_doorbell;  // 1 = Pong ready (written by RISC-V)
    volatile uint32_t total_pings;
    volatile uint32_t total_pongs;
    ShmPingPacket     ping_pkt;        // Linux -> RISC-V
    ShmPingPacket     pong_pkt;        // RISC-V -> Linux
};
```

### 3.2 RISC-V Firmware: The Pong Handler
On the RISC-V core, incoming requests can be serviced via polling or triggered via PLIC IRQ 25:

```cpp
/* apps/testPing/main.cpp */
#define SHM_CHANNEL ((volatile ShmPingChannel *)SHM_PING_SRAM_ADDR)

while (1) {
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

        // Clear host doorbell, assert RISC-V doorbell
        SHM_CHANNEL->host_doorbell  = 0;
        SHM_CHANNEL->riscv_doorbell = 1;

        // Fire ARM GIC SPI 147 via Hardware Mailbox Channel 0
        hal::MsgBox::send(hal::MsgBox::Channel::Channel0, 0x01);
        SHM_CHANNEL->total_pongs++;
    }
}
```

### 3.3 Linux Host: Why Event-Driven UIO Crushes Busy-Polling
We built two host test tools to highlight an essential systems design lesson:

1. **`ping_shm` (Busy Polling via `/dev/mem`)**:
   Polls `riscv_doorbell` in a tight loop. While it achieves the absolute latency floor (~1.2 µs), it **burns 100% of an entire Cortex-A55 core**, creating thermal throttling and starving other Linux tasks.
2. **`ping_uio` (Event-Driven via UIO + `epoll`)**:
   Uses the `cubie-a5e-uio.dtbo` device tree overlay to wire MSGBOX Channel 0 directly to `/dev/uio0`. The userspace process sleeps in `epoll_wait()`. When RISC-V writes the doorbell token, hardware raises GIC SPI 147, Linux wakes `epoll`, and userspace reads the pong packet.

```bash
# Event-driven: 50,000 round trips with 0% idle CPU burn
ping_uio -n 50000
# Measured result: 1.5 µs to 2.5 µs round-trip latency at 0.1% CPU load!
```

### 3.4 Lock-Free SPSC Ring Queue (Preventing False Sharing)
For continuous telemetry streams, [`hal::SpscQueue<T, Capacity>`](https://github.com/tcmichals/cubie-a5e/blob/main/firmware/common/hal/spsc_queue.hpp) provides a lock-free ring buffer. 

> ⚠️ **The 64-Byte Alignment Rule**: Notice the `alignas(64)` on `head_` and `tail_`. On an SMP SoC where ARM Cortex-A55 has 64-byte L1 cache lines, placing `head_` and `tail_` on the same cache line causes **false sharing cache thrashing**. Aligning each pointer to separate cache lines prevents the cores from invalidating each other:

```cpp
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    bool push(const T& item) noexcept {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if ((head - tail) >= Capacity) return false;
        buffer_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;
        item = buffer_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<uint32_t> head_;  // Isolated cache line
    alignas(64) std::atomic<uint32_t> tail_;  // Isolated cache line
    T buffer_[Capacity];
};
```

---

## 4. Paradigm 2: Standard Linux VirtIO RPMsg (`testPingRpmsg`)

**Goal**: Seamless compatibility with standard Linux kernel IPC tools and scripts via `/dev/rpmsg0`.

### 4.1 How VirtIO RPMsg Operates
Unlike the bare-metal SRAM channel, RPMsg relies on the Linux `virtio_rpmsg_bus` driver. The RISC-V firmware declares a VirtIO device in its `.resource_table`:

```text
RISC-V Firmware                          Linux Kernel (remoteproc)
─────────────────                        ───────────────────────────────
.resource_table:                         virtio_rpmsg_bus:
  VDEV0 (VirtIO device, type=7)   ──►      Discovers descriptor rings
  vring0: TX ring (DDR memory)    ──►      Creates /dev/rpmsg0 character device
  vring1: RX ring (DDR memory)    ◄──      Routes standard open/read/write
```

### 4.2 Firmware Implementation
The firmware initializes the transport, advertises the service endpoint, and echoes packets:

```cpp
/* apps/testPingRpmsg/main.cpp */
hal::Rpmsg::init();
hal::Rpmsg::announce_channel("rpmsg-ping-channel");

uint8_t buf[256];
while (1) {
    int len = hal::Rpmsg::recv(buf, sizeof(buf));
    if (len > 0) {
        hal::Rpmsg::send(buf, len); // Echo back to Linux
    }
}
```

```bash
# Run 5,000 round-trip RPMsg packets
ping_rpmsg -n 5000
# Measured result: ~50 to 200 µs latency
```

* **The Trade-Off**: Round-trip latency increases from 2 µs to ~100 µs due to Linux kernel thread scheduling, ring buffer parsing, and context switches. However, you gain seamless compatibility with any standard Linux IPC program without custom UIO kernel modules.

---

## 5. Paradigm 3: Hybrid SRAM Control + DDR Bulk Payload Pool (`testDRAMMsg`)

**Goal**: Stream massive data volumes (high-rate flight logs, raw camera frames, multi-sensor bursts) exceeding SRAM capacity while maintaining zero-jitter control responsiveness.

### 5.1 Architecture: Separating Control and Data Planes

```text
┌────────────────────────────────────────────────────────────────────────┐
│ FAST CONTROL PLANE — Dedicated MCU SRAM (0x07130000, 4 KB)             │
│ • Single-cycle zero-wait-state access (single cycle at 200 MHz, ~5 ns) │
│ • volatile uint32_t host_head, riscv_tail ring indices                 │
│ • DramSpscDesc tx_ring[16], rx_ring[16] descriptors                    │
│   └── dram_buf_offset ──┐                                             │
└─────────────────────────┼──────────────────────────────────────────────┘
                          ▼ Points into 1 MB payload window
┌────────────────────────────────────────────────────────────────────────┐
│ BULK DATA PLANE — External DDR DRAM Pool (0x48100000, 1 MB)            │
│ • 16 descriptor slots × 4096 bytes per slot                            │
│ • Uncached DMA payload streaming (> 100 MB/s sustained)                │
└────────────────────────────────────────────────────────────────────────┘
```

#### Why This Split is Essential
If queue pointers and descriptor headers were kept in external DDR DRAM, every queue poll and descriptor check would stall the RISC-V pipeline waiting on external DDR memory controller turns. 

By placing the **control block in SRAM** and **payloads in DDR**, synchronization completes in single-cycle SRAM bus turns, and external DDR DRAM is touched **only** during payload copy.

### 5.2 The Silicon Reality: Cache Coherency & The XuanTie Trap

When communicating over shared DDR DRAM, memory coherency is where most embedded projects hit a brick wall. The XuanTie E906 presents several unique silicon constraints:

#### 1. Standard RISC-V PMP Has No Cacheability Bits
Standard RISC-V Physical Memory Protection (PMP) CSRs (`pmpcfg`, `pmpaddr`) manage **Read, Write, and Execute** security permissions only. They cannot designate a memory region as cacheable or non-cacheable.

#### 2. The T-Head SYSMAP is Hardwired in ASIC Gates
On the E906 co-processor in the Allwinner T527, the T-Head SYSMAP (System Memory Map) unit is **permanently hardwired in silicon logic**. There are no runtime programmable registers to mark a DDR DRAM carveout as non-cacheable. All DRAM addresses (`0x40000000`+) are hardwired as **Normal Cacheable**.

#### 3. The `mhcr.DE = 0` Solution for Bring-Up
Because DDR cannot be selectively marked uncached, `riscv-firmware` runs with the L1 Data Cache globally disabled (`mhcr.DE = 0`, CSR `0x7C1` bit 1). This ensures 100% deterministic coherency across Linux DMA and RemoteProc without complex cache flush logic.

#### 4. The XuanTie Instruction Trap (`CSR_MXSTATUS`)
If you decide to enable the Data Cache (`mhcr.DE = 1`) for peak CPU performance, you must invalidate and clean cache lines using T-Head vendor opcodes (`dcache.cpa` and `dcache.iva`). 

> 💥 **The Landmine**: If you execute `dcache.cpa` without unlocking the core, the processor throws an immediate **Illegal Instruction exception (`mcause = 2`)**! On XuanTie cores, vendor instructions are disabled at reset. You MUST first set **`CSR_MXSTATUS` (0x7C0) bit 22 (`THEADISAEE = 1`)** before invoking any cache maintenance instructions.

*(In **[Part 4](#part-4)**, we walk through how the AbstractX framework implements this complete high-performance cache pipeline).*

### 5.3 High-Bandwidth Verification

```bash
# Transfer 4 KB payloads across 1,000 iterations
ping_dram -n 1000 -s 4096
# Measured Result: > 100 MB/s sustained throughput, ~15 µs per 4 KB transfer
```

---

## 6. Summary: Choosing the Right Tool for the Job

1. **Need hard real-time latency (< 2.5 µs) and 0% Linux CPU?**
   Use **Paradigm 1** (Shared SRAM + UIO MSGBOX Doorbell).
2. **Need plug-and-play compatibility with Linux CLI scripts and existing tooling?**
   Use **Paradigm 2** (VirtIO RPMsg via `/dev/rpmsg0`).
3. **Need high-bandwidth streaming (> 100 MB/s) for camera, radar, or multi-sensor data?**
   Use **Paradigm 3** (Hybrid SRAM Control + DDR Carveout).

---

### Series Navigation
* **[Part 1: Architecture and Memory-Mapped Debugging](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063)**
* **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8)**
* **Part 3: Inter-Processor Communication (IPC) Deep Dive** *(You are here)*
* **[Part 4: C++20 Coroutines vs. FreeRTOS on Bare-Metal RISC-V](#part-4)**

---

#EmbeddedSystems #RISCV #Linux #RemoteProc #IPC #Allwinner #BareMetal #RealTime #SharedMemory
