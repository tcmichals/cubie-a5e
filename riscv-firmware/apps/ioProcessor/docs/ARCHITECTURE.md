# XuanTie E907 Hardware I/O & PCIe TLP Co-Processor Architecture

This project implements a dedicated, high-speed **Hardware I/O Co-Processor** running on the auxiliary **XuanTie E907 RISC-V core (@ 600 MHz)** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ARM64 Linux Host (Cortex-A55 Cores)                      │
│  - PCIe Host Subsystem / Device Drivers / Userspace Applications            │
│  - Real-time Pigweed log detokenization (io-bridge)                         │
│  - Ingests Barectf Common Trace Format (CTF) binary streams to disk         │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ 128-Byte SPSC Queues in Shared SRAM C
                                       │ Hardware Doorbell: MSGBOX (0x03003000)
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                XuanTie E907 RISC-V Hardware I/O Processor (@ 600 MHz)       │
│  - AbstractX C++20 stackless coroutine scheduler (Static DTCM pool, ETL)    │
│  - High-Speed Dual-SPI0 PCIe TLP Bridge <-> FPGA (CS0 Pin 24, MOSI/MISO)    │
│  - 1 kHz Raw IMU Sensor Acquisition (Single-SPI0 CS1 Pin 26)                │
│  - High-Speed UART2 Serial Ingestion (Pins 11 & 13) with RTO Idle Timeout   │
│  - Hardware Machine Software Interrupt (MSIP) asynchronous wakeups          │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Hardware Pinout on 40-Pin Header

| Interface | Header Pins | SoC Pin Name | Logic Level | Target |
| :--- | :--- | :--- | :--- | :--- |
| **UART2-TX** | **Pin 11** | `PB0` | 3.3V | RISC-V Dedicated Serial Stream (UART2) |
| **UART2-RX** | **Pin 13** | `PB1` | 3.3V | RISC-V Dedicated Serial Stream (UART2) |
| **SPI0-MOSI / IO0** | **Pin 19** | `PC2` | 3.3V | RISC-V Dual-IO Data Lane 0 (PCIe TLP) |
| **SPI0-MISO / IO1** | **Pin 21** | `PC4` | 3.3V | RISC-V Dual-IO Data Lane 1 (PCIe TLP) |
| **SPI0-CLK** | **Pin 23** | `PC12` | 3.3V | RISC-V SPI Clock |
| **SPI0-CS0** | **Pin 24** | `PC3` | 3.3V | RISC-V FPGA Chip Select (Dual Mode) |
| **SPI0-CS1** | **Pin 26** | `PC7` | 3.3V | RISC-V IMU Chip Select (Single Mode)|
| **FPGA ISR** | **Pin 22** | GPIO | 3.3V | RISC-V Frame Ready Edge Interrupt |
| **IMU DRDY ISR** | **Pin 29** | GPIO | 3.3V | RISC-V Data Ready Edge Interrupt |

---

## High-Speed HAL & DMA Architecture Reference

For detailed hardware timing diagrams, register offsets, and step-by-step guides on asynchronous zero-polling drivers:
* 🚀 **[Comprehensive HAL, DMA & Asynchronous Driver Guide](../../../common/hal/README.md)**:
  * Zero-polling AbstractX coroutine awaiters (`co_await`).
  * Asynchronous Dual-SPI0 Transfer Complete (`TC_INT`) interrupt driver.
  * **Zero-Copy UART RX DMA + Receiver Timeout (RTO)** for variable-length serial packets.
  * Hardware MSGBOX doorbell memory barriers.

---

## Embedded C++ Standards: ETL (Embedded Template Library)

* **Repository:** [ETLCPP/etl](https://github.com/ETLCPP/etl) (`third_party/etl`)
* **Submodule Command:** `git submodule add https://github.com/ETLCPP/etl.git third_party/etl`
* **STL Ban:** Standard Template Library dynamic containers (`std::vector`, `std::queue`, `std::map`, `std::string`, `std::list`) are strictly banned from the RISC-V firmware to eliminate heap fragmentation and out-of-memory crashes.
* **Mandatory ETL:** All container data structures must use **ETL (`etl::*`)**:
  * `etl::vector<T, N>` (Fixed-capacity zero-heap vector)
  * `etl::circular_buffer<T, N>` / `etl::queue<T, N>`
  * `etl::array<T, N>`
  * `etl::span<T>`
  * `etl::string<N>`
  * `etl::flat_map<Key, Value, N>` / `etl::flat_set<T, N>`

---

## Build Types & Interactive OpenOCD / GDB Debugging

### 1. Build Types in CMake
The firmware build system defaults to **`Debug`**:

| Build Type | Compiler Flags | Optimization | Symbol Table | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **`Debug` (Default)** | `-Og -g3 -ggdb -DDEBUG=1 -fno-omit-frame-pointer` | Coroutine-friendly | Full DWARF-5 + Macros | OpenOCD + GDB live stepping |
| **`Release`** | `-O3 -DNDEBUG=1 -flto` | Maximum speed | Stripped | Production deployment |
| **`RelWithDebInfo`** | `-O2 -g3 -DNDEBUG=1` | High speed | Full DWARF symbols | Profiled deployment |

```bash
# Default Debug Build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv32-toolchain.cmake
make -C build

# Switch to Release Build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/riscv32-toolchain.cmake
make -C build
```

### 2. Interactive GDB Debugging over OpenOCD
Because the XuanTie E907 Debug Module is mapped over internal MMIO (`0x07090000`), OpenOCD runs natively on the Cubie A5E listening on port `:3333`.

```bash
# Connect GDB directly to OpenOCD
./debug.sh
```



