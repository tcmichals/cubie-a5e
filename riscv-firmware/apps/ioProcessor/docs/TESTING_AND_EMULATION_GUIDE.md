# Automated Testing, Mock HAL & QEMU Emulation Guide

This document explains the testing strategy, mock hardware architecture, and QEMU emulation workflow for the **XuanTie E907 Hardware I/O Co-Processor (`ioProcessor`)** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**.

---

## 1. Testing Philosophy: Native Host vs. QEMU vs. Physical Board

Developing bare-metal firmware for an asymmetric multi-core SoC requires different testing levels:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ TIER 1: Native Host Unit Tests (CppUTest + Mock HAL)                        │
│ - Speed: Instant (< 1 millisecond execution)                                │
│ - Debuggers: Native GDB, AddressSanitizer (ASan), Valgrind                  │
│ - Validates: Coroutine state machines, SPSC queues, PCIe TLP serialization  │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ TIER 2: QEMU Emulation (qemu-riscv32-static)                                │
│ - Speed: Fast (~10–50 milliseconds)                                         │
│ - Validates: RV32IMAC compiler code generation, ILP32 ABI alignment,        │
│              and detection of unaligned memory traps on XuanTie E907        │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ TIER 3: Physical Hardware Bring-Up (tools/test_riscv.py & OpenOCD)          │
│ - Speed: Real-time 600 MHz RISC-V core                                      │
│ - Validates: Real Allwinner silicon MMIO, DMA transfers, physical Dual-SPI  │
│              signals to FPGA (Pins 19, 21, 23, 24), and UART2 (PB0/PB1)     │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Why We Need Mock HAL (Does QEMU Have Allwinner SPI & MSGBOX?)

### The Limitation of Standard QEMU
Standard QEMU (`qemu-system-riscv32 -M virt` or `qemu-riscv32-static`):
*  Emulates the standard RISC-V CPU core instructions (`rv32imac`), SiFive PLIC, and standard 16550 UART.
* ❌ **Does NOT emulate Allwinner's proprietary hardware blocks:**
  * No Allwinner `0x04025000` Dual-SPI controller (no `SPI_TCR`, `SPI_MBC`, `SPI_BCC` registers).
  * No Allwinner `0x03003000` MSGBOX hardware doorbell FIFO.
  * No Allwinner `0x02000000` PIO Port C/Port B pinmux registers.

If bare-metal firmware attempts to read or write `0x04025000` directly inside standard QEMU, the emulator will trigger a memory bus fault because those memory-mapped I/O (MMIO) addresses are not modeled.

---

## 3. How the Mock HAL Works (`mock_hal.cpp`)

To achieve **100% test coverage without hardware dependencies**, we decoupled the hardware driver implementations into a clean Hardware Abstraction Layer (**HAL**):

```
                       AUTOMATED TEST (CppUTest / QEMU)
                                      │
              ┌───────────────────────┴───────────────────────┐
              │                                               │
              ▼                                               ▼
   ┌──────────────────────┐                       ┌──────────────────────┐
   │    SPI0 MOCK BUS     │                       │    DOORBELL MOCK     │
   │  - Ingests PCIe TLP  │                       │  - Sets host_rang=1  │
   │  - Checks CRC/fields │                       │  - Asserts PLIC IRQ  │
   │  - Injects FPGA CplD │                       │  - Verifies wakeup   │
   └──────────────────────┘                       └──────────────────────┘
              ▲                                               ▲
              └───────────────────────┬───────────────────────┘
                                      │
                                      ▼
                      COROUTINE SCHEDULER & HARDWARE AWAITERS
                     (Validates full async state transitions)
```

### 1. Mock Hardware Mailbox (`MsgBox`)
* **Doorbell Notification:** When the firmware pushes a packet to shared SRAM C and calls `MsgBox::notify_host(0x01)`, the mock verifies that the memory barrier (`fence rw, rw` / `dmb ish`) executed before the doorbell is triggered.
* **Host Inter-Core Interrupt Simulation:** The test can call `msip_dispatch_events()` or mock a Linux doorbell trigger to verify that awaiting coroutines wake up immediately.

### 2. Mock Dual-SPI0 (`Spi0`)
* **Outgoing PCIe TLP Verification:** Intercepts `Spi0::async_transceive_fpga_dual(tx, rx, len)`. Validates that the 3DW header (`tlp_fmt_type = 0x40`, `length_dw`, `address_lo`) matches the PCIe specification.
* **Incoming FPGA Completion Injection:** The mock writes an FPGA Completion with Data (`CplD` `0x4A`) directly into the `rx` buffer and fires the completion callback, simulating a real FPGA response in 0 CPU cycles.

### 3. Mock High-Speed UART2 (`Uart2`)
* **Zero-Copy RX DMA & RTO Simulation:** Injects simulated byte streams (e.g. 24B CRSF frames or 48B NMEA GPS strings) into the receive buffer.
* Simulates the **4-character idle line timeout (RTO)** to verify that variable-length frames are harvested immediately without waiting for the buffer to fill.

### 4. Mock High-Resolution Timer (`Timer`)
* Maps `get_cycles()` and `get_time_ns()` to the host operating system's `std::chrono::steady_clock`.
* Validates that `co_await sleep_us(1000)` properly yields control back to the AbstractX scheduler and resumes on time.

---

## 4. Test Suites in `riscv-firmware/apps/ioProcessor/tests/`

| Test File | Test Group | Description |
| :--- | :--- | :--- |
| **`test_ringbuffer.cpp`** | `SpscRingBufferTest` | 100-round wrap-around stress test of SPSC queue, memory barriers, packet drop detection, and 128B alignment. |
| **`test_pcie_tlp.cpp`** | `PcieTlpEncodingTest` | Memory Write (`MWr32`) and Completion (`CplD`) 3DW/4DW layout verification. |
| **`test_coroutine_scheduler.cpp`** | `AbstractXSchedulerTest` | C++20 stackless coroutine task spawning, eager execution, `co_await` yielding, and `MAX_TASKS = 16` static pool capacity enforcement. |

---

## 5. How to Build & Run Tests

### Step 1: Run Native Host Tests (Recommended)
```bash
cd riscv-firmware/apps/ioProcessor/tests
mkdir -p build && cd build

cmake ..
make -j$(nproc)

# Run test runner:
./ioprocessor_tests -v

# Or run via CTest:
ctest --verbose
```

**Expected Output:**
```text
TEST(AbstractXSchedulerTest, TaskCapacityLimits) - 0 ms
TEST(AbstractXSchedulerTest, TaskSpawningAndRunOnce) - 0 ms
TEST(PcieTlpEncodingTest, CompletionWithDataTlpLayout) - 0 ms
TEST(PcieTlpEncodingTest, MemoryWrite3DWTlpLayout) - 0 ms
TEST(SpscRingBufferTest, BufferWrapAroundIntegrity) - 0 ms
TEST(SpscRingBufferTest, PushAndPopSinglePacket) - 0 ms

OK (6 tests, 6 ran, 450 checks, 0 ignored, 0 filtered out, 0 ms)
```

### Step 2: Run under QEMU RISC-V Emulation
```bash
# Validate instruction set & ABI alignment:
qemu-riscv32-static ./ioprocessor_tests
```
