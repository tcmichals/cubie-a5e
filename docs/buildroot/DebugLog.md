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

## Case Study 6: Retiring Fragile Userspace `/dev/mem` Loader (`riscv-loader`) for Linux `remoteproc`
**Date:** August 19, 2026  
**Component:** XuanTie E907 Co-Processor Lifecycle Management & `sunxi_rproc.c`

### 🚨 The Problem & Circling Around `riscv-loader`
During early bring-up of the XuanTie E907 co-processor, we developed a userspace `/dev/mem` MMIO tool (`riscv-load` / `load-riscv.sh`) to poke CCU clock registers (`0x07010020` / `0x07010100`) and copy flat binary payloads (`firmware.bin`) directly to ITCM (`0x07110000`). This repeatedly stalled progress:
1. **Security & Kernel Restrictions**: Modern Linux 7.1 enforces `CONFIG_STRICT_DEVMEM` and `CONFIG_IO_STRICT_DEVMEM`. Direct userspace `mmap()` of physical memory was blocked or unstable without insecure bootargs (`iomem=relaxed`).
2. **Missing Section Mapping**: Dumping a raw `.bin` payload into `0x07110000` failed to handle multi-region memory layouts where `.text` belongs in ITCM (`0x00000000`), `.data`/`.bss` belongs in DTCM (`0x00080000`), and static pools reside in System SRAM C (`0x07130000`).
3. **Clock Tree Desynchronization**: Userspace `devmem` writes to CCU registers fought against the kernel Common Clock Framework (CCF), runtime PM, and suspend hooks, causing cores to silently drop into `HALTED` / `In reset` states without error messages.
4. **Tooling & Build Disconnects**: Buildroot package compilation of `riscv-load` frequently fell out of sync with rootfs overlays and shell script fallback paths.

### 🛠️ The Architectural Resolution: Full Standardisation on `remoteproc`
We officially retired the userspace `riscv-loader` approach in favor of the **Linux Mainline Remote Processor (`remoteproc`) Framework** via [`sunxi_rproc.c`](../ALLWINNER_RISCV_REMOTEPROC_GUIDE.md):

1. **Kernel-Level ELF Parsing & Memory Routing**:
   - `sunxi_rproc` natively parses standard `firmware.elf` binaries from `/lib/firmware/`.
   - ELF Program Headers (`paddr`) are automatically mapped via `da_to_va` handlers directly into ITCM (`0x07110000`), DTCM (`0x07120000`), and SRAM C (`0x07130000`), with zero-padding of `.bss`.
2. **Integrated CCF Clocks & Reset Handles**:
   - Clock gates and resets are acquired through `devm_clk_get()` and `devm_reset_control_get()`, ensuring proper parent clock enable sequencing and refcounting.
3. **Automatic Debugfs Trace Buffers**:
   - The `.resource_table` embedded in the ELF automatically generates `/sys/kernel/debug/remoteproc/remoteproc0/trace0`, enabling live firmware log streaming without extra UART lines.
4. **Standard Sysfs Lifecycle Interface**:
   ```bash
   echo "riscv-firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware
   echo start > /sys/class/remoteproc/remoteproc0/state
   cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
   ```
5. **Independent Debug Validation**:
   - Paired with our Python DMI test harness (`tools/dmi_test.py`), we use OpenOCD over JTAG/DAP to verify the core's hardware Debug Module status (`dmstatus` at DMI `0x11`) independently of firmware execution state.
6. **Purged `iomem=relaxed` Bootargs**:
   - Because `remoteproc` operates inside kernel space with native `ioremap_wc()`, insecure `/dev/mem` relaxations are obsolete. We permanently removed `iomem=relaxed` from `boot.cmd` and `uboot-env.txt` for both Cubie A5E and Cubie A7A, restoring standard `CONFIG_STRICT_DEVMEM` physical memory protections.

---

## Case Study 7: Radxa Cubie A7A (Allwinner A733) Boot Handoff, GICv3 Migration & DTB Parity
**Date:** August 20, 2026  
**Component:** Device Tree (`sun60i-a733-cubie-a7a.dts`), ARM Trusted Firmware (BL31), GICv3 (`arm,gic-v3`), and Vendor U-Boot 2018.07

### 🚨 Symptoms & The Boot Loops Encountered
During early bring-up of the **Radxa Cubie A7A (Allwinner A733 / `sun60iw2`)** running Linux 7.1 `PREEMPT_RT`, three distinct boot behaviors were observed:

1. **Boot Failure 1 (GICv2 vs GICv3 Firmware Mismatch)**:
   - Kernel booted via earlycon (`[ 0.000000] earlycon: uart8250 at MMIO32 0x0000000002500000`), initialized DMA zones and CPUs, but aborted at IRQ init:
   ```text
   [ 0.000000] Root IRQ handler: gic_handle_irq
   [ 0.000000] GIC CPU mask not found - kernel will fail to boot.
   [ 0.000000] GICv3 system registers enabled, broken firmware!
   [ 0.000000] WARNING: drivers/irqchip/irq-gic.c:57 at gic_cpu_init+0x100/0x108
   ```
   - **Root Cause**: The legacy vendor device tree declared GICv2 (`compatible = "arm,cortex-a15-gic"` at `0x03021000`), but Allwinner ARM Trusted Firmware (BL31) configured the CPU interfaces with GICv3 System Register Enable (`ICC_SRE_EL1.SRE = 1`). Mainline Linux 7.1 strictly rejects GICv2 register access when GICv3 system registers are active.

2. **Boot Failure 2 (The Incompatible DTSI Substitution Trap)**:
   - To fix the GIC definition, we attempted to write a minimal 340-line DTS including `sun55i-a523.dtsi` (producing a 30,886-byte DTB).
   - The board hung silently after BL31 jumped to kernel entry:
   ```text
   NOTICE:  BL3-1: Next image address = 0x40200000
   NOTICE:  BL3-1: Next image spsr = 0x3c5
   ```
   - **Root Cause**: The Allwinner **A733 (`sun60iw2`)** has a **2× Cortex-A76 + 6× Cortex-A55 DynamIQ** cluster, distinct CCU clock register base addresses, and different power domains from the **A523/A527 (`sun55i`)** 8× Cortex-A55 SoC. Substituting the A523 DTSI starved early clocks and broke earlycon before the kernel could print a single character.

3. **Boot Failure 3 (Vendor U-Boot 2018.07 `/memory` Query Failure)**:
   - In Allwinner's vendor U-Boot 2018.07, `update_fdt_dram_para_from_bootpara()` queries for an *existing* `/memory` or `/memory@40000000` node.
   - If missing from the DTS, vendor U-Boot logs `## error: update_fdt_dram_para_from_bootpara : FDT_ERR_NOTFOUND` and skips adding memory banks, causing `early_init_dt_scan_memory()` to find 0 bytes of RAM.

### 🛠️ The Architectural Resolution
We permanently aligned the device tree and build pipeline:

1. **Restored Full A733 Hardware Tree with Surgical GICv3 Mapping**:
   - Re-instated the complete 2564-line A733 vendor hardware map (covering all 2× A76 + 6× A55 CPU nodes, clocks, and power domains).
   - Replaced `interrupt-controller@3020000` with the native GICv3 distributor and redistributors:
   ```dts
   interrupt-controller@3400000 {
       compatible = "arm,gic-v3";
       #interrupt-cells = <0x03>;
       #address-cells = <0x02>;
       #size-cells = <0x02>;
       ranges;
       interrupt-controller;
       reg = <0x00 0x03400000 0x00 0x10000>,
             <0x00 0x03460000 0x00 0x100000>;
       interrupts = <0x01 0x09 0x04>;
       interrupt-parent = <0x9b>;
       dma-noncoherent;
       phandle = <0x9b>;

       its: msi-controller@3440000 {
           compatible = "arm,gic-v3-its";
           reg = <0x00 0x03440000 0x00 0x20000>;
           msi-controller;
           #msi-cells = <0x01>;
           dma-noncoherent;
       };
   };
   ```
   - Maintained `phandle = <0x9b>` so all 2500+ lines of peripheral nodes connect directly to the GICv3 interrupt controller without breaking phandle references.

2. **Fixed `post-image.sh` DTB Overwrite**:
   - Removed the stale `cp -f` in `board/radxa/cubie_a7a/post-image.sh` that was previously overwriting the kernel's freshly-compiled DTB with an outdated vendor binary.

3. **Metrics & Artifacts Summary**:
   - **DTB Size**: `41,583 bytes` (compiled as `sun60i-a733-cubie-a7a.dtb`).
   - **Kernel**: Linux 7.1.0 `PREEMPT_RT` with built-in `sunxi_rproc.o`.
   - **Disk Image**: `sdcard.img` (620,756,992 bytes) ready in `bld.a7a/images/`.

---

## Case Study 8: Analysis of Allwinner A733 (`sun60iw2`) Official Factory Image vs Mainline Linux 7.1 & Decision to Pause Mainline A7A Bring-Up
**Date:** August 20, 2026  
**Component:** Official Radxa Image (`radxa-a733_bullseye_kde_r6.output_512.img.xz`), Mainline Linux 7.1 PREEMPT_RT, Mainline U-Boot 2026.01

### 🔍 Deep Dive: Official Factory Image Extraction & DTB Comparison
To eliminate ambiguity, we extracted and disassembled the official Radxa factory image (`radxa-a733_bullseye_kde_r6.output_512.img.xz`):
- **Extracted Factory DTB**: `extracted_official.dtb` (41,449 bytes from sector 26902).
- **Line-by-Line DTS Comparison**: Across 2,500+ lines, our hardware map is 100% identical to the factory image, except:
  1. Factory DTB omits `/cpus`, `/timer`, `/psci`, and `/memory` because vendor U-Boot and BL31 create/patch those at runtime.
  2. Factory DTB declares legacy GICv2 (`interrupt-controller@3020000`). Radxa's vendor kernel (Linux 5.10 / 6.1 BSP) includes vendor patches in `drivers/irqchip/irq-gic.c` to accept GICv2 even with `ICC_SRE_EL1.SRE=1`. Mainline Linux 7.1 enforces strict upstream ARM64 security and aborts.

### 🛑 Root Cause: Allwinner A733 Is Not Yet in Upstream Mainline
- **Upstream U-Boot**: 0% mainline support. No `sun60iw2` CCU, pinctrl, or LPDDR5 multi-PState training driver. It depends completely on Allwinner's closed-source `boot0` binary blob.
- **Upstream Linux 7.1**: 0% mainline support. While `CONFIG_ARCH_SUNXI` supports A527/T527 (`sun55i`) via `ccu-sun55i-a523.c` and `pinctrl-sun55i-a523.c`, there are no `ccu-sun60iw2.c` or `pinctrl-sun60iw2.c` drivers in upstream Linux.

### 🎯 Engineering Decision
1. **Focus on A5E as Primary Platform**: The **Cubie A5E (Allwinner A527 / T527 / `sun55i`)** remains our verified 100% upstream mainline production platform.
2. **A7A Hardware Reference**: Use the official Radxa factory image (`radxa-a733_bullseye_kde_r6.output_512.img.xz`) for standalone A7A hardware, NPU, and ISP validation.

---

## Case Study 9: Discovery of Upstream A733 CCU & Pinctrl Patch Series and Automated Watcher Strategy
**Date:** August 20, 2026  
**Component:** `drivers/clk/sunxi-ng/ccu-sun60i-a733.c`, `drivers/pinctrl/sunxi/pinctrl-sun60i-a733.c`, `tools/watch_a733_upstream.py`

### 🔍 Discovery: Active Upstream Review of A733 Drivers
Following our investigation into the root cause of the peripheral clock starvation on the A733, we traced upstream kernel and U-Boot mailing list submissions:
1. **Clock Controller (CCU & PRCM)**: Junhui Liu submitted `clk: sunxi-ng: Add support for Allwinner A733 CCU and PRCM` (under active v2/v7 review on `linux-clk` & `linux-sunxi`), introducing `ccu-sun60i-a733.c` and `ccu-sun60i-a733-r.c`.
2. **RTC & Base Clocks**: Jerome Brunet and Chen-Yu Tsai queued `clk: sun6i-rtc: Add support for Allwinner A733 SoC` for the Linux 7.3 merge window.
3. **Pin Controller (Pinctrl)**: Yixun Lan submitted `pinctrl: sunxi: a733: add initial support` (`pinctrl-sun60i-a733.c`) covering PIO and R-PIO blocks.
4. **U-Boot Base Support**: Yixun Lan submitted initial A733 U-Boot support on Patchwork.

### 🛠️ Strategic Solution: Buildroot Patch Integration + Upstream Watcher
1. **Hybrid Mainline Route**: Rather than waiting for a full 6-month kernel release cycle, we can integrate the `ccu-sun60i-a733` and `pinctrl-sun60i-a733` patch series into `project-cubie-a5e/patches/linux/` and combine it with the working `radxa_a733_bootloader.bin` LPDDR5 DRAM trainer.
2. **Automated Tracking Tool (`tools/watch_a733_upstream.py`)**: Developed an automated tool that queries `lore.kernel.org` (linux-sunxi, linux-clk) and U-Boot Patchwork to monitor for new patch revisions (v3, v4, etc.) and git pull requests.

---

## Case Study 10: Radxa Cubie A7A Boot Handoff Failure: Vendor U-Boot 2018.07 vs Mainline Memory & Entry Point Friction
**Date:** August 21, 2026  
**Component:** `boot.cmd`, `uboot-env.txt`, `sun60i-a733-cubie-a7a.dts`, ARM Trusted Firmware (BL31), Vendor U-Boot 2018.07 (`radxa_a733_bootloader.bin`)

### 🚨 Symptoms
Upon booting the newly recompiled Linux 7.1 `PREEMPT_RT` kernel on the Radxa Cubie A7A, the system initialized through vendor U-Boot and ARISC SCP firmware, but halted silently at the ARM Trusted Firmware handoff:
```text
Found U-Boot script /boot.scr
## Executing script at 4fc00000
41583 bytes read in 10 ms (4 MiB/s)
44395008 bytes read in 1888 ms (22.4 MiB/s)
[15.468]libfdt fdt_path_offset() returned FDT_ERR_BADMAGIC
[15.485]## error: update_fdt_dram_para_from_bootpara : FDT_ERR_NOTFOUND
[15.528]Starting kernel ...
NOTICE:  [SCP] :wait arisc ready....
NOTICE:  [SCP] :arisc version: [d463b9da43dc50320f21ba51c6c51afe2db20d83]
NOTICE:  [SCP] :arisc startup ready
NOTICE:  [SCP] :sunxi-arisc driver is starting
NOTICE:  BL3-1: Next image address = 0x40200000
NOTICE:  BL3-1: Next image spsr = 0x3c5
<hang - no earlycon output>
```

### 🔍 Root Cause Analysis: Why Basic Bring-Up Checks Failed
The failure stemmed from an impedance mismatch between **modern mainline ARM64 defaults** (used by Cubie A5E) and **Allwinner's 2018 vendor bootloader blob** (required by Cubie A7A):

1. **Kernel Entry Address Mismatch (`0x40080000` vs `0x40200000`)**:
   - Modern upstream U-Boot and ARM64 kernels default to `kernel_addr_r = 0x40080000` (512 KB DRAM offset).
   - Radxa's vendor BL31 binary blob (`radxa_a733_bootloader.bin`) was compiled with a hardcoded non-secure entrypoint of `0x40200000` (2 MB legacy DRAM offset).
   - When BL31 jumped to `0x40200000`, it branched **1.5 MB into the middle of the kernel binary**, causing the core to immediately execute invalid instructions and fault before `earlycon` could print a single character.

2. **Legacy U-Boot DRAM Node Query (`FDT_ERR_NOTFOUND`)**:
   - Modern upstream U-Boot generates `/memory` nodes dynamically at runtime if they are missing from the DTS.
   - Allwinner's vendor U-Boot 2018.07 relies on `fdt_path_offset(fdt, "/memory")` to locate an *existing* node and patch DRAM calibration timings from `bootpara`.
   - Because `sun60i-a733-cubie-a7a.dts` followed modern mainline practice (omitting `/memory`), vendor U-Boot failed with `FDT_ERR_NOTFOUND` and skipped passing physical memory geometry to the kernel.

3. **Why this was missed**:
   - The A7A configuration was ported directly from the verified A5E mainline template. While the A5E uses upstream U-Boot 2026.01, the A7A relies on an 8-year-old closed-source vendor bootloader fork with legacy BSP requirements.

### 🛠️ The Fixes
1. **Realaligned Kernel Address**:
   - Updated `kernel_addr_r` from `0x40080000` to `0x40200000` in both `board/radxa/cubie_a7a/boot.cmd` and `uboot-env.txt`.
2. **Explicit Device Tree Base Memory & Chosen Nodes**:
   - Injected the base 6 GiB memory node and earlycon into `sun60i-a733-cubie-a7a.dts` and `patches/linux/0001-arm64-dts-allwinner-add-sun60i-a733-cubie-a7a.patch`:
     ```dts
     chosen {
         bootargs = "earlycon=uart8250,mmio32,0x02500000 console=ttyS0,115200 root=/dev/mmcblk0p2 rootwait panic=10 isolcpus=7 nohz_full=7 rcu_nocbs=7 loglevel=8";
     };

     memory@40000000 {
         device_type = "memory";
         reg = <0x00 0x40000000 0x01 0x80000000>; /* 6 GiB */
     };
     ```
3. **Rebuilt Build Pipeline**:
   - Executed `make linux-rebuild` and `make` in `bld.a7a`, generating updated `boot.scr`, `uboot.env`, `sun60i-a733-cubie-a7a.dtb`, and `sdcard.img`.




