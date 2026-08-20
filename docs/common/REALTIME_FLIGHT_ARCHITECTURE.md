# ⏱️ Common Subsystem Guide: Real-Time Flight Architecture & OS Isolation

This document details the **Hard Real-Time Linux Strategy**, kernel CPU isolation parameters, POSIX real-time scheduling (`SCHED_FIFO`), memory lockdown (`mlockall`), and lock-free inter-process communication (IPC) for deterministic flight control loops.

---

## 1. Hard Real-Time OS Isolation (The "ArduPilot" Strategy)

Simply elevating a thread to real-time priority is insufficient on Linux because tick-timers, interrupt handlers, and background tasks can interrupt execution. The flight stack implements a complete multi-tier hardware isolation strategy:

```
┌──────────────────────────────────────────────────────────┐
│ Kernel Boot Parameters (boot.cmd / uboot-env.txt)        │
│ isolcpus=7 nohz_full=7 rcu_nocbs=7                       │
└────────────────────────────┬─────────────────────────────┘
                             │
                             ▼
┌──────────────────────────────────────────────────────────┐
│ CPU 7 Wall-off: Linux scheduler stripped from Core 7      │
│ • No tick timer interrupts (nohz_full=7)                 │
│ • No RCU callback processing (rcu_nocbs=7)               │
│ • Zero background tasks assigned to Core 7               │
└────────────────────────────┬─────────────────────────────┘
                             │
                             ▼
┌──────────────────────────────────────────────────────────┐
│ Real-Time Flight Daemon (rbb-server / INAV)              │
│ 1. mlockall(MCL_CURRENT | MCL_FUTURE) -> Lock physical RAM│
│ 2. pthread_setaffinity_np()          -> Pin strictly to #7│
│ 3. sched_setscheduler(SCHED_FIFO, 99)-> Max RT priority   │
│ 4. Blocks on /dev/uio0 Doorbell       -> 0% idle CPU burn │
└──────────────────────────────────────────────────────────┘
```

---

## 2. Low-Latency Inter-Processor IPC & UIO Doorbell

Communication between the Linux ARM host and the XuanTie RISC-V co-processor uses zero-copy circular ring buffers backed by hardware mailbox interrupts:

```
     ARM Linux (Host)                           XuanTie E907 (Co-processor)
┌─────────────────────────┐                     ┌─────────────────────────┐
│ Real-Time SCHED_FIFO    │                     │ Bare-Metal Coroutine    │
│ Daemon (Core 7)         │                     │ Flight Loop (ITCM)      │
└────────────┬────────────┘                     └────────────▲────────────┘
             │                                               │
             │ Write SPSC Ringbuffer (SRAM C: 0x07130000)     │
             ├───────────────────────────────────────────────┤
             │                                               │
             ▼                                               │
┌─────────────────────────┐  Hardware Doorbell  ┌────────────┴────────────┐
│ Mailbox Register        │ ──────────────────> │ Mailbox ISR Doorbell    │
│ (0x07090000)            │                     │ Wake Coroutine Event    │
└─────────────────────────┘                     └─────────────────────────┘
```

1. **Zero Busy-Polling**:
   - The ARM host daemon blocks on `read(/dev/uio0)`.
   - When the RISC-V co-processor pushes telemetry into SRAM C, it rings the hardware mailbox doorbell.
   - The UIO interrupt unblocks the `SCHED_FIFO` thread instantly with deterministic sub-microsecond latency.
2. **Zero-Copy Memory Access**:
   - Circular Single-Producer Single-Consumer (SPSC) ring buffers are mapped via `mmap()` on the host and direct pointers on the RISC-V core, eliminating buffer copies.
