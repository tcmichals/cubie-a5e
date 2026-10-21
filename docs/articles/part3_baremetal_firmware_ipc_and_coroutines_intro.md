<!--
================================================================================
HACKSTER.IO PROJECT SETTINGS (Copy & Paste into Hackster Form)
================================================================================
Project Title: Ultra-Low-Latency IPC: Connecting Linux and RISC-V on Allwinner T527 (Part 3)
Elevator Pitch: Achieving 14 µs polling / 58 µs event-driven round-trip latency, zero-drop 20,000 msg/s VirtIO streaming, and 100% crash-free multi-worker concurrency between ARM64 Linux and the auxiliary XuanTie E906 RISC-V co-processor on the Radxa Cubie A5E.
Hardware: Radxa Cubie A5E (Allwinner T527 / A527)
Software: Linux 6.x/7.x, RISC-V GCC, Allwinner Tina SDK
Tags: #RISCV #Linux #Embedded #RemoteProc #IPC #RealTime #Allwinner #FreeRTOS
================================================================================
-->

# Ultra-Low-Latency IPC: Connecting Linux and RISC-V on Allwinner T527 (Part 3)

*Part 3 of Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Radxa Cubie A5E)*

In **[Part 1](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063)** and **[Part 2](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8)**, we established the heterogeneous architecture, built the Linux `remoteproc` driver for the **Allwinner T527 / A527** (`sun55i`), and proved hardware execution with the `riscv-firmware/apps` verification suite.

Now we tackle the single most critical engineering question for any heterogeneous SoC:

> **How do the XuanTie E906 RISC-V co-processor and the ARM64 Linux host talk to each other without burning CPU or collapsing real-time determinism?**

Instead of burying the results at the end, here is the architectural reality up front: there is no single "best" IPC mechanism. Depending on whether your system needs hard real-time latency, Linux ecosystem compatibility, or bulk payload streaming, we developed and validated **three distinct IPC paradigms** running on actual silicon.

---

## 1. The Three IPC Paradigms at a Glance

Here is how the three architectures compare across hardware placement, latency, and throughput:

### Paradigm 1: Shared SRAM + Mailbox (`testPing`)
* **Round-Trip Latency**: **14 µs** (polling via `/dev/mem`) / **58.9 µs** (event-driven UIO `epoll` at ~0% CPU load).
* **Throughput & Payload**: High packet rate (~6,400–9,000 msgs/s), 512-byte packets (484-byte payload).
* **Host CPU Overhead**: 100% core burn in busy-polling mode (`ping_shm`) / **~0% CPU burn** in event-driven mode (`ping_uio`).
* **Memory Placement**: SRAM Space 0 (`0x072B0000` Host PA / `0x3FFF0000` Core DA).
* **Cache Coherency**: Zero coherency burden (dedicated on-chip SRAM bypasses L1/L2 caches).
* **Best Used For**: Sub-millisecond sensor/actuator control loops and deterministic hard real-time triggers.
* **Reference Firmware**: [`firmware/riscv-firmware/apps/testPing`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testPing)

### Paradigm 2: Standard VirtIO RPMsg (`testPingRpmsg` & `testFreeRTOS`)
* **Round-Trip Latency**: **130 µs** (on-chip SRAM) to **162 µs** (DDR) single-flight; **761 µs** (SRAM) to **1,355 µs** (DDR) under 16-thread saturation.
* **Throughput & Payload**: 6,100–7,600 msgs/s (~6–7 MB/s), standard 512-byte VirtIO buffers. Under 16-worker stress in SRAM, exceeds **20,400 msgs/s**.
* **Host CPU Overhead**: Linux kernel scheduler context switch overhead.
* **Memory Placement**: DDR carveout (`0x48000000`) or On-Chip SRAM Space 1 (`0x072C0000`).
* **Cache Coherency**: Zero coherency overhead (`dma_alloc_coherent` on Linux + `mhcr.DE = 0` on E906).
* **Best Used For**: Seamless Linux ecosystem integration (`/dev/rpmsg0`), standard scripts, and RTOS workers.
* **Reference Firmware**: [`testPingRpmsg`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testPingRpmsg) and [`testFreeRTOS`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testFreeRTOS)

### Paradigm 3: Hybrid SRAM Control + DDR Carveout (`testDRAMMsg`)
* **Round-Trip Latency**: **~192 µs** (4 KB frame), ~3,018 µs loaded.
* **Throughput & Payload**: **3.3 – 5.0 MB/s** aggregate streaming, **4 KB × 16 descriptor slots** (1 MB DDR pool).
* **Host CPU Overhead**: Userspace ring poll / zero-copy DDR DMA offload.
* **Memory Placement**: Fast descriptor ring in SRAM A2 (`0x00040000`) + 1 MB bulk payload window in DDR DRAM (`0x48100000`).
* **Cache Coherency**: Uncached bus access (`mhcr.DE = 0`, or unlocked T-Head vendor opcodes via `THEADISAEE`).
* **Best Used For**: High-bandwidth data planes (flight logs, camera frames, radar bursts) exceeding SRAM capacity.
* **Reference Firmware**: [`firmware/riscv-firmware/apps/testDRAMMsg`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testDRAMMsg)

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

---

## 2. The Shared Hardware Layer: Allwinner Message Box (MSGBOX)

All three paradigms share a common hardware primitive for inter-core signaling: the **Allwinner Hardware Message Box (MSGBOX)** located at physical address `0x03003000`.

```text
+-------------------------------------------------------------+
|      Allwinner T527 Hardware Message Box (0x03003000)       |
+------------------------------+------------------------------+
| ARM64 Linux (Cortex-A55)     | XuanTie E906 (RISC-V)        |
| ARM bank (host)              | RV bank (E906)               |
+------------------------------+------------------------------+
| TX/RX: Linux channel 9       | TX/RX: port 2, channel 1     |
| (one FIFO per direction)     | (one FIFO per direction)     |
+------------------------------+------------------------------+
| Host Interrupt Routing:      | Co-Processor Interrupts:     |
|  - GIC SPI 0   (ARM bank)    |  - PLIC IRQ 25 / CLIC L17    |
|  - GIC SPI 205 (TX Done IRQ) |  - RV bank RX IRQ            |
+------------------------------+------------------------------+
```

### 2.1 The Four-Bank Silicon Discovery (Upstream v3)
A critical finding during upstream Linux driver bring-up (`v3` series) is that the Allwinner message box contains **four independent hardware banks**:
* ARM (`0x03003000`): Cortex-A55
* DSP (`0x07120000`): HiFi4 DSP
* CPUS (`0x07094000`): Allwinner low-power core
* RV (`0x07136000`): XuanTie E906 RISC-V

Each bank has its own bus clock and reset line, spread over the main CCU, the MCU CCU and the R CCU (see the [message box reference](../platforms/ALLWINNER_SUN55I_MSGBOX_REFERENCE.md) §6). If software fails to enable the RV bank's clock, writes to the E906's message box are silently dropped by silicon gates! The upstream v3 driver explicitly manages all four banks.

Furthermore, upstream `remoteproc` consolidates to a **single bidirectional mailbox channel (`<&msgbox 9>`)**. Because each mailbox channel features a dedicated 8-entry FIFO in both directions, a single channel handle cleanly multiplexes host kicks and coprocessor notifications without race conditions.

### 2.2 Doorbell Signaling vs. Data Payloads
The MSGBOX is purely a **doorbell mechanism**. Its hardware FIFOs are only 8 entries deep (`SUN55I_MSGBOX_MAX_MSG = 8`). Pushing bulk application data through hardware FIFOs causes bus stalls and FIFO overflows. Instead, the MSGBOX carries a 32-bit token signaling: *"There is new work waiting for you in shared memory."*

The RISC-V hardware driver ([`riscv-firmware/common/hal/msgbox.hpp`](https://github.com/tcmichals/cubie-a5e/blob/main/firmware/common/hal/msgbox.hpp)) provides a non-blocking API:

```cpp
namespace hal {

class MsgBox {
public:
    enum class Channel : uint8_t {
        Channel0 = 0, Channel1 = 1, Channel2 = 2, Channel3 = 3
    };

    static void init() noexcept;

    // Non-blocking send: returns false if Tx FIFO is full
    static bool send(Channel ch, uint32_t data) noexcept;

    // Non-blocking receive: returns std::nullopt if Rx FIFO is empty
    static std::optional<uint32_t> receive(Channel ch) noexcept;

    [[nodiscard]] static bool is_rx_pending(Channel ch) noexcept;
    [[nodiscard]] static bool is_tx_ready(Channel ch) noexcept;
    [[nodiscard]] static uint32_t tx_pending(Channel ch) noexcept;

    static void enable_rx_irq(Channel ch, bool enable) noexcept;
    static void clear_irq_status(Channel ch) noexcept;
};

} // namespace hal
```

---

## 3. Paradigm 1: Ultra-Low-Latency Shared SRAM + Mailbox (`testPing`)

**Goal**: Achieve the fastest possible round-trip latency (14 µs polling, 58 µs event-driven) between ARM64 and RISC-V with **0% Linux host CPU burn**.

### 3.1 Memory Layout & Protocol
The channel is allocated in **SRAM Space 0** (4 KB window):
* **Linux Host Physical Address**: `0x072B0000` (`0x07280000 + 0x30000`)
* **RISC-V Device Address**: `0x3FFF0000` (`0x3FFC0000 + 0x30000`)

To eliminate locks and prevent false sharing, the protocol defines **one writer per field** using monotonic sequence numbers:

```c
/* common/include/shm_ping_protocol.h */
#define SHM_PING_MAGIC      0x50494E47UL /* "PING" */
#define SHM_PONG_MAGIC      0x504F4E47UL /* "PONG" */
#define SHM_ERROR_MAGIC     0x4552524FUL /* "ERRO": rejected malformed ping */

#if defined(__riscv)
#define SHM_PING_SRAM_ADDR  0x3FFF0000UL /* Core DA in SRAM Space 0 */
#else
#define SHM_PING_SRAM_ADDR  0x072B0000UL /* Host PA in SRAM Space 0 */
#endif
#define SHM_PING_SRAM_SIZE  0x1000UL     /* 4 KB */

/* 512-byte ping/pong packet aligned to 8-byte boundary */
struct __attribute__((aligned(8))) ShmPingPacket {
    uint32_t magic;         // SHM_PING_MAGIC, SHM_PONG_MAGIC or SHM_ERROR_MAGIC
    uint32_t seq;           // Sequence number matching ping_seq/pong_seq
    uint64_t host_tx_ts_ns; // Linux Host TX timestamp (CLOCK_MONOTONIC_RAW)
    uint64_t riscv_cycles;  // RISC-V mcycle hardware counter
    uint32_t payload_len;   // Length of payload
    char     payload[484];  // Payload data (total packet = 512 bytes)
};

struct __attribute__((aligned(8))) ShmPingChannel {
    volatile uint32_t ping_seq;     // Written only by Linux
    volatile uint32_t pong_seq;     // Written only by E906
    volatile uint32_t total_pings;  // E906 only: pings answered
    volatile uint32_t errors;       // E906 only: malformed pings rejected
    ShmPingPacket     ping_pkt;     // Linux -> E906
    ShmPingPacket     pong_pkt;     // E906 -> Linux
};
```

### 3.2 RISC-V Firmware: The Pong Handler
The slot is busy while `pong_seq != ping_seq`. Linux posts ping `N + 1` only when `pong_seq == ping_seq == N`. The E906 processes the message, publishes the reply with a full memory barrier (`fence rw, rw`), advances `pong_seq`, and emits a mailbox doorbell only if the host's FIFO is empty:

```cpp
/* apps/testPing/main.cpp */
#define SHM_CHANNEL ((volatile ShmPingChannel *)SHM_PING_SRAM_ADDR)

volatile ShmPingChannel *ch = SHM_CHANNEL;
uint32_t handled = ch->ping_seq;
ch->pong_seq = handled;

while (1) {
    // Keep incoming mailbox FIFO drained
    while (hal::MsgBox::is_rx_pending(hal::MsgBox::Channel::Channel1)) {
        (void)hal::MsgBox::receive(hal::MsgBox::Channel::Channel1);
    }

    uint32_t posted = ch->ping_seq;
    if (posted != handled) {
        // Ensure packet data is visible before reading
        asm volatile("fence rw, rw" ::: "memory");

        uint32_t pkt_seq = ch->ping_pkt.seq;
        uint64_t host_ts = ch->ping_pkt.host_tx_ts_ns;

        if (posted == handled + 1 && pkt_seq == posted &&
            ch->ping_pkt.magic == SHM_PING_MAGIC) {
            // Echo payload and write pong packet
            ch->pong_pkt.magic         = SHM_PONG_MAGIC;
            ch->pong_pkt.seq           = posted;
            ch->pong_pkt.host_tx_ts_ns = host_ts;
            ch->pong_pkt.riscv_cycles  = hal::Timer::get_ticks();
            ch->pong_pkt.payload_len   = sizeof(ch->pong_pkt.payload);

            // Memory barrier: publish pong_pkt before advancing pong_seq
            asm volatile("fence rw, rw" ::: "memory");
            ch->pong_seq = posted;
            asm volatile("fence rw, rw" ::: "memory");

            // Wake-up hint: send doorbell only if host FIFO has room
            if (hal::MsgBox::tx_pending(hal::MsgBox::Channel::Channel0) == 0) {
                (void)hal::MsgBox::send(hal::MsgBox::Channel::Channel0, posted);
            }
            ch->total_pings++;
            handled = posted;
        }
    }
}
```

### 3.3 Linux Host: Busy-Polling vs. Event-Driven UIO
We built two host test tools to demonstrate the real-world trade-offs:

1. **`ping_shm` (Busy Polling via `/dev/mem`)**:
   Polls `pong_seq` in a tight userspace loop. While it achieves the lowest absolute latency (**14.08 µs round trip**), it **burns 100% of an entire Cortex-A55 core**, generating thermal throttling and starving peer processes.
2. **`ping_uio` (Event-Driven via UIO + `epoll`)**:
   Uses the project-specific `sun55i-msgbox-uio` driver and the `-uio` base DTB. The userspace process sleeps in `epoll_wait()`. When the E906 writes the doorbell, hardware raises **GIC SPI 0**, Linux wakes the waiting process, and userspace reads the pong packet.
   * **Measured Result**: **58.90 µs round-trip latency at 0.0% idle CPU burn!**

```bash
# Event-driven: 50,000 round trips with zero host CPU load
ping_uio -n 50000
# Result: 58.9 µs avg RTT, 6,359 msgs/sec, 0% CPU burn
```

### 3.4 Lock-Free SPSC Ring Queue (Preventing False Sharing)
For continuous telemetry streams, [`hal::SpscQueue<T, Capacity>`](https://github.com/tcmichals/cubie-a5e/blob/main/firmware/common/hal/spsc_queue.hpp) provides a lock-free ring buffer. 

> ⚠️ **The 64-Byte Alignment Rule**: Notice the `alignas(64)` on `head_` and `tail_`. On an SMP SoC where the ARM Cortex-A55 has 64-byte L1 cache lines, placing `head_` and `tail_` on the same cache line causes **false sharing cache thrashing**. Aligning each pointer to separate cache lines prevents the cores from invalidating each other:

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

## 4. Paradigm 2: Standard Linux VirtIO RPMsg (`testPingRpmsg` & `testFreeRTOS`)

**Goal**: Full compatibility with standard Linux kernel IPC tools and userspace scripts via `/dev/rpmsg0`, with zero buffer overruns and zero mailbox overflows under multi-threaded stress.

### 4.1 How VirtIO RPMsg Operates
Unlike the custom SRAM ping protocol, RPMsg relies on the standard Linux `virtio_rpmsg_bus` driver. The RISC-V firmware declares a VirtIO device in its `.resource_table`:

```text
RISC-V Firmware                          Linux Kernel (remoteproc)
─────────────────                        ───────────────────────────────
.resource_table:                         virtio_rpmsg_bus:
  VDEV0 (VirtIO device, type=7)   ──►      Discovers descriptor rings
  vring0: TX ring (Remote -> Host)──►      Creates /dev/rpmsg0 character device
  vring1: RX ring (Host -> Remote)◄──      Routes standard open/read/write
```

### 4.2 Data Plane Backpressure: Why RPMsg Never Floods Co-Processor Memory
A frequent concern when pairing a high-speed multi-core Linux host with a modest 200 MHz co-processor is memory overrun: *what happens if Linux dumps 1,000 packets per millisecond into the co-processor's memory?*

Under VirtIO split-ring protocol, **co-processor memory flooding is structurally impossible**:
1. **Fixed Descriptor Pool (`VRING_NUM_DESCS`)**: The shared vring contains a fixed number of pre-allocated buffers (typically 8, 16, or 256 entries).
2. **Producer/Consumer Ring Throttling**: The host can only enqueue buffers by indexing the Available Ring (`s_rx_vq.avail->ring`). If the E906 is busy and has not processed existing descriptors, the ring fills up.
3. **Natural Backpressure**: When all descriptors are in flight, Linux's `virtqueue_add_outbuf()` returns `-ENOSPC`. The Linux userspace caller blocks in `write()` or yields in the kernel scheduler. Linux is physically incapable of overrunning the E906's memory or exhausting its stack.

### 4.3 The Doorbell Signaling Trap: Why Naive Mailboxes Flood (Case Study 15)
While the **data plane** has natural backpressure, the **signaling plane (the mailbox doorbell)** does not!

During live multi-worker stress benchmarking on the Cubie A5E (`stress_rpmsg -n 100000 -w 16`, 16 concurrent userspace threads), we uncovered a severe signaling flaw. The Linux console flooded with **1,598 warnings**:
```text
sun55i-msgbox 3003000.mailbox: Try increasing MBOX_TX_QUEUE_LEN
```

#### Why Did This Happen?
In bidirectional RPMsg, kicks originate from **two separate virtqueues** on Linux:
1. `vrp->svq` (Linux Host TX / Remote RX): Linux kicks the E906 whenever userspace transmits a message.
2. `vrp->rvq` (Linux Host RX / Remote TX): In `virtio_rpmsg_bus.c:rpmsg_recv_done()`, every time Linux consumes an echo reply from the E906, it replenishes the empty buffer back into `vrp->rvq` and immediately calls `virtqueue_kick(vrp->rvq)`.

With 16 parallel threads running continuous send/reply loops, 16 concurrent sends combined with 16 replenishment kicks flooded Linux's mailbox software queue (`MBOX_TX_QUEUE_LEN = 20`) with up to 32 requests in under a microsecond!

### 4.4 The Protocol Defense: VirtIO Kick Suppression (`VRING_USED_F_NO_NOTIFY`)
The VirtIO specification provides a built-in mechanism to prevent signaling floods: **Kick Suppression**. In the split ring layout, the consumer can set `VRING_USED_F_NO_NOTIFY` (bit 0 of `used->flags`). 

In the Linux kernel (`drivers/virtio/virtio_ring.c`):
```c
needs_kick = !(vq->split.vring.used->flags & VRING_USED_F_NO_NOTIFY);
```

If the flag is set, Linux **skips the hardware mailbox doorbell entirely**!

#### Implementation in Firmware (`hal::Rpmsg`):
1. **Continuous Polling Mode (`testPingRpmsg`)**:
   When the E906 runs a continuous polling loop (`while(1) { hal::Rpmsg::poll(); }`), it is already scanning `avail->idx` directly in shared memory at 200 MHz. Every doorbell sent by Linux during active polling is 100% redundant.
   * `hal::Rpmsg` initializes `s_rx_vq.used->flags = VRING_USED_F_NO_NOTIFY;`.
   * Result: Over a 100,000-message stress test, **only 1 initial host doorbell was ever emitted**—Linux ran entirely via shared memory!
2. **Buffer Replenishment Suppression (`s_tx_vq`)**:
   The E906 suppresses kicks on `s_tx_vq` by default, and only clears `flags = 0` if `avail_pending()` indicates that free reply buffers are completely exhausted. Linux only kicks when replenishing an empty ring.
3. **Mailbox Loop-Draining**:
   Firmware drains the hardware FIFO in a tight `while` loop across both Channel 0 and Channel 1 to ensure all pending doorbells are consumed in batches.

```cpp
/* firmware/common/hal/rpmsg.cpp */
bool Rpmsg::poll() noexcept {
    if (!is_driver_ready() || s_rx_vq.avail == nullptr) return false;

    // Drain all doorbells the host queued
    drain_host_doorbells();

    uint16_t pending = 0;
    if (!avail_pending(s_rx_vq, pending) || pending == 0) return false;

    // Suppress redundant host kicks while actively draining
    s_rx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    for (;;) {
        for (; pending > 0; --pending) {
            // Process descriptor and dispatch to endpoint callback...
            dispatch_message();
            return_descriptor_to_used();
        }
        // Re-check ring: did more buffers arrive while processing?
        if (!avail_pending(s_rx_vq, pending) || pending == 0) break;
    }

    notify_host(); // Tell host its transmit buffers are back
    return true;
}
```

### 4.5 Polling vs. RTOS Interrupt-Driven Wake (`testFreeRTOS`)
For battery-powered systems or complex applications, running a core at 100% busy-polling is unacceptable. We integrated FreeRTOS on the XuanTie E906 ([`apps/testFreeRTOS`](https://github.com/tcmichals/cubie-a5e/tree/main/firmware/riscv-firmware/apps/testFreeRTOS)) with dynamic kick suppression:

1. **Dynamic Unsuppression**: Before blocking on `ulTaskNotifyTake()`, the worker task unsuppresses kicks:
   ```cpp
   hal::Rpmsg::enable_rx_kick(true); // s_rx_vq.used->flags = 0
   ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
   hal::Rpmsg::enable_rx_kick(false); // Re-suppress upon ISR wake
   ```
2. **CLIC Line 17 ISR Wake**: When Linux transmits, hardware fires CLIC line 17. The ISR calls `vTaskNotifyGiveFromISR()` and wakes the worker task.

#### The "RTOS Tax": Head-to-Head Performance
* **Hardcoded Simple Polling (`testPingRpmsg`)**: **163.4 µs** avg RTT, **6,093 msgs/sec** (at 100% CPU spinning).
* **FreeRTOS ISR Wake (`testFreeRTOS`)**: **185.5 µs** avg RTT, **5,369 msgs/sec** (at ~0% idle CPU load).
* **The Takeaway**: FreeRTOS adds a modest **22.1 µs scheduler penalty** (ISR dispatch, register stack saving in `portASM_e906.S`, and context restoration), but frees up the co-processor to sleep or execute concurrent control loops!

### 4.6 Silicon Stress Sweep Results
Live silicon stress testing on the Radxa Cubie A5E confirmed that VirtIO kick suppression completely eliminated the mailbox queue overflow bug:

* **Profile 1: Standard DDR VirtIO (Timer Mode, 100k pkts, 16 workers)**:
  * *Pre-Fix*: 1,598 mailbox queue overflow warnings (`Try increasing MBOX_TX_QUEUE_LEN`)
  * *Post-Fix*: **0 warnings** | Data Integrity: **PASS (100,000/100,000)** | Avg RTT: 1,355.0 µs (1,489.7 µs max)
* **Profile 1T: Standard DDR VirtIO (ISR Mode, 100k pkts, 16 workers)**:
  * *Pre-Fix*: 1,598 mailbox queue overflow warnings
  * *Post-Fix*: **0 warnings** | Data Integrity: **PASS (100,000/100,000)** | Avg RTT: 1,355.2 µs (1,470.8 µs max)
* **Profile 2: On-Chip SRAM Space 1 VirtIO (`0x072C0000`, 100k pkts, 16 workers)**:
  * *Pre-Fix*: 1,598 mailbox queue overflow warnings
  * *Post-Fix*: **0 warnings** | Data Integrity: **PASS (100,000/100,000)** | Avg RTT: **760.9 µs** (842.1 µs max) — **nearly 2x faster than DDR!**
* **Profile 1 / 1T: FreeRTOS ISR Wake (`testFreeRTOS`, 2k pkts)**:
  * *Queue Overflows*: **0** | Data Integrity: **PASS (2,000/2,000)** | Avg RTT: 185.5 µs (265.2 µs max)

*Notice the Profile 2 advantage:* placing VirtIO descriptor rings in **On-Chip SRAM Space 1 (`0x072C0000`)** cuts multi-worker latency almost in half (**from 1,355 µs down to 760 µs**) and pushes sustained throughput beyond **20,400 msgs/sec**!

---

## 5. Paradigm 3: Hybrid SRAM Control + DDR Bulk Payload Pool (`testDRAMMsg`)

**Goal**: Stream massive data volumes (high-rate flight logs, camera frames, radar bursts) exceeding SRAM capacity while maintaining zero-jitter control responsiveness.

### 5.1 Architecture: Separating Control and Data Planes

```text
+-------------------------------------------------------------+
| FAST CONTROL PLANE: Dedicated SRAM A2 (0x00040000, 4 KB)    |
| * Single-cycle zero-wait-state access at 200 MHz            |
| * volatile uint32_t host_head, riscv_tail ring indices      |
| * DramSpscDesc tx_ring[16], rx_ring[16] descriptors         |
|   |--> dram_buf_offset --+                                  |
+--------------------------|----------------------------------+
                           |
                           v Points into 1 MB payload window
+-------------------------------------------------------------+
| BULK DATA PLANE: External DDR DRAM Pool (0x48100000, 1 MB)  |
| * 16 descriptor slots x 4096 bytes per slot                 |
| * Zero-copy uncached payload streaming                      |
+-------------------------------------------------------------+
```

#### Why This Split is Essential
If queue pointers and descriptor headers were kept in external DDR DRAM, every queue poll and index check would stall the RISC-V pipeline waiting on external DDR memory controller bus arbitration. 

By placing the **control block in SRAM** and **payloads in DDR**, ring synchronization completes in single-cycle SRAM bus turns, and external DDR DRAM is touched **only** during payload copy.

### 5.2 The Silicon Reality: Cache Coherency & The XuanTie Trap
When communicating over shared DDR DRAM, memory coherency is where most embedded projects hit a brick wall. The XuanTie E906 presents unique silicon constraints:

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

### 5.3 Measured Silicon Performance
```bash
# Transfer 4 KB payloads across 1,000 iterations
ping_dram -n 1000 -s 4096
# Single in flight: 192.11 µs avg RTT, 3.28 MB/sec bandwidth
# 16 in flight: 3,018 µs avg RTT, 5.00 MB/sec aggregate throughput
```

---

## 6. Summary: Choosing the Right Tool for the Job

1. **Need hard real-time determinism with 0% Linux CPU burn?**
   Use **Paradigm 1** (Shared SRAM + UIO MSGBOX Doorbell): **14 µs** polling or **58 µs** event-driven latency.
2. **Need plug-and-play compatibility with standard Linux tooling and scripts?**
   Use **Paradigm 2** (VirtIO RPMsg via `/dev/rpmsg0`): **130 µs – 162 µs** latency, fully protected by VirtIO kick suppression (`VRING_USED_F_NO_NOTIFY`). For maximum performance, run in **SRAM Space 1** (Profile 2) to cut loaded latency to **760 µs** and achieve **>20,000 msgs/sec**.
3. **Need bulk transfers for flight telemetry, image frames, or sensor logs?**
   Use **Paradigm 3** (Hybrid SRAM Control + DDR Carveout): up to **1 MB payload buffers** with single-cycle control synchronization.

---

### Series Navigation
* **[Part 1: Architecture and Memory-Mapped Debugging](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063)**
* **[Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8)**
* **Part 3: Inter-Processor Communication (IPC) Deep Dive** *(You are here)*
* **[Part 4: C++20 Coroutines vs. FreeRTOS on Bare-Metal RISC-V](#part-4)**

---

#EmbeddedSystems #RISCV #Linux #RemoteProc #IPC #Allwinner #BareMetal #RealTime #SharedMemory #FreeRTOS
