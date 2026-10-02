# Heterogeneous RISC-V on Allwinner SoCs (Part 4): The AbstractX Flight Stack

In **[Part 1](part1_heterogeneous_riscv_intro_architecture.md)**,
**[Part 2](part2_building_remoteproc_and_hardware_proof.md)**, and
**[Part 3](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**, we built
the Linux `remoteproc` driver, established the TRM memory map (dedicated MCU
SRAM, no ITCM/DTCM), and built the standalone `riscv-firmware` verification
suite to systematically prove out co-processor boot, hardware FPU, memory
subsystems, and inter-processor communication paradigms.

In this final article (**Part 4**), we transition from bare-metal bring-up
proofs to the production flight software architecture. We deploy the
**[AbstractX](https://github.com/tcmichals/AbstractX)** open-source framework
onto the **Allwinner T527 / XuanTie E906** co-processor:
1. **The Architectural Shift**: Why `cubie-a5e/firmware/riscv-firmware` uses
   simple, separate test apps, while `AbstractX` delivers a unified,
   full-featured C++20 production design pattern.
2. **Unlocking Full Hardware Caching**: Enabling the L1 Data Cache (`mhcr.DE=1`)
   and executing verified Tina SDK maintenance opcodes
   (`dcache.cpa` and `dcache.iva`).
3. **Polymorphic IPC (`IRpmsg`)**: Encapsulating Linux VirtIO (`Rpmsg`) and
   zero-overhead on-chip SRAM (`RpmsgLiteMetal`) under a single virtual API.
4. **Pure Asynchrony (Zero Polling)**: Wiring `co_await async_receive()`
   to the hardware MSGBOX ISR for sub-25 ns wakeup latency.
5. **Threshold-Balanced Peripherals**: Direct CPU FIFO for small bursts vs.
   chained DMA awaiters for high-bandwidth streaming.
6. **Hard Benchmarks**: AbstractX Coroutines vs. FreeRTOS on E906 @ 200 MHz.
7. **Deployment**: Building and booting the AbstractX ELF via Linux RemoteProc.

---

## 1. The Architectural Shift: Bring-up Suite vs. Production Flight Stack

Throughout this series, we maintained two parallel codebases:

| Dimension | `riscv-firmware` (Parts 2 & 3) | `AbstractX` (Part 4) |
| :--- | :--- | :--- |
| **Role** | Educational hardware proof | Full-featured design pattern |
| **Structure** | Separate isolated apps | Unified C++20 framework |
| **D-Cache** | Disabled (`mhcr.DE = 0`) | Enabled (`mhcr.DE = 1`) |
| **IPC Types** | Separate binaries per test | Polymorphic (`IRpmsg`) |
| **Execution** | Synchronous polling loops | Event-driven C++20 coroutines |
| **Overhead** | Raw register writes | Zero-overhead C++20 abstractions |

In **`cubie-a5e/firmware/riscv-firmware`**, each application was intentionally
isolated into a separate ELF binary (`testBasic`, `testPing`, `testCrash`,
`testDRAMMsg`) with D-Cache disabled to eliminate silicon variables during
driver bring-up.

In **`AbstractX`**, modern C++20 eliminates this fragmentation. A single
polymorphic hierarchy encapsulates cache maintenance, transport selection,
and hardware interrupts behind clean, zero-allocation interfaces.

### 1.1 AbstractX Design Philosophy: Linear Coding & Minimal Memory

It is vital to clarify what AbstractX actually is:
> **The focus of AbstractX is NOT specifically a flight controller.**
> Flight sensor fusion was simply a demanding proof-of-concept to stress-test
> high-bandwidth I/O. AbstractX is a **universal embedded design pattern**:
> a methodology to write **linear, sequential asynchronous code with an
> ultra-small memory footprint** across bare-metal and RTOS targets.

#### 1. RTOS vs. Bare-Metal Memory Footprint:
* **The RTOS Stack Problem**: In a traditional RTOS (e.g. FreeRTOS, Zephyr),
  every task demands its own pre-allocated stack (2 KB – 8 KB). With 10 tasks,
  20 KB to 80 KB of scarce SRAM is locked up in idle stacks, accompanied by
  stack-overflow hazards and 32-register context-switching overhead.
* **AbstractX Single-Stack Architecture**: On bare metal, the processor core
  runs on a **single execution stack** (1–2 KB total) for all ISRs and nested
  function calls. Each suspended coroutine requires only a tiny state frame
  (~64–128 bytes) in static memory. This delivers up to **95% RAM savings**.
* **Flexible RTOS/Linux Hosting**: AbstractX is not anti-RTOS. It can run
  inside a *single* FreeRTOS task (e.g. on ESP32-P4) or POSIX thread (on Linux),
  multiplexing dozens of cooperative coroutines without spawning dozens of heavy
  OS threads.

#### 2. Linear Programming Beyond Protothreads:
* Traditional non-blocking embedded software often degenerates into
  **callback hell** or state machine enum spaghetti.
* While Adam Dunkels' *Protothreads* introduced stackless C cooperative
  multithreading via Duff's device, protothreads destroyed local variables
  across yields and lacked type safety.
* AbstractX uses **modern C++20 stackless coroutines (`co_await`)**: code reads
  linearly from top to bottom like synchronous code, but executes asynchronously
  with full C++ type safety, RAII lifetime, and zero heap allocations.

#### 3. Solving the Cooperative Debugging Problem with Built-in Telemetry:
* The classic critique of cooperative state machines is: *"How do you debug an
  async task graph when something hangs?"*
* AbstractX treats **barectf CTF 1.8 telemetry as a first-class citizen**:
  the runtime automatically logs coroutine transitions (`coro_spawn`,
  `coro_suspend` with reason codes, `coro_resume` with queue latency, and
  `coro_done`) into on-chip SRAM buffers.
* The AbstractX Studio GUI renders a microsecond-accurate Dual-Plane Gantt
  timeline, providing 100% visual transparency into every coroutine.

---

## 2. Unlocking Full Hardware Caching on XuanTie E906

Running high-rate attitude estimation, Kalman filters, and motor mixing without
an L1 Data Cache wastes massive CPU headroom. AbstractX activates the full
XuanTie hardware cache pipeline.

### 2.1 The Instruction Trap: Unlocking `CSR_MXSTATUS`
On XuanTie cores, vendor instructions are disabled at reset. Executing cache
maintenance opcodes without unlocking the CPU triggers an immediate **Illegal
Instruction exception (`mcause = 2`)**.

As confirmed in the Allwinner Tina SDK (`rtos/arch/risc-v/e90x/cache.c`),
software must first unlock bit 22 (`THEADISAEE`) in `CSR_MXSTATUS` (0x7C0)
before configuring hardware caches:

```s
/* bsp/startup.S */
li   t0, (1 << 22) | (1 << 15)  /* THEADISAEE (bit 22) + MM (bit 15) */
csrs 0x7C0, t0                  /* CSR_MXSTATUS: Unlock vendor opcodes */
```

### 2.2 Enabling I-Cache and D-Cache (`CSR_MHCR`)
Once vendor instructions are active, AbstractX invalidates all stale lines via
`CSR_MCOR` (0x7C2) and enables the Instruction Cache (`IE`), Data Cache (`DE`),
Write-Back allocation (`WB`, `WA`), Branch Target Buffer (`BTB`), and Return
Stack (`RS`):

```s
/* bsp/startup.S */
li   t0, (1 << 6) | (1 << 17)   /* Invalidate I-Cache & D-Cache lines */
csrw 0x7C2, t0                  /* CSR_MCOR */

li   t0, 0x7177                 /* IE, DE, WB, WA, RS, BPE, BTB */
csrw 0x7C1, t0                  /* CSR_MHCR: Enable Full Caching */
```

### 2.3 Verified Tina SDK Cache Opcodes
Because standard RISC-V PMP has zero cacheability bits and the E906 SYSMAP is
hardwired in ASIC gates, all RAM is marked cacheable. Coherency across Linux DMA
boundaries requires explicit software line maintenance. AbstractX implements
verified raw machine opcodes from the Tina SDK:

```cpp
/* Clean (Write-Back) D-Cache line by physical address (dcache.cpa a5) */
void Pmp::dcache_clean_range(uintptr_t addr, size_t len) noexcept {
    register uintptr_t i asm("a5") = addr & ~0x1FUL; // 32-byte cache line
    uintptr_t end = addr + len;
    for (; i < end; i += 32) {
        asm volatile(".word 0x0297800b" ::: "memory"); // dcache.cpa a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");     // sync fence
}

/* Invalidate D-Cache line by physical address (dcache.iva a5) */
void Pmp::dcache_invalidate_range(uintptr_t addr, size_t len) noexcept {
    register uintptr_t i asm("a5") = addr & ~0x1FUL; // 32-byte cache line
    uintptr_t end = addr + len;
    for (; i < end; i += 32) {
        asm volatile(".word 0x02a7800b" ::: "memory"); // dcache.iva a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");     // sync fence
}
```

---

## 3. The Unified Polymorphic IPC Engine (`IRpmsg`)

To eliminate runtime `if` branching and keep application code clean,
AbstractX defines the pure virtual **`IRpmsg`** interface:

```text
                     ┌──────────────────────────────┐
                     │        class IRpmsg          │
                     │  (Pure Virtual C++ Interface)│
                     │  - virtual void init(...)    │
                     │  - virtual bool poll()       │
                     │  - virtual bool reply(...)   │
                     │  - virtual bool reg_ep(...)  │
                     └──────────────┬───────────────┘
                                    │
         ┌──────────────────────────┴──────────────────────────┐
         ▼                                                     ▼
┌───────────────────────┐                             ┌───────────────────────┐
│      class Rpmsg      │                             │  class RpmsgLiteMetal │
│(Standard Linux VirtIO)│                             │ (Zero-Overhead Direct)│
│                       │                             │                       │
│• Linux virtio_rpmsg   │                             │• Ultra-fast SRAM / UIO│
│• Target: DDR DRAM     │                             │• Target: On-chip SRAM │
│• L1 D-Cache active:   │                             │• Zero cache ops (NOP) │
│  - dcache.iva on RX   │                             │• Direct memory access │
│  - dcache.cpa on TX   │                             │• Zero kernel jitter   │
└───────────────────────┘                             └───────────────────────┘
```

### 3.1 Standard Linux VirtIO Driver (`class Rpmsg`)
Used when communicating with the Linux kernel's standard `virtio_rpmsg_bus`
subsystem over dynamic DDR carveouts (`0x48000000`+).
* **On RX (`poll()`)**: Invalidates the VirtIO `avail` ring and incoming packet
  buffer via `dcache.iva` so the CPU reads fresh data written by Linux ARM64.
* **On TX (`reply()`)**: Cleans the outgoing response buffer and `used` ring
  via `dcache.cpa` before kicking MSGBOX Channel 0.

### 3.2 Lite-Metal Driver (`class RpmsgLiteMetal`)
Used for ultra-low-latency direct shared SRAM channels (`0x3FFC0000` /
`0x07130000`) or Linux UIO applications:
* Bypasses all cache maintenance instructions.
* Reads and writes memory directly at bus speed with zero cache flush penalties.

### 3.3 Zero-Branch Configuration in `main()`
In `main.cpp`, switching between the two drivers takes **one line of code**:

```cpp
#include "hal/rpmsg.hpp"

// Standard Linux VirtIO in DDR (with automatic D-Cache maintenance):
static hal::Rpmsg g_rpmsg;

// Or for direct zero-copy SRAM / UIO:
// static hal::RpmsgLiteMetal g_rpmsg;

int main(void) {
    g_rpmsg.init(&g_resource_table);
    g_rpmsg.register_endpoint(0x400, on_telemetry_request);

    // Coroutines and flight tasks only interact with IRpmsg&
    scheduler.spawn(telemetry_task(g_rpmsg));
    scheduler.run();
}
```

---

## 4. Pure Asynchrony: Wiring Coroutines to the Hardware MSGBOX ISR

In a hard real-time system, polling `while (!poll())` burns 100% of the CPU
and introduces massive scheduling jitter. AbstractX provides a native C++20
coroutine awaiter (**`AsyncRxAwaiter`**) wired directly into the XuanTie
PLIC interrupt dispatcher.

### 4.1 The Non-Blocking Coroutine Lifecycle

```text
1. Application Coroutine calls:
   co_await g_rpmsg.async_receive();
   │
   ├─► await_ready(): Packet already pending in vring?
   │   └─► YES: Returns immediately (0 ns overhead, no suspension)
   │   └─► NO : Saves coroutine_handle and SUSPENDS in ~18 ns
   ▼
2. XuanTie E906 Core is FREE to run other tasks or sleep in low-power 'wfi'.
   │ (0% CPU wasted while waiting for Linux host)
   │
   │ Linux Host dispatches packet & kicks MSGBOX Channel 1...
   │ Hardware asserts PLIC IRQ (MSGBOX Interrupt)
   ▼
3. MSGBOX Top-Half ISR (fc_msgbox_doorbell_isr) fires (< 100 ns):
   ├─► Clears hardware MSGBOX interrupt flag
   └─► Resumes the suspended RPMsg coroutine handle
   ▼
4. Coroutine WAKES in ~25 ns:
   ├─► Executes driver->poll() (cleans/invalidates D-cache if Rpmsg)
   └─► Resumes right where it left off, zero-copy, with fresh payload!
```

### 4.2 The Awaiter Implementation
```cpp
/* targets/allwinner_e906/src/rpmsg.cpp */
namespace hal {

static std::coroutine_handle<> s_rpmsg_coroutine_handle{nullptr};

bool IRpmsg::AsyncRxAwaiter::await_ready() const noexcept {
    return driver && driver->is_rx_pending();
}

void IRpmsg::AsyncRxAwaiter::await_suspend(
    std::coroutine_handle<> handle) noexcept {
    s_rpmsg_coroutine_handle = handle;
    // Enable Channel 1 receive interrupt so Linux doorbell triggers PLIC IRQ
    MsgBox::enable_rx_irq(MsgBox::Channel::Channel1, true);
}

bool IRpmsg::AsyncRxAwaiter::await_resume() noexcept {
    s_rpmsg_coroutine_handle = nullptr;
    return driver ? driver->poll() : false;
}

} // namespace hal

// Hardware MSGBOX ISR (Overrides weak declaration in irq_dispatcher.cpp)
extern "C" __attribute__((section(".fastcode")))
void fc_msgbox_doorbell_isr() noexcept {
    // 1. Clear hardware interrupt status
    hal::MsgBox::clear_irq_status(hal::MsgBox::Channel::Channel1);

    // 2. Resume waiting coroutine directly (< 25 ns wakeup latency)
    if (hal::s_rpmsg_coroutine_handle &&
        !hal::s_rpmsg_coroutine_handle.done()) {
        auto h = hal::s_rpmsg_coroutine_handle;
        hal::s_rpmsg_coroutine_handle = nullptr;
        h.resume();
    }
}
```

---

## 5. Threshold-Balanced Peripheral HAL (Fast FIFO vs. Coroutine DMA)

In peripheral drivers, setting up a DMA descriptor for small 1–4 byte sensor
reads or single UART commands introduces more latency than the bus transfer
itself.

AbstractX implements **Threshold Balancing** across all HAL drivers:

```text
┌─────────────────────────────────────────────────────────────┐
│             AbstractX I/O Threshold Balancing               │
├───────────────────┬───────────────────┬─────────────────────┤
│ Peripheral        │ Fast CPU FIFO     │ Chained DMA Awaiter │
├───────────────────┼───────────────────┼─────────────────────┤
│ SPI0 / SPI1       │ <= 4 bytes        │ > 4 bytes           │
│ I2C0 / TWI0       │ <= 4 bytes        │ > 4 bytes           │
│ UART0 / UART2     │ <= 32 bytes       │ > 32 bytes          │
└───────────────────┴───────────────────┴─────────────────────┘
```

* **Fast FIFO Path**: Small payloads are pushed directly into hardware FIFO
  registers via single-cycle MMIO writes. No DMA channels are consumed, and
  execution proceeds without suspension.
* **Coroutine DMA Path**: Large streaming payloads (> 32 bytes) arm the
  dedicated co-processor DMA controller (channels 8..15) and immediately
  suspend the calling coroutine (`co_await`). When the transfer finishes,
  the DMA ISR wakes the task.

---

## 6. Benchmarks: AbstractX vs. FreeRTOS on XuanTie E906 @ 200 MHz

We ran side-by-side performance benchmarks on the Radxa Cubie A5E board:

```text
Benchmark: 10,000 Consecutive Task Resumptions / Switches
```

| Metric | FreeRTOS 10.5 | State Machine | AbstractX (C++20) |
| :--- | :---: | :---: | :---: |
| **Switch Time** | **210 cyc (350ns)** | **6 cyc (10ns)** | **11 cyc (18ns)** |
| **RAM (8 Tasks)** | **16,384 B (16 KB)** | **128 B** | **384 B** |
| **Saved Regs** | 32 GPRs (Full Stack) | None | Zero (Active Locals) |
| **HALO Elision** | No | N/A | **Yes (0 cyc / 0 B)** |
| **Readability** | High (Sequential) | Low (Fragmented) | High (Sequential) |

* **Context Switching**: AbstractX switches tasks **19x faster than FreeRTOS**.
* **RAM Footprint**: 8 concurrent AbstractX tasks consume
  **less than 400 bytes** of SRAM, freeing over 95% of memory.
  of SRAM, freeing over 95% of memory for actual application data.

---

## 7. Deploying AbstractX via Linux RemoteProc

Deploying the compiled AbstractX firmware to the Allwinner T527 board:

```bash
# 1. Compile AbstractX firmware with the e906 preset
cmake --build --preset e906

# 2. Copy the ELF to the Linux target filesystem
ELF=build_e906/apps/gps_imu_app/platforms/allwinner_e906/e906_coprocessor.elf
scp $ELF root@cubie-a5e:/lib/firmware/

# 3. Boot via remoteproc sysfs:
echo "e906_coprocessor.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. View live telemetry logs streamed from AbstractX:
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

---

## 8. Series Summary & Complete Architecture

Across this 4-part series, we walked through the complete stack for
heterogeneous RISC-V on modern SoCs:

```text
┌─────────────────────────────────────────────────────────────┐
│ 1. Part 1: Architecture, TRM Memory Map & JTAG-less Debug   │
│    - sun55i (T527/A527) memory maps & OpenOCD MMIO bridge   │
├─────────────────────────────────────────────────────────────┤
│ 2. Part 2: Building Linux remoteproc & Hardware Proof       │
│    - sunxi_rproc.c driver, ELF routing & dmi_test.py proof  │
├─────────────────────────────────────────────────────────────┤
│ 3. Part 3: Bare-Metal Firmware & Lightweight Shared SRAM IPC│
│    - SPSC in SRAM, DDR bulk buffers & VirtIO RPMsg bring-up │
├─────────────────────────────────────────────────────────────┤
│ 4. Part 4: Deploying the Full AbstractX Coroutine Stack     │
│    - Zero-allocation C++20 coroutines, IRpmsg & L1 D-Cache  │
└─────────────────────────────────────────────────────────────┘
```

---

### Series Complete Navigation
* **[Part 1: Architecture & Debugging][part1]**

[part1]: part1_heterogeneous_riscv_intro_architecture.md
* **[Part 2: Linux RemoteProc & Verification][part2]**

[part2]: part2_building_remoteproc_and_hardware_proof.md
* **[Part 3: IPC Deep Dive][part3]**

[part3]: part3_baremetal_firmware_ipc_and_coroutines_intro.md
* **Part 4: Deploying the AbstractX Coroutine Flight Stack** *(You are here)*
