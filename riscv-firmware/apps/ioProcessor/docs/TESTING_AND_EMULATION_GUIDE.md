# Automated Unit Testing & Emulation Guide (CppUTest & QEMU)

This guide documents the automated testing, CppUTest test runner, and QEMU emulation test bench for the **XuanTie E907 Hardware I/O Co-Processor (`ioProcessor`)**.

---

## 1. Overview & Architecture

The test suite is located under **`riscv-firmware/apps/ioProcessor/tests/`** and runs both **natively on the developer workstation** and inside **QEMU emulation** (`qemu-riscv32-static`).

```text
riscv-firmware/apps/ioProcessor/tests/
├── CMakeLists.txt                  # CppUTest CMake build configuration & CTest integration
├── test_main.cpp                   # CppUTest runner entry point
├── mock_hal.cpp                    # Mock HAL hardware drivers (MSGBOX, Timer, PIO)
├── test_ringbuffer.cpp             # SPSC lock-free shared SRAM ring buffer tests (100 rounds)
├── test_pcie_tlp.cpp               # PCIe TLP packet structure & 128-byte payload tests
└── test_coroutine_scheduler.cpp    # AbstractX C++20 coroutine scheduler & ETL capacity tests
```

---

## 2. Test Suites & Coverage

### 1. `SpscRingBufferTest` (`test_ringbuffer.cpp`)
* **Push and Pop Integrity:** Verifies 128-byte packet packing, magic header (`0x544C5049`), sequence monotonicity, and payload byte preservation.
* **Wrap-Around Stress Test:** Executes 100 sequential push/pop rounds forcing ring buffer `head` and `tail` index wrap-arounds at the 120-slot boundary with zero memory leaks.

### 2. `PcieTlpEncodingTest` (`test_pcie_tlp.cpp`)
* **Memory Write 32-bit (MWr32):** Validates 3DW header packing, 4 DWord payload alignment, traffic class, and byte enable masks.
* **Completion with Data (CplD):** Tests PCIe response packet generation and multi-dword byte alignment.

### 3. `AbstractXSchedulerTest` (`test_coroutine_scheduler.cpp`)
* **Task Spawning & Yielding:** Tests eager initialization up to suspension awaiter (`co_await MockYieldAwaiter`), and ensures `run_once()` advances coroutine execution state.
* **Static Arena & Zero-Heap Capacity:** Verifies that registering up to `MAX_TASKS` (16 tasks) succeeds inside `StaticCoroutinePool`, while the 17th task gracefully rejects without heap allocation.

---

## 3. How to Build and Run Tests

### Running Natively on Linux Workstation
```bash
cd riscv-firmware/apps/ioProcessor/tests
mkdir -p build && cd build

cmake ..
make -j$(nproc)

# Run test runner with verbose output:
./ioprocessor_tests -v

# Or run via CTest:
ctest --verbose
```

### Example Test Runner Output:
```text
TEST(AbstractXSchedulerTest, TaskCapacityLimits) - 0 ms
TEST(AbstractXSchedulerTest, TaskSpawningAndRunOnce) - 0 ms
TEST(PcieTlpEncodingTest, CompletionWithDataTlpLayout) - 0 ms
TEST(PcieTlpEncodingTest, MemoryWrite3DWTlpLayout) - 0 ms
TEST(SpscRingBufferTest, BufferWrapAroundIntegrity) - 0 ms
TEST(SpscRingBufferTest, PushAndPopSinglePacket) - 0 ms

OK (6 tests, 6 ran, 450 checks, 0 ignored, 0 filtered out, 0 ms)
```

---

## 4. Running under QEMU RISC-V Emulation

To test bare-metal RISC-V binaries on your host PC:

```bash
# User-mode RISC-V 32-bit emulation:
qemu-riscv32-static ./ioprocessor_tests

# Full system emulation (optional):
qemu-system-riscv32 -M virt -nographic -bios none -kernel ioprocessor_firmware.elf
```
