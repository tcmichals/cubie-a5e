# Next Session Priority: Wi-Fi Driver SDIO Merge (Radxa to GitHub)

The newer GitHub driver (`shenmintao/aic8800d80`) now **compiles successfully** against Linux 7.1 with both SDIO and USB support enabled, thanks to our recent 130-line API patch. The USB interface is fully functional. 

**However, the SDIO implementation in the GitHub codebase does not work on hardware.**

## The Goal: Porting Radxa SDIO Logic
The older Radxa DKMS version of the driver has a known working SDIO implementation. 
We need to merge the SDIO logic from the Radxa version into the newer GitHub version.

### The Root Cause & Fix (Completed)
We discovered that the Linux MMC core was completely ignoring the SDIO Wi-Fi card because the Github driver incorrectly assumed the hardware would report its internal class as `SDIO_CLASS_WLAN` (0x07). Cheap combo chips often report a class of `0x00` (None). Because the card's class didn't match the generic WLAN class in the driver, the Linux MMC core silently ignored it, and `aicwf_sdio_probe` was never called (resulting in `register_driver timeout`).

We ported the explicit Vendor ID and Device ID table (e.g., `SDIO_VENDOR_ID_AIC8800D80 0xc8a1`) from the Radxa DKMS driver into the Github driver's `aicwf_sdmmc_ids` array. This explicitly forces the MMC core to bind the driver to the card regardless of what class it reports.

**Next Steps for Hardware Test:**
1. Flash the updated image to an SD card: `sudo dd if=bld/images/sdcard.img of=/dev/sdX bs=4M status=progress`
2. Boot the Cubie A5E board.
3. Verify that the MMC core enumerates the card and calls our probe by running: `dmesg | grep -iE 'aic|mmc1'` (Look for `AIC8800: aicwf_sdio_probe called`).
## Patch Details (130 lines)
- `aicwf_sdio.c`: `del_timer` → `timer_delete`, `del_timer_sync` → `timer_delete_sync`, rxq pointer casts
- `aicwf_txrxif.c`: Moved `aicwf_another_ptk()` out of `#ifdef` so SDIO can use it, rxq pointer casts
- `rwnx_cmds.c`: Fixed dual-bus `rwnx_hw` variable redefinition
- `rwnx_main.c`: Stubbed `android_priv_cmd()` → `-EOPNOTSUPP`
- `aic8800_fdrv/Makefile`: Removed `aic_priv_cmd.o` (Android-only)

---

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
