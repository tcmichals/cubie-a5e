# High-Speed Hardware Abstraction Layer (HAL) & DMA Architecture

This document details the hard real-time, non-blocking, and zero-copy hardware driver architecture for the **XuanTie E907 RISC-V Co-Processor** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**.

---

## 1. Core Architectural Philosophy: Zero Polling, Pure Asynchrony

In a high-throughput **Hardware I/O Processor (`ioProcessor`)** moving PCIe Transaction Layer Packets (TLPs), IMU sensor data, and UART streams:
* ❌ **Busy-waiting in `while()` loops is strictly forbidden.** Polling pegs the CPU at 100%, introduces severe jitter, and starves concurrent I/O channels.
*  **All HAL operations are asynchronous and interrupt/DMA-driven via AbstractX C++20 coroutines (`co_await`).**

When a transfer is initiated, the driver arms the hardware and **immediately suspends the coroutine (~18 ns)**. The CPU is instantly freed to process other streams or sleep in low-power `wfi`. When the hardware completes, the interrupt handler resumes the coroutine in **~25 ns**.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. Coroutine calls: co_await Spi0::async_transceive_dual(tx, rx, len);      │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ 1. Driver loads FIFO / arms DMA
                                       │ 2. Enables Transfer Complete IRQ (SPI_IER.TC)
                                       │ 3. Coroutine SUSPENDS (~18 ns)
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. XuanTie E907 Core executes other tasks or enters low-power 'wfi' sleep.   │
│    (0% CPU wasted during bus shifting)                                      │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Hardware shifts bits at 25-50 MHz...
                                       │ SPI Controller asserts PLIC IRQ 15 (TC)
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 3. Interrupt Handler (PLIC ISR - Top Half):                                 │
│    - Drains RX FIFO / verifies DMA completion                               │
│    - Deasserts Chip Select (PC3) & clears SPI_ISR.TC                        │
│    - Posts coroutine handle to thread-safe SPSC Ready Queue:                │
│        IsrDispatcher::isr_post_resume(handle);                              │
│    - Triggers Machine Software Interrupt (MSIP) to wake CPU                 │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Top-half exits immediately (< 100 ns)
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 4. Main Thread Scheduler (Bottom Half in main.cpp):                         │
│    - Drains SPSC Ready Queue in thread context on main stack                │
│    - Resumes coroutine safely without interrupt stack overflow risks!       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. High-Speed SPI0: Asynchronous Dual/Single Mode Engine

### Controller Register Offsets (`SPI0_BASE` = `0x04025000`)
* `0x08` — `SPI_TCR`: Transfer Control (Dual Mode bit 28, CPOL/CPHA, SS_OWN)
* `0x10` — `SPI_IER`: Interrupt Enable
  * Bit 12: `TC_INT_EN` (Transfer Complete Interrupt)
  * Bit 10: `TF_ERQ_INT_EN` (TX FIFO Watermark)
  * Bit 1: `RF_FQL_INT_EN` (RX FIFO Watermark)
* `0x14` — `SPI_ISR`: Interrupt Status (Bit 12 `TC` - Transfer Complete)
* `0x18` — `SPI_FCR`: FIFO Control & DMA Request Enable (Bit 24 `DRQ_EN`)

### Operating Modes
1. **Dual-IO High-Speed Link (FPGA @ CS0 `PC3`):**
   * Transmits and receives PCIe TLPs across `PC2` (IO0) and `PC4` (IO1) at **25–50 MHz**.
   * Hardware Master Burst Counter (`SPI_MBC`) and Master Transmit Counter (`SPI_MTC`) are pre-loaded.
   * On transfer finish, `TC_INT_EN` fires, waking the TLP dispatcher coroutine.
2. **Single Full-Duplex Mode (IMU @ CS1 `PC7`):**
   * Standard 4-wire SPI at 10 MHz for low-latency sensor sampling.

---

## 3. High-Speed UART: Zero-Copy RX DMA + Receiver Timeout (RTO)

A common challenge with UART DMA is handling **variable-length packets** (e.g. 24-byte CRSF frames, 48-byte GPS NMEA strings). If software waited for the DMA buffer to fill, packets would be delayed indefinitely.

The `ioProcessor` solves this using **Circular RX DMA combined with DesignWare 8250 Receiver Timeout (RTO)**:

```
                       INCOMING SERIAL DATA STREAM (e.g. 24-byte packet)
                              ┌───┬───┬───┬───┬───┐
                      ───────►│ 1 │ 2 │ 3 │...│24 │───► (Line goes IDLE)
                              └───┴───┴───┴───┴───┘
                                        │
             ┌──────────────────────────┴──────────────────────────┐
             │                                                     │
             ▼                                                     ▼
┌─────────────────────────┐                               ┌─────────────────────────┐
│     DMA CONTROLLER      │                               │    UART CONTROLLER      │
│  Silently copies bytes  │                               │  Detects 4-char IDLE    │
│  directly to DTCM/SRAM  │                               │  gap on the RX line     │
│  (0% CPU overhead)      │                               │  Asserts RTO Interrupt  │
└────────────┬────────────┘                               └────────────┬────────────┘
             │                                                         │
             │           ┌─────────────────────────────────────────────┘
             │           │
             ▼           ▼
┌───────────────────────────────────────────────────────────────────────────────────┐
│                           XUANTIE E907 RTO ISR (~25 ns)                           │
│  1. Reads DMA Remaining Byte Counter: Packet Size = (Buffer_Size - Remaining_Bytes)│
│  2. Resumes coroutine with pointer directly to data (ZERO CPU COPYING!)           │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### How the Mechanism Operates:
1. **Continuous Circular DMA:**
   * A DMA channel is armed in circular mode targeting an `etl::array<uint8_t, 512>` in DTCM.
   * Incoming bytes are transferred directly from `UART2_RBR` (`0x02500800`) to memory without CPU interrupts per byte.
2. **Hardware 4-Character Idle Detection (RTO):**
   * When FIFO is enabled (`FCR[0] = 1`) and RX interrupt is enabled (`IER[0] = 1`), the UART hardware monitors the RX line.
   * When a burst finishes and the line stays idle for **4 character times** (~35 µs @ 115200 baud, ~10 µs @ 420000 baud), the UART asserts the **RTO Interrupt** (`IIR = 0x0C`).
3. **Zero-Copy Length Calculation:**
   * In the RTO ISR, the CPU does **not** read bytes from the FIFO.
   * It queries the DMA controller's **Current Byte Counter (`DMA_BCR_REG`)**:
     $$\text{Packet Length} = \text{Total DMA Buffer Size} - \text{DMA Remaining Count}$$
   * The ISR creates an `etl::span<const uint8_t>` pointing directly to the received packet slice and immediately resumes the awaiting coroutine!

---

## 4. Hardware Doorbell & Inter-Core Synchronization (MSGBOX)

* **Mailbox Controller (`0x03003000` / `0x40030000`):**
  * **Channel 0 (RISC-V -> Linux):** Written by RISC-V after pushing 128-byte packets to shared SRAM C. Asserts GIC IRQ 147 on ARM Cortex-A55.
  * **Channel 1 (Linux -> RISC-V):** Written by Linux after dispatching PCIe TLPs. Asserts PLIC IRQ 25 on XuanTie E907.
* **Memory Fences:**
  * Every queue update executes an explicit RISC-V memory barrier:
    ```cpp
    __asm__ volatile("fence rw, rw" ::: "memory");
    ```
    guaranteeing complete store ordering across the on-chip AXI interconnect before the doorbell interrupt is triggered.

---

## 5. PLIC Interrupt Vector Mapping (XuanTie E907)

| PLIC IRQ | Peripheral | Trigger Condition | HAL Action |
| :--- | :--- | :--- | :--- |
| **IRQ 8** | `UART0` | TX Empty / RX Available | Debug console I/O |
| **IRQ 10** | `UART2` | Receiver Timeout (RTO) / TX FIFO Empty | Harvests DMA packet / wakes serial coroutine |
| **IRQ 15** | `SPI0` | Transfer Complete (`TC_INT`) | Deasserts CS / wakes SPI awaiter |
| **IRQ 25** | `MSGBOX` | Message Received (Channel 1) | Wakes IPC command task |
| **PIO IRQ** | `Main PIO` | Edge on Pin 22 (FPGA Ready) or Pin 29 (IMU DRDY) | Triggers instant SPI burst |
