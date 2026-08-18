# 🐛 Flight Stack Debugging Log & Case Studies

This document serves as a living history of the complex embedded systems bugs we encounter while building the Radxa Cubie A5E flight stack. By logging our experiences, symptoms, debugging methodologies, and final fixes, we create a knowledge base for future developers.

---

## Case Study 1: The `ttyS0` Serial Hijack (Kernel Console Disconnect)
**Date:** July 12, 2026  
**Component:** Linux Device Tree Overlays & 8250 Serial Driver  

### 🚨 Symptoms
The system booted successfully through U-Boot and began the Linux kernel boot sequence over the serial debug cable (connected to physical pins 8 & 10, which route to `UART0`). 
Everything looked perfect until the kernel hit this specific line in `dmesg`:
```text
[    1.577126] printk: legacy console [ttyS0] enabled
[    1.577138] printk: legacy bootconsole [uart0] disabled
```
At that exact moment, all output on the serial terminal stopped permanently. The board did not crash or panic; the login prompt was simply never printed to the screen.

### 🔍 Debugging & Investigation
1. **Initial Assumption:** We assumed the kernel had panicked or hung during the transition from the `earlycon` bootloader console to the real `ttyS0` serial driver.
2. **Reading the Tea Leaves:** We looked closer at the kernel log right before the freeze:
   ```text
   [    1.061080] 2500800.serial: ttyS0 at MMIO 0x2500800 (irq = 15, base_baud = 1500000) is a 16550A
   ```
3. **The 'Aha!' Moment:** The memory map for the Allwinner A523 SoC dictates that `UART0` is at `0x02500000`. But the driver successfully claimed `ttyS0` for MMIO `0x02500800`... which is `UART2`! 
4. **Root Cause:** 
   In our custom flight stack device tree overlay (`cubie-a5e-flight-stack.dtso`), we had enabled `&uart2` to prepare for a future GPS module.
   However, we forgot to explicitly alias the UARTs to specific `ttyS` numbers. 
   When the Linux 8250 driver initialized, it probed `UART2` first. Because it lacked a strict alias, the driver dynamically assigned it the lowest available name: `ttyS0`.
   U-Boot was passing `console=ttyS0` to the kernel. So, the kernel dutifully moved the system console to `ttyS0` (UART2) and began sending the login prompt out of the GPS pins instead of the debug cable!

### 🛠️ The Fix
We modified the overlay to explicitly map the serial ports in a root `&{/aliases}` block using absolute string paths. This forces the Linux driver to assign the correct `ttyS` index, preventing dynamic renaming.

*Incorrect Attempt (Phandles are invalid for aliases in overlays):*
```dts
aliases {
    serial0 = &uart0; /* ERROR: dtc cannot parse phandles in aliases */
};
```

*Correct Fix (Absolute String Paths):*
```dts
&{/aliases} {
    serial0 = "/soc/serial@2500000"; /* Force UART0 to ttyS0 */
    serial2 = "/soc/serial@2500800"; /* Force UART2 to ttyS2 */
};
```
We also temporarily set `&uart2 { status = "disabled"; };` until the physical GPS module is actually wired up, ensuring a clean debug environment.

---

## Case Study 2: C++ Migration & Realtime IPC
**Date:** July 12, 2026
**Component:** RISC-V Firmware & ARM Linux Host `rbb-server`

### 🏗️ Architectural Evolution
As the flight stack matured, we identified two major bottlenecks in our original C-based architecture:
1. **Host CPU Burn:** The ARM Linux host was using a spinning `while(1)` loop to poll `/dev/mem` for new ring buffer data, burning 100% of a CPU core.
2. **Unsafe Bare-Metal Macros:** The RISC-V firmware relied on raw `#define` C macros for memory-mapped I/O, lacking type safety and auto-completion.
3. **Silent Linker Overflows:** If firmware grew past 64KB, it would silently corrupt adjacent memory.

### 🛠️ The Fixes
We executed a complete C++ migration across both processors:

**RISC-V Co-processor (Zero-Cost Abstractions):**
- Converted the firmware to C++ using `riscv-none-elf-g++` but with `-fno-exceptions -fno-rtti` to entirely strip standard library bloat.
- Replaced the C mailbox macros with a zero-cost `volatile struct` and `constexpr` C++ class (`hardware::Mailbox`). This compiles down to the exact same 1-cycle assembly instruction as the raw macros but guarantees strict type safety.
- Added strict `ASSERT` rules inside `firmware.ld` to ensure the build explicitly fails if `.vectors` or `.bss` exceed the 64KB ITCM/DTCM bounds.

**ARM Host (POSIX Real-time Threads):**
- Rewrote `rbb-server` in C++20 using `std::jthread`.
- Extracted the underlying `native_handle()` to elevate the worker thread to `SCHED_FIFO` (a POSIX realtime scheduler policy).
- Instead of spinning, the real-time thread now blocks on a `read()` from a UIO device node (`/dev/uio0`), which is tied directly to the Mailbox hardware interrupt doorbell.
- Called `mlockall(MCL_CURRENT | MCL_FUTURE)` on startup to lock the daemon's memory into RAM, completely eliminating page faults and swap latency (this is the exact technique used by ArduPilot/ArduCopter for deterministic flight loops).
- Now, the ARM CPU sleeps at 0% usage. The moment the RISC-V pushes a packet and rings the doorbell, the kernel instantly wakes our `SCHED_FIFO` thread with extreme priority to drain the `/dev/mem` SPSC ring buffer.

### 🛡️ Hard Realtime OS Isolation (The "ArduPilot" Strategy)
Simply elevating a POSIX thread to `SCHED_FIFO` is not enough for true hard real-time performance on Linux, because the OS scheduler can still interrupt the thread to service background tasks, network packets, or tick-timers. To achieve deterministic microsecond latency for iNav, we implemented a full isolation strategy:
1. **Kernel Boot Isolation:** U-Boot passes `isolcpus=7 nohz_full=7 rcu_nocbs=7` to the Linux kernel. This completely walls off CPU Core 7. The Linux scheduler is forbidden from assigning normal tasks to it, the tick-timer is disabled, and RCU callbacks are stripped. Core 7 does nothing but wait.
2. **Memory Lockdown:** `rbb-server` calls `mlockall(MCL_CURRENT | MCL_FUTURE)` on startup. This locks the daemon's memory footprint strictly into physical RAM, entirely eliminating the possibility of a page-fault or disk swap latency spike.
3. **Thread Affinity:** As soon as the `std::jthread` ISR worker spawns, it calls `pthread_setaffinity_np()` to explicitly pin itself to the isolated CPU 7. 
4. **The Result:** CPU 7 runs exactly one thread (`rbb-server`). When the UIO Mailbox doorbell fires, the CPU wakes up and processes the lock-free `/dev/mem` ringbuffer without any possibility of being preempted by the Linux OS.

---

## Case Study 3: Wi-Fi Kernel Module Version Mismatch & WPA Supplicant
**Date:** July 12, 2026
**Component:** AIC8800 Wi-Fi Driver & Buildroot

### 🚨 Symptoms
Upon booting the flight controller and attempting to bring up the AIC8800 Wi-Fi interface, `dmesg` spit out the following fatal errors and the wireless interface (`wlan0`) never appeared:
```text
[    7.479554] module aic_load_fw: .gnu.linkonce.this_module section size must match the kernel's built struct module size at run time
Successfully initialized wpa_supplicant
Line 1: unknown global field 'ctrl_interface=/var/run/wpa_supplicant'.
Failed to read or parse configuration '/etc/wpa_supplicant.conf'.
```

### 🔍 Debugging & Investigation
There were two distinct issues happening simultaneously:

1. **The Kernel Module Mismatch (`section size must match...`):**
   This is a classic "out-of-tree module" error. We had recently rebuilt the Linux kernel (likely when modifying the device tree overlay for the UARTs or enabling RT patches). When the kernel configuration changes, the byte-size of internal C structures (like the `module` struct) can shift. 
   Buildroot correctly compiled the *new* kernel (`vmlinux`), but because the source code for the `aic8800` Wi-Fi driver package had not changed, Buildroot skipped recompiling it. It simply copied the *old* `aic8800_fdrv.ko` binary into the new root filesystem.
   At boot, the new kernel checked the signature (`modversions`/`vermagic`) of the old Wi-Fi driver, noticed the struct size mismatch, and safely aborted loading to prevent a kernel panic or memory corruption.

2. **The `wpa_supplicant` Syntax Error:**
   The error `unknown global field 'ctrl_interface=/var/run/wpa_supplicant'` indicated a parsing failure on line 1 of our configuration file. This is usually caused by either a missing `DIR=` directive or a hidden Windows CRLF (`\r\n`) carriage return character breaking the Linux parser.

### 🛠️ The Fixes
1. **Forcing a Wi-Fi Driver Rebuild:**
   We manually wiped the cached build artifacts for the Wi-Fi driver and forced a fresh compilation against the *new* kernel headers:
   ```bash
   make aic8800-driver-dirclean
   make
   ```
   This perfectly realigned the memory structs.

2. **Fixing the Config Syntax:**
   We updated `/etc/wpa_supplicant.conf` via the Buildroot rootfs-overlay to use the strict `DIR=` syntax, ensuring the parser wouldn't fail:
   ```text
   ctrl_interface=DIR=/var/run/wpa_supplicant
   update_config=1
   country=US
   ```

After reflashing the newly assembled `sdcard.img`, the AIC8800 driver loaded perfectly and `wpa_supplicant` successfully established the Wi-Fi link for telemetry!

---

## Case Study 4: Migrating to Upstream Shenmintao AIC8800 Driver & Build System Cleanup
**Date:** July 14, 2026
**Component:** AIC8800 Wi-Fi Driver Build Configuration

### 🚨 The Goal & Symptoms
The previous vendor-provided Radxa AIC8800 driver was messy, requiring extensive patching and hardcoded `sed` script replacements in Buildroot just to force SDIO support (e.g., rewriting `CONFIG_SDIO_SUPPORT=y` in the driver's Makefile during the `POST_PATCH_HOOK`). 
We wanted a clean, modern Linux driver configuration capable of targeting either the SDIO or USB bus seamlessly from Buildroot's `menuconfig` without hacking the source code on the fly. The ultimate goal was to produce a lean, upstreamable patch for the `shenmintao` repository.

### 🛠️ The Fixes & Upstream Patch
We transitioned the Buildroot package to pull from the cleaner `shenmintao` tree and fundamentally fixed how the configuration is passed.

1. **Buildroot `Config.in` & `.mk` Updates:**
   - We removed the mutually exclusive hardcoding and added independent booleans `BR2_PACKAGE_AIC8800_DRIVER_SDIO` and `BR2_PACKAGE_AIC8800_DRIVER_USB`.
   - Updated the `.mk` file to dynamically append `CONFIG_SDIO_SUPPORT=y/n` and `CONFIG_USB_SUPPORT=y/n` to the `make` command-line options based on Kconfig selections, rather than relying on `sed` hooks.
   - Removed legacy `sed` hacks (like deleting `aic_priv_cmd.o`, which didn't even exist in the upstream tree).

2. **The Upstream Patch (`0005-clean-build-config.patch`):**
   - We authored a clean patch against the driver's `drivers/aic8800/aic8800_fdrv/Makefile`.
   - We replaced the hardcoded `CONFIG_SDIO_SUPPORT =y` and `CONFIG_USB_SUPPORT =y` assignments with `?=` (conditional assignment).
   - This modern Linux driver practice allows the external environment (like Buildroot or standard Kernel Kbuild) to define the bus configuration, keeping the Makefile clean and flexible.
   - This patch is now queued for a Pull Request to the upstream `shenmintao` repository.

---

## Case Study 5: Unified Dual-Bus Driver (SDIO + USB) for Cubie A5E & A7A on Linux 7.1
**Date:** August 18, 2026  
**Component:** AIC8800 Upstream Wireless Driver (`aic8800-upstream`)

### 🚨 The Goal & Symptoms
The Radxa Cubie A5E (Allwinner A527/T527) operates over **SDIO**, whereas the Radxa Cubie A7A (Allwinner A733) operates over **USB** (`0xA69C:0x8800`). Previous vendor codebases maintained separate, broken out-of-tree repositories or relied on tangled `#ifdef` chains that broke modern kernel builds on Linux 7.1 `PREEMPT_RT`. Furthermore, the upstream `wireless-next` RFC v2 patch submission was strictly SDIO-only.

### 🛠️ The Architectural Solution & Fixes
1. **Clean USB Transport Integration**:
   - Ported and modernised `aicwf_usb.c`, `aicwf_usb.h`, `usb_host.c`, and `usb_host.h` into `aic8800-upstream/aic8800_fdrv/`.
   - Replaced legacy APIs with standard Linux `usbcore` registration, asynchronous URB anchors, dynamic skb allocations, and modern timer APIs.
2. **Bus-Agnostic Core Abstraction**:
   - Added helper functions `rwnx_platform_get_dev()` and `rwnx_platform_get_hw()` in `rwnx_platform.c` so higher-level MAC layers (`rwnx_tx.c`, `rwnx_txq.c`, `rwnx_main.c`, `aic_priv_cmd.c`) access hardware state independently of transport type.
   - Unified CRC-8 calculations in `aicwf_chip_ops.c` and guarded SDIO-specific structures (`aic8800_bsp.ko`, `rx_frame_queue`) so USB mode builds a single, standalone `aic8800_fdrv.ko` module.
3. **Buildroot Multi-Target Support**:
   - `bld.a5e` builds `aic8800_bsp.ko` + `aic8800_fdrv.ko` for SDIO.
   - `bld.a7a` builds standalone `aic8800_fdrv.ko` for USB.
   - Both build warning-free against Linux 7.1 and generate full `sdcard.img` target artifacts.

---
