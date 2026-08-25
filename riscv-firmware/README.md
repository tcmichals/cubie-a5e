# RISC-V Co-Processor Firmware & Applications Guide

This repository contains bare-metal firmware, runtime drivers, and Linux host companion tools for the auxiliary **XuanTie E907 RISC-V Core (@ 600 MHz)** on the **Radxa Cubie A5E (Allwinner A527 / T527 / `sun55i`)**.

---

## 1. Directory Overview & Architecture Guides

```text
riscv-firmware/
├── common/             # Shared HAL drivers, startup.S, memory map, ETL, AbstractX, Pigweed, Barectf
│   └── hal/README.md   # [CRITICAL] High-Speed HAL, DMA & Non-Blocking Coroutine Architecture
├── apps/
│   ├── ioProcessor/    # PCIe TLP & Hardware I/O Co-Processor (Firmware + Linux io-bridge)
│   │   └── docs/       # Full architecture, IPC maps, trace and logging guides
│   └── legacy_demo/    # Standalone baseline demo (Makefile)
└── tools/              # Linux host ELF loader (riscv-load), remoteproc scripts, test utilities
```

### Core Architecture & Driver Documentation
* 🚀 **[High-Speed HAL, DMA & Asynchronous Coroutine Architecture](common/hal/README.md)**: Zero-polling philosophy, SPI0 Transfer-Complete interrupts, and **Zero-Copy UART RX DMA + Receiver Timeout (RTO)** for variable-length packet ingestion.
* 📦 **[ioProcessor PCIe TLP Architecture](apps/ioProcessor/docs/ARCHITECTURE.md)**: Hardware pinout (40-pin header), 3.3V Port C setup, and AbstractX coroutine engine.
* ⚡ **[128-Byte IPC Shared Memory Map](apps/ioProcessor/docs/IPC_MEMORY_MAP.md)**: SPSC ring buffers and packet formats in shared SRAM C (`0x07130000`).
* 📊 **[Barectf CTF Nanosecond Execution Tracing](apps/ioProcessor/docs/BARECTF_TRACE_GUIDE.md)**: Profiling with Babeltrace 2 & Trace Compass.
* 🪵 **[Google Pigweed Tokenized Logging](apps/ioProcessor/docs/PIGWEED_LOGGING.md)**: 4-byte compile-time tokenized logging.
* 🧪 **[CppUTest & QEMU Unit Testing Guide](apps/ioProcessor/docs/TESTING_AND_EMULATION_GUIDE.md)**: Automated CppUTest suite and QEMU test bench.

---

## 2. Prerequisites & Toolchains

### RISC-V Bare-Metal Cross-Compiler (for Firmware)
To build the RISC-V firmware, you need a 32-bit RISC-V GCC toolchain supporting `rv32imac` and `ilp32` ABI with C++20 support:

```bash
# Ubuntu / Debian
sudo apt-get install gcc-riscv64-unknown-elf picolibc-riscv64-unknown-elf
# or
sudo apt-get install gcc-riscv32-unknown-elf
```

### Host C++20 Compiler & CMake (for Linux Host Bridge)
```bash
sudo apt-get install cmake build-essential gdb-multiarch
```

---

## 3. How to Build `ioProcessor` (PCIe TLP & Hardware I/O)

The `ioProcessor` app contains both the **RISC-V Firmware** and the **Linux Host Bridge**.

### Option A: Build Everything at Once (Top-Level CMake)
```bash
cd riscv-firmware/apps/ioProcessor
mkdir -p build && cd build

# Configure with RISC-V cross-toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE=../firmware/cmake/riscv32-toolchain.cmake
make -j$(nproc)
```

### Option B: Build Only the RISC-V Firmware (`ioprocessor_firmware.elf`)
```bash
cd riscv-firmware/apps/ioProcessor/firmware
mkdir -p build && cd build

# 1. Debug Build (Default: -Og -g3 -ggdb with full DWARF symbols)
cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/riscv32-toolchain.cmake
make -j$(nproc)

# 2. Release Build (Production: -O3 -flto)
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/riscv32-toolchain.cmake
make -j$(nproc)
```
Output binaries generated in `build/`:
* `ioprocessor_firmware.elf` (ELF binary with debug symbols and `.resource_table`)
* `ioprocessor_firmware.bin` (Raw flat binary)
* `ioprocessor_firmware.map` (Memory map inspection)

### Option C: Build Only the Linux Host Companion (`io-bridge`)
```bash
cd riscv-firmware/apps/ioProcessor/linux
mkdir -p build && cd build

cmake ..
make -j$(nproc)
```
Output binary: `io-bridge`

---

## 4. How to Build via Buildroot (Automated System Image)

If you are building the entire Linux OS image for the Cubie A5E using Buildroot:

```bash
# In your buildroot output directory (e.g. bld.a5e/)
cd bld.a5e

# Rebuild and install riscv-firmware package into target rootfs:
make riscv-firmware-rebuild

# Build final sdcard.img:
make
```

Buildroot installs:
* Firmware ELF: `/lib/firmware/riscv-firmware.elf`
* Host loader: `/usr/bin/riscv-load`
* Host helper: `/usr/bin/load-riscv.sh`
* Test script: `/usr/bin/test_riscv.py`

---

## 5. How to Load and Run on the Cubie A5E Target

### Method 1: Mainline Linux `remoteproc` (Recommended)
```bash
# 1. Copy your firmware ELF to /lib/firmware/
sudo cp ioprocessor_firmware.elf /lib/firmware/riscv-firmware.elf

# 2. Start the core via remoteproc
echo "start" | sudo tee /sys/class/remoteproc/remoteproc0/state

# 3. Check kernel dmesg
dmesg | tail -n 20

# 4. Stop core if needed
echo "stop" | sudo tee /sys/class/remoteproc/remoteproc0/state
```

### Method 2: Direct Host Loader (`riscv-load`)
```bash
sudo riscv-load ioprocessor_firmware.elf
```

---

## 6. Running the Host Monitoring Bridge (`io-bridge`)

Once the RISC-V core is booted:
```bash
# Launch the host bridge daemon (requires sudo for /dev/mem shared SRAM access)
sudo ./io-bridge
```

Example Live Output:
```text
[IPC Bridge] Successfully mapped Shared SRAM C @ 0x7130000
[Main] Bridge active. Listening for telemetry, logs, and traces...
[0.002150 s] [RISC-V] XuanTie E907 Hardware I/O & PCIe TLP Processor Booting...
[0.002240 s] [RISC-V] AbstractX C++20 Coroutine Scheduler Initialized
[0.002310 s] [RISC-V] FPGA PCIe TLP Dual-SPI Bridge Task Initialized
[0.002380 s] [RISC-V] IMU Sensor Acquisition Task Initialized
[0.002450 s] [RISC-V] UART2 Serial Stream Ingestion Task Initialized
[0.002520 s] [RISC-V] Entering Hard Real-Time I/O Event Loop
[I/O Throughput] PCIe TLPs: 500 pkts/s | IMU SPI: 1000 pkts/s | UART: 50 pkts/s
```

---

## 7. Interactive GDB Debugging with OpenOCD

Because the XuanTie E907 Debug Module is mapped over internal MMIO (`0x07090000`), OpenOCD runs directly on the Cubie A5E:

```bash
# 1. Start OpenOCD on the board (listening on :3333)
openocd -f /usr/share/openocd/scripts/target/allwinner_e907.cfg

# 2. In a second terminal or from your workstation, run the debug script:
cd riscv-firmware/apps/ioProcessor/firmware
./debug.sh
```
`debug.sh` launches GDB, connects to `localhost:3333`, loads `ioprocessor_firmware.elf` symbols, and stops at `_start` / `main`.
