# RISC-V Firmware (E907) Future Enhancements & TODOs

This document tracks upcoming architectural improvements for the bare-metal XuanTie E907 flight controller firmware.

## 1. Hardware Watchdog Timer (WDT) Failsafe
* **Goal:** Prevent drone crashes if the RISC-V firmware hard-faults or enters an infinite loop.
* **Implementation:** Initialize the XuanTie Hardware Watchdog Timer. The `main.cpp` flight loop must "kick the dog" (reset the timer) every 2ms. If the firmware freezes, the hardware will fire a Non-Maskable Interrupt (NMI). 
* **NMI Handler:** The NMI handler must instantly bypass all logic and send a "KILL MOTORS" or "AUTO-LEVEL" failsafe command over the SPI bus to the FPGA before the drone crashes.

## 2. True C++11 `<atomic>` Lock-Free Queue
* **Goal:** Optimize memory barriers and remove legacy C code.
* **Implementation:** Rename `ringbuffer.c` to `ringbuffer.cpp`. Replace the `volatile uint32_t head/tail` pointers and heavy `__sync_synchronize()` barriers with `std::atomic<uint32_t>`.
* **Benefit:** Using `std::memory_order_acquire` and `std::memory_order_release` will generate the absolute tightest 1-way barrier assembly for the SPSC queue, rather than the heavy 2-way C barriers.

## 3. SPI DMA (Direct Memory Access) Offloading
* **Goal:** Recover wasted CPU cycles currently spent polling SPI FIFOs.
* **Implementation:** Configure the Allwinner DMA Controller to autonomously stream the Dual-SPI IMU data directly from the FPGA into the SRAM C buffers.
* **Benefit:** The 600MHz CPU can execute math-heavy operations (Kalman filters, quaternions) concurrently while the DMA moves data. The DMA will fire a PLIC interrupt when the transfer completes.

## 4. Zero-Cost C++ Abstractions for SPI
* **Goal:** Eliminate unsafe C `#define` macros for memory-mapped I/O (similar to the `mailbox.hpp` upgrade).
* **Implementation:** Create an `spi.hpp` header containing a packed `volatile struct` mirroring the Allwinner SPI controller registers.
* **Benefit:** Provides strict type safety, namespace scoping, and prevents accidental bitwise errors on control registers, while maintaining zero overhead compilation.

## 5. DSP/Vector Extension Utilization
* **Goal:** Maximize floating-point math performance for the PID loops.
* **Implementation:** Ensure the GCC flags correctly leverage the XuanTie E907 DSP instructions (e.g., Multiply-Accumulate / MAC) for matrix operations.
