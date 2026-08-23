# Cubie A5E & Cubie A7A Flight Controller

This repository contains the files to build a custom Linux distribution for the **Radxa Cubie A5E** (Allwinner A527/T527) and **Radxa Cubie A7A** (Allwinner A733) single-board computers and run the flight controller application stack.

---

## Why This Repository? (Mainline vs. Vendor BSP)

If you've used the default Radxa Debian or Ubuntu images, you know the pain: ancient, heavily patched kernels (often Linux 5.10 or older), proprietary binary blobs, out-of-tree drivers that break on updates, and zero real-time determinism. 

**This project fundamentally breaks that mold.**

Here is why this stack is superior for robotics, aerospace, and high-performance embedded engineering:

1. **Zero Bloat, Pure Mainline (Linux 7.1+)**: We discarded the bloated vendor BSP entirely. This OS is built from scratch using Buildroot, targeting the absolute bleeding-edge mainline Linux kernel. If a driver isn't in mainline, we upstream it ourselves (like the FOSS Etnaviv NPU driver and our cleanly refactored Wi-Fi stack).
2. **Hard Real-Time Determinism (`PREEMPT_RT`)**: The default Radxa image is built for general-purpose desktop use. This image is built for flight. We patch the kernel with `PREEMPT_RT`, strictly isolate CPU cores, and utilize bare-metal RISC-V co-processors to guarantee microsecond-level execution loops without OS jitter.
3. **Reproducibility**: No more flashing mysterious pre-compiled images and praying. Every single configuration, device tree overlay, kernel patch, and compiler flag is codified in our Buildroot external tree. Run `make` and you get an identical, bit-for-bit reproducible operating system every time.
4. **Architectural Transparency**: Vendor images hide hardware complexity behind opaque HALs and blobs. We expose it. Every subsystem—from the Mailbox IPC synchronization to the memory-mapped Camera pipelines—is documented with engineering blueprints and KUnit tests.

---

## Supported Boards & Hardware Comparison

| Hardware Feature | Radxa Cubie A5E | Radxa Cubie A7A |
| :--- | :--- | :--- |
| **System on Chip (SoC)** | Allwinner **A527 / T527** (`sun55i-a527`) | Allwinner **A733** (`sun60i-a733`) |
| **CPU Architecture** | 8× Arm Cortex-A55 @ 1.8 GHz | 2× Arm Cortex-A76 @ 2.0 GHz + 6× Cortex-A55 @ 1.8 GHz |
| **Real-Time Co-Processor** | XuanTie E907 RISC-V @ 600 MHz (0-cycle TCM) | XuanTie E907 RISC-V @ 600 MHz (0-cycle TCM) |
| **NPU AI Accelerator** | 2.0 TOPS (Teflon / TFLite Delegate) | 3.0 TOPS (Teflon / TFLite Delegate) |
| **GPU Core** | Arm Mali-G57 MC1 | Imagination BXM-4-64 MC1 |
| **System RAM** | LPDDR4 / LPDDR4X | LPDDR5 |
| **Wi-Fi 6 & Bluetooth 5.4** | AicSemi AIC8800 (**SDIO** Bus) | AicSemi AIC8800 (**USB** Bus) |
| **Storage Interfaces** | MicroSD / eMMC Module / SPI NOR | MicroSD / eMMC Module / UFS Module / SPI NOR |
| **Flight Bus Header** | Standard 40-pin GPIO (3× SPI, 2× I2C, 2× UART) | Standard 40-pin GPIO (3× SPI, 2× I2C, 2× UART) |
| **Linux Kernel Target** | Mainline Linux 7.1 (`PREEMPT_RT`) | Mainline Linux 7.1 (`PREEMPT_RT`) |
| **Buildroot Defconfig** | `cubie_a5e_defconfig` | `cubie_a7a_defconfig` |
| **Device Tree Base** | `allwinner/sun55i-a527-cubie-a5e.dtb` | `allwinner/sun60i-a733-cubie-a7a.dtb` |
| **Flight Stack Overlay** | `cubie-a5e-flight-stack.dtbo` | `cubie-a7a-flight-stack.dtbo` |

---

### Shared Hardware Pinout & Flight Bus Continuity
Both the **Cubie A5E** and **Cubie A7A** share the identical 40-pin GPIO physical header assignment:
- **SPI0 (Pins 19, 21, 23, 24):** Dedicated Link A for ultra-low latency IMU attitude estimation.
- **SPI1 (Pins 12, 35, 38, 40):** Dedicated Link B for bidirectional AbstractX FPGA coprocessor communication.
- **SPI2 (Pins 7, 15, 16, 18):** General Purpose expansion SPI bus.
- **I2C1 (Pins 3, 5) & I2C3 (Pins 27, 28):** Dual I2C buses for external compass, barometer, and flight telemetry.
- **UART0 (Pins 8, 10):** Mainline Linux serial debug console.
- **Isolated Core 7:** Core 7 is isolated on both chips (`isolcpus=7 nohz_full=7 rcu_nocbs=7`) for jitter-free real-time flight loops.

---

## Project Mantra & Core Philosophies

1. **Mainline First:** We reject ancient, bloated vendor BSP kernels. We target the absolute latest mainline Linux kernel releases and push for pure FOSS (Free and Open-Source Software) drivers (e.g., Etnaviv for the NPU, V4L2 for camera pipelines). 
2. **ArduPilot-Grade Determinism:** Flight loops must not jitter. We achieve microsecond-level hard real-time execution by aggressively isolating the OS (`PREEMPT_RT`, `isolcpus`, `mlockall`, `SCHED_FIFO`) and offloading zero-tolerance timing to the bare-metal RISC-V and FPGA co-processors.
3. **Zero-Cost Abstractions:** Embedded code doesn't have to be unsafe C macros. We embrace modern C++ (C++20) for strict type safety and `std::atomic` lock-free IPC, compiling with `-fno-exceptions` to generate perfectly optimized, bloat-free assembly.
4. **Transparent Engineering:** We document the "why," not just the "how." Every register map, architectural decision, and debugging nightmare is extensively logged so future aerospace engineers can learn from the hardware up.

---
## Current Project Status

As of the current bring-up phase, here is the functional status of the flight stack hardware and software components:

* **✅ Base OS & Bootloader (100% OPERATIONAL & VERIFIED ON HARDWARE):** 
  - **Radxa Cubie A5E (Allwinner A527/T527):** Mainline Linux 7.1 (`PREEMPT_RT`) fully operational on real silicon. Ext4 rootfs read-write mounting, isolated CPU Core 7 (`isolcpus=7 nohz_full=7 rcu_nocbs=7`), Etnaviv NPU (GC9000 rev 9003), Panfrost GPU (Mali-G57 MC1), dual Gigabit Ethernet MACs (`dwmac-sun55i` / `dwmac-sun8i`), and AXP717 + AXP323 PMICs.
  - **Radxa Cubie A7A (Allwinner A733):** Full multi-stage boot chain verified on real silicon: `BootROM` $\rightarrow$ `boot0` (6 GiB LPDDR5 auto-training) $\rightarrow$ `TOC1` $\rightarrow$ `TF-A BL31` $\rightarrow$ `OP-TEE` $\rightarrow$ `Mainline U-Boot 2026.01-rc1` (4KB page-aligned at `0x4a001000`) $\rightarrow$ `Mainline Linux 7.1.0 PREEMPT_RT` booted across all 8 SMP cores (6× Cortex-A55 + 2× Cortex-A78) with 6 GiB RAM.
  - **Multi-Board Device Trees:** Native upstream support for Radxa Cubie A5E (`sun55i-a527-cubie-a5e.dtb`) and Radxa Cubie A7A (`sun60i-a733-cubie-a7a.dtb`).

* **✅ Mainline Wi-Fi 6 Driver (100% OPERATIONAL & DUAL-BUS READY):** 
  - **Unified Dual-Bus Architecture:** Mainline kernel driver (`aic8800-upstream`) unified with modular transport HAL backends:
    - **SDIO Transport (Radxa Cubie A5E):** Multi-module `aic8800_bsp.ko` + `aic8800_fdrv.ko` verified on real silicon with sub-300ms firmware upload and full Wi-Fi 6 association.
    - **USB Transport (Radxa Cubie A7A):** Standalone `aic8800_fdrv.ko` registering directly with Linux `usbcore` (`0xA69C:0x8800` / `0x8801`) without BSP dependency.
  - **Bus Timing & Probe Wakeup Stabilized:** Guarded internal IOPAD delay registers (`0xF0`/`0xF8`/`0xF1`) to prevent MMC data errors at 25 MHz, added explicit chip wakeup during probe, and implemented safe BootROM fallback.
  - **Linux 7.1 PREEMPT_RT Verified:** Clean 0-warning, 0-error compilation across both `bld.a5e` and `bld.a7a` target buildroots.
  - **RFC v3 Mainline Preparation:** Clean 4-patch series codified under [`docs/upstream_patches/`](docs/upstream_patches/) and tracked in the [Action Plan](docs/buildroot/AIC8800_Porting_Action_Plan.md).

* **✅ Real-Time Determinism & Core Isolation (100% OPERATIONAL):**
  - **Flight Loop Isolation:** CPU Core 7 is strictly isolated for microsecond-level determinism.
  - **IRQ Priority Elevation:** [`/etc/init.d/S15realtime`](project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/init.d/S15realtime) dynamically steers IRQ affinities away from Core 7 to Cores 0–6 and elevates SPI/I2C kernel IRQ thread priorities to **85** (preempting the flight loop at 80).

* **✅ RISC-V Co-Processor Lifecycle & Live Trace (100% OPERATIONAL & VERIFIED ON HARDWARE):**
  - **Option A Direct SRAM Execution Proven:** Proven that the XuanTie E907 co-processor has **no silicon BootROM** and executes directly from **256 KB Dedicated Zero-Wait-State RISC-V Local SRAM at `0x0728_0000`** (Core address `0x0000_0000`) with zero wait states.
  - **Linux RemoteProc Standard (`sunxi_rproc.c`):** Full kernel lifecycle management (`echo start/stop > /sys/class/remoteproc/remoteproc0/state`). Driver implements `.prepare()` to clock `CLK_DSP` and `TZMA` bus bridges before ELF loading, and `devm_ioremap_wc()` (Normal Non-Cacheable RAM) matching upstream TI/Xilinx/NXP standards.
  - **Standard Debugfs Live Trace Stream:** Co-processor streams real-time ASCII flight diagnostics directly to Linux userspace via `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
  - **Live SRAM C Telemetry:** Verified real-time telemetry block at `0x00028000` with active heartbeat ticking at **~130 Hz** (`Magic = 0x52495343` "RISC").
  - **AbstractX Integration:** Powered by the open-source [AbstractX](https://github.com/tcmichals/AbstractX) C++20 coroutine engine for zero-allocation cooperative multitasking and HALO compiler elision (19x faster context-switching vs. FreeRTOS).

* **🔄 Next Step — Local OpenOCD & GDB Remote Debugging (In Progress):**
  - **JTAG-Less On-Chip Debugging (MMIO):** Connect `riscv-none-elf-gdb` and OpenOCD directly to the XuanTie E907 Debug Module registers (`0x07090000`) over the SoC internal bus for hardware breakpoints, register inspection (`x0`–`x31`), and single-stepping without physical JTAG hardware dongles.

* **⚠️ NPU / TinyML (Compiled in, Integration Ready):** Open-source Etnaviv DRM kernel drivers (GC9000 NPU bound on `/dev/dri/card0`) and the Teflon TensorFlow Lite delegate (`libteflon.so`) are built into the rootfs, ready for vision pipeline testing.

---
## Architectural Documentation & Technical Articles

1. **[Bringing Up Heterogeneous RISC-V on Allwinner SoCs (4-Part Technical Series)](docs/articles/README.md)**:
   * **[Part 1: Architecture, Memory-Mapped Debugging, and Why We Ditched `/dev/mem` Hacks](docs/articles/part1_heterogeneous_riscv_intro_architecture.md)** — Silicon taxonomy (`T527`/`A733`/`sun55i`), TRM memory maps, and JTAG-less on-chip debugging over OpenOCD.
   * **[Part 2: Building the Linux `remoteproc` Driver and Proving Hardware State](docs/articles/part2_building_remoteproc_and_hardware_proof.md)** — `sunxi_rproc.c` driver, surgical ELF mapping, debugfs trace logs, and automated Python DMI hardware verification.
   * **[Part 3: Bare-Metal Firmware, Lightweight IPC, and C++ Coroutines Intro](docs/articles/part3_baremetal_firmware_ipc_and_coroutines_intro.md)** — Zero-wait TCM determinism, lightweight lock-free shared SRAM ring buffers + Mailbox interrupts, and live GDB workflows.
   * **[Part 4: Deploying the AbstractX C++20 Coroutine Framework on XuanTie E907](docs/articles/part4_deep_dive_baremetal_cpp_coroutines.md)** — Deploying AbstractX on bare-metal RISC-V, HALO compiler optimizations (0 cycles / 0 bytes), benchmarks vs FreeRTOS (19x speedup), and non-blocking hardware awaiters.

2. **[Heterogeneous Avionics Architecture & Bring-Up Guide](docs/HETEROGENEOUS_AVIONICS_ARCHITECTURE.md)**:
   Comprehensive system architecture covering the Cortex-A76 isolated Core 7 flight loop, C++20 Coroutine Async Engine (`when_any`, `when_all`), 16-channel DMA partitioning, MSGBOX mailbox doorbells, and Dual-SPI FPGA TLP packet streaming.

3. **[Allwinner XuanTie RISC-V Remote Processor (`sunxi_rproc`) Guide](docs/ALLWINNER_RISCV_REMOTEPROC_GUIDE.md)**:
   Technical deep-dive into the mainline Linux 7.1 RemoteProc driver (`drivers/remoteproc/sunxi_rproc.c`), standalone kernel patch, device tree schemas, and `/sys/class/remoteproc/` user-space control.

4. **[Radxa Cubie A7A Platform Specification & Patch Roadmap](docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)**:
   Hardware specs, LPDDR5 dynamic training architecture, GICv3 interrupt controller mapping, and upstream patch series tracking ([`tools/watch_a733_upstream.py`](tools/watch_a733_upstream.py)).

5. **[Allwinner A733 Boot Architecture & Disk Geometry](docs/buildroot/A733_Boot_Architecture_And_Disk_Layout.md)**:
   Exhaustive analysis of the A733 BROM 128 KB search offset, multi-stage bootloader staging, and 16 MB partition alignment.

6. **[Buildroot OS Documentation](docs/buildroot/)**:
   How we use Buildroot to configure, build, and package the custom Linux operating system (`sdcard.img`).

7. **[Flight Controller Application Documentation](docs/flightcontroller/)**:
   High-level flight logic, rate PID dynamics, TinyML/NPU models, and real-time FPGA co-processor communication over SPI.

---

## AI Assistant & IDE Context

This repository includes project-context and prompt configurations that are automatically read by AI coding assistants to enforce system architecture, package layouts, and coding conventions:
* **Antigravity Profiles:** Loads architectural bounds and engineering mandates from [`.antigravity/profiles.json`](.antigravity/profiles.json). This defines the host domain (ARM Cortex-A55 / A76 mainline Linux) vs. the real-time domain (XuanTie RISC-V bare-metal/Melis), and states mandates like using mainline Linux syntax/vb2_dma_contig allocator and compiling the AIC8800 driver against standard mainline wireless stacks.
* **Cursor / Antigravity Rules:** Enforces workspace rules via [`.cursorrules`](.cursorrules) on workspace startup.
* **VS Code Copilot:** Reads [`.github/copilot-instructions.md`](.github/copilot-instructions.md) to bootstrap chat and inline completion context.

### Workspace Prompts & Blueprints

We maintain structured engineering blueprints under [`workspace_prompts/`](workspace_prompts/) to guide phased development, alongside their completed target diagnostics and memory maps:
1. **[Camera Media Controller Linkage](workspace_prompts/prompt1_mainline_camera.md)**
2. **[Stateless VEU Encoder Driver](workspace_prompts/prompt2_mainline_veu_encoder.md)**
3. **[XuanTie RISC-V Ring-Buffer Ingestion](workspace_prompts/prompt3_riscv_ingestion.md)** — See Bring-up Guide: [HowToRISCV.md](docs/buildroot/HowToRISCV.md) and Memory Map: [prompt3_riscv_tcm_map.md](workspace_prompts/prompt3_riscv_tcm_map.md)
4. **[Bidirectional Mailbox IPC Synchronization](workspace_prompts/prompt4_mailbox_sync.md)** — See Trace Log: [prompt4_mailbox_sync_trace.md](workspace_prompts/prompt4_mailbox_sync_trace.md), Kernel Driver: [sunxi_t527_rproc.c](bld/build/linux-7.1/drivers/remoteproc/sunxi_t527_rproc.c), RPMsg Example: [rpmsg_host_example.c](project-cubie-a5e/rpmsg_host_example.c), and Kernel Patch: [.antigravity/patches/0003-mailbox-sunxi-t527-driver.patch](.antigravity/patches/0003-mailbox-sunxi-t527-driver.patch)
5. **[Local JTAG-less Debugging via ARM MMIO](workspace_prompts/prompt5_riscv_debug_bridge.md)** — **See full GDB Step-by-Step Guide: [HowToDebugE907.md](docs/buildroot/HowToDebugE907.md)**
6. **[Mainline Linux Wi-Fi Integration](workspace_prompts/prompt6_mainline_wifi.md)** — See FOSS Guide: [HowToNPU.md](docs/buildroot/HowToNPU.md)

#### How to Use These Prompts
These files are designed to bootstrap an AI coding agent (like Cursor or Antigravity) with precise context for a given engineering goal:
1. **Feed the Prompt:** Copy the contents of the chosen blueprint (e.g., `prompt6_mainline_wifi.md`) or reference it directly in your AI chat (using `@prompt6_mainline_wifi.md` or equivalent).
2. **Execute Phases:** Instruct the AI assistant to work through the defined **Implementation Phases** sequentially.
3. **Enforce Mandated Rules:** The AI will automatically adhere to the **Mandated Rules** (such as avoiding legacy vendor drivers, enforcing zero-copy vb2 memory buffers, or maintaining isolated CPU cores).
4. **Generate Trace Logs:** As execution proceeds, the AI must output the required trace logs (e.g., `prompt6_wifi_mainline_diagnostics.md`) to document exactly how registers and symbols were mapped, providing a clear educational history for future developers.

These configurations keep AI agents aligned on the OS/Application boundaries, custom package layouts, U-Boot device tree overlays (`fdt apply`), and workspace defaults.

---

## Quick Start (Build the OS)

For complete build instructions and prerequisites, see [Buildroot System How-To](docs/buildroot/BuildRootHowTo.md).

### 1. Clone Buildroot (if not already cloned)
```bash
git clone https://github.com/buildroot/buildroot.git
```

### 2. Configure and Build for Your Target Board

#### Option A: Radxa Cubie A5E (Allwinner A527 / T527 — SDIO Wi-Fi 6)
```bash
mkdir -p bld
PATH=$PWD/bld/bin:$PATH make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig
PATH=$PWD/bld/bin:$PATH make -C bld
```

#### Option B: Radxa Cubie A7A (Allwinner A733 — USB Wi-Fi 6)
```bash
mkdir -p bld
PATH=$PWD/bld/bin:$PATH make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a7a_defconfig
PATH=$PWD/bld/bin:$PATH make -C bld
```

The resulting bootable image is generated at `bld/images/sdcard.img`.

---

## Flashing the Image

Write the image to your SD card (replace `/dev/sdX` with your SD card device node):

```bash
sudo dd if=$PWD/bld/images/sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

> [!WARNING]
> Double-check `/dev/sdX` before running `dd` to avoid overwriting the wrong drive.
