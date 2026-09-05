# Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Part 3): Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro

In **[Part 1](part1_heterogeneous_riscv_intro_architecture.md)** and **[Part 2](part2_building_remoteproc_and_hardware_proof.md)**, we built the Linux `remoteproc` foundation for the **Allwinner T527 / A527** (`sun55i`) and verified on-chip Debug Module access using our automated Python DMI test harness.

With the hardware debugged and the lifecycle managed, we turn our attention to the auxiliary core itself: **How do we write clean, deterministic, high-performance bare-metal firmware on the XuanTie E907 RISC-V core?**

This article covers:
1. **Memory Determinism**: Why zero-wait-state Tightly Coupled Memory (TCM) beats DDR RAM for hard real-time tasks.
2. **Lightweight IPC**: Why a simple lock-free ring buffer (libmetal pattern) + Hardware Mailbox beats heavyweight RPMsg/VirtIO.
3. **Interactive Debugging**: Connecting `riscv-none-elf-gdb` and OpenOCD directly to the running core without physical JTAG probes.
4. **Introduction to Bare-Metal C++20 Coroutines**: A modern, stackless alternative to heavy RTOS task switching.

---

## 1. Memory Determinism: Zero-Wait-State TCM vs. DDR DRAM

The XuanTie E907 core features a Harvard architecture with 32 KB I-Cache and 32 KB D-Cache, running at **200 MHz** (RV32IMAFDC). 

While the RISC-V core *can* access the SoC's main system DDR RAM (mapped above `0x4000_0000`), executing code or polling data from DDR is **strongly discouraged for hard real-time tasks**:

```text
┌─────────────────────────────────────────────────────────────┐
│                    Main Interconnect (AXI/AHB)              │
├──────────────────────────────┬──────────────────────────────┤
│  ARM Cortex-A55 Cores (Linux)│  Mali GPU / NPU Accelerators │
└──────────────────────────────┴──────────────────────────────┘
                               ▲
                        [Bus Contention]
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                   Shared DDR DRAM Controller                │
│  - Unpredictable arbitration latency (DRAM refresh cycles)  │
│  - Cache misses introduce non-deterministic jitter          │
└─────────────────────────────────────────────────────────────┘
```

### The Solution: Fast Dedicated On-Chip SRAM
To eliminate bus contention and guarantee deterministic execution, the firmware payload runs directly out of high-speed on-chip SRAM:

* **Shared PubSRAM C (128 KB @ `0x0002_0000`)**: Identity-mapped fast execution window hosting the exception vector table, CRT0 bootstrap, application runtime, and stack.
* **Dedicated MCU SRAM (256 KB @ `0x3FFC_0000` Core / `0x0728_0000` Host)**: Zero-wait-state dedicated SRAM window for high-rate control loops and lock-free IPC ring buffers.

```ld
/* Linker Script: firmware_t527.ld */
MEMORY {
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K
}
```

By linking critical interrupt service routines and state machines directly into on-chip SRAM, the firmware guarantees cycle-accurate execution completely isolated from Linux DDR bus traffic.

---

## 2. Lightweight Inter-Processor Communication (IPC)

The standard Linux RemoteProc framework includes **RPMsg (Remote Processor Messaging)** over VirtIO ring buffers. While RPMsg is great for general-purpose messaging, it introduces significant overhead for high-frequency embedded sensor streams:
* Dynamic buffer allocations and scatter-gather descriptors.
* VirtIO protocol state machines and header parsing.
* Multiple interrupt transitions per message.

### The Lightweight Alternative: Lock-Free Shared SRAM + Hardware Mailbox

For high-frequency telemetry and sensor acquisition, a **lock-free single-producer single-consumer (SPSC) circular queue** in shared PubSRAM C coupled with the **Allwinner Hardware Message Box (`0x03003000`)** provides microsecond latency with zero memory allocations:

```text
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  RISC-V Core (XuanTie E907)  │        │  ARM64 Host (Linux Kernel)   │
│  - High-frequency sensor poll│        │  - Data consumer / Telemetry │
└──────────────┬───────────────┘        └──────────────┬───────────────┘
               │                                       ▲
               │ 1. Write Packet (Zero Copy)           │ 3. Read Packet
               ▼                                       │
┌──────────────────────────────────────────────────────┴───────────────┐
│           Shared PubSRAM C Window (32 KB @ 0x00020000)               │
│   [ Head Pointer ] ──► [ Lock-Free Circular Buffer ] ◄── [ Tail Ptr ]│
└──────────────────────────────────────┬───────────────────────────────┘
                                       │
                       2. Pulse Mailbox Doorbell IRQ
                                       ▼
┌──────────────────────────────────────────────────────────────────────┐
│             Allwinner Hardware Message Box (0x03003000)              │
│       Fires ARM GIC IRQ 147 / Triggers RISC-V PLIC Interrupt         │
└──────────────────────────────────────────────────────────────────────┘
```

* **Zero-Copy**: The RISC-V core writes telemetry frames directly into the mapped SRAM buffer.
* **Lock-Free Concurrency**: Head and tail indices use atomic acquire-release semantics (`std::atomic<uint32_t>`).
* **Hardware Doorbell**: When a batch is ready, writing a 32-bit word to the Allwinner MSGBOX register triggers an instant hardware interrupt (ARM GIC IRQ 147) without burning CPU cycles on polling.

---

## 3. Interactive Debugging with OpenOCD & GDB

Because the Debug Module is mapped over the internal SoC interconnect, we can attach standard `gdb` over TCP:

```bash
# 1. Start OpenOCD on the target board (listening on :3333 for GDB)
openocd -f /etc/openocd/openocd_t527_local.cfg &

# 2. From your development workstation, connect GDB:
riscv-none-elf-gdb firmware.elf

# 3. Inside GDB:
(gdb) set arch riscv:rv32
(gdb) target remote cubieboard:3333
Remote debugging using cubieboard:3333
0x00000000 in _start ()

# 4. Set breakpoints, step through C++ code, and inspect registers:
(gdb) break main
(gdb) continue
Continuing.
Breakpoint 1, main () at main.cpp:24
24          hardware_init();
(gdb) info registers
ra             0x54     0x54
sp             0x8fff0  0x8fff0
gp             0x713800 0x713800
tp             0x0      0x0
t0             0x0      0
```

Hardware breakpoints and memory watches work transparently, allowing you to catch stack overflows or state machine bugs in real time.

---

## 4. Modern Bare-Metal C++: An Introduction to Coroutines

Embedded developers often feel forced into a binary choice:
1. **Bare-metal `while(1)` super-loops with manual state machines**: Fast and tiny, but cumbersome and prone to `switch(state)` spaghetti code.
2. **A Real-Time Operating System (FreeRTOS, Zephyr)**: Structured multi-tasking, but each task requires a dedicated stack (typically 1 KB to 4 KB per task), context-switch overhead, and mutex contention.

### The Modern Alternative: C++20 Stackless Coroutines (`co_await`, `co_yield`)

C++20 coroutines introduce **stackless cooperative multitasking** directly at the language level:

```cpp
#include <coroutine>
#include <stdint.h>

struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

// A non-blocking sensor acquisition task
Task sensor_poll_task() {
    while (true) {
        start_adc_conversion();
        
        // Yield control back to scheduler until ADC conversion is complete
        while (!adc_conversion_ready()) {
            co_await std::suspend_always{};
        }
        
        uint32_t sample = read_adc_sample();
        process_sample(sample);
    }
}
```

### Why Coroutines Excel on Bare-Metal Microcontrollers:
* **Microscopic Memory Footprint**: Unlike an RTOS thread that needs a 2 KB stack, a coroutine's state frame is automatically generated by the compiler and often takes **less than 64 bytes**.
* **Zero Context Switch Overhead**: Resuming a coroutine is a single indirect function call—no saving/restoring all 32 CPU registers to the stack.
* **Readable Asynchronous Code**: Write sequential-looking code (`co_await`) without messy callbacks or fragmented switch-case state tables.

---

## 5. Summary & Coming Up in Part 4

In Part 3, we explored:
1. Guaranteeing execution determinism by isolating code in **64 KB ITCM** and **64 KB DTCM**.
2. Building an ultra-low-latency **lock-free shared SRAM ring buffer** with hardware Mailbox interrupts.
3. Interactive JTAG-less debugging via **OpenOCD and GDB**.
4. How **C++20 coroutines** bridge the gap between simple super-loops and heavy RTOS threads.

In **Part 4**, we will dive deep into coroutine implementation details:
* Step-by-step construction of a bare-metal, allocation-free coroutine scheduler.
* Benchmarking coroutine context-switching latency against FreeRTOS on the XuanTie E907.
* Handling real-time timer events, SPI streams, and Mailbox IPC using `co_await`.

---

### Series Navigation
* **[Part 1: Architecture and Memory-Mapped Debugging](part1_heterogeneous_riscv_intro_architecture.md)**
* **[Part 2: Building the Linux `remoteproc` Driver and Proving Hardware State](part2_building_remoteproc_and_hardware_proof.md)**
* **Part 3: Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro** *(You are here)*
* **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E907](part4_deep_dive_baremetal_cpp_coroutines.md)**

---

#EmbeddedSystems #RISCV #Cpp20 #Coroutines #BareMetal #OpenOCD #Allwinner #Firmware #RealTime
