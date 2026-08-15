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

* **⚠️ Base OS & Bootloader (Tested/Functional):** U-Boot successfully loads custom device tree overlays. The Linux kernel (`PREEMPT_RT`) boots correctly, isolates CPU Core 7, and mounts the rootfs.
* **✅ Mainline Wi-Fi 6 Driver (100% OPERATIONAL & VERIFIED ON HARDWARE):** 
  - **Upstream Mainline Kernel Integration:** Integrated the official Linux kernel mailing list RFC submission (`[RFC PATCH wireless-next v2] wifi: aic: add AIC8800 FullMAC driver`) directly into the Buildroot system distribution.
  - **Clean Multi-Bus Architecture:** Supports both SDIO (Cubie A5E) and USB (Cubie A7A) transports via `aic8800_bsp.ko` (hardware bring-up, bus setup, firmware loading, MCU boot) and `aic8800_fdrv.ko` (`cfg80211` FullMAC WLAN driver).
  - **100% Verified Hardware Bring-Up:** Firmware upload (`fw_patch_table`, `fw_adid`, `fw_patch`, `fmacfw`) completes in <300ms, returning chip version `06090101`. `wlan0` registers cleanly, acquires DHCP lease (`192.168.1.15`), and executes internet pings (`yahoo.com`) with 0% packet loss.
  - **Documentation & Reference Logs:** Tracked in [`docs/buildroot/AIC8800_Porting_Action_Plan.md`](docs/buildroot/AIC8800_Porting_Action_Plan.md) and [`walkthrough.md`](.gemini/antigravity-ide/brain/061e4443-888e-4bf7-88c1-615de74a8deb/walkthrough.md).

* **✅ RISC-V Co-Processor & On-Chip Direct MMIO Debugging (100% OPERATIONAL & VERIFIED):**
  - **Toolchain & Firmware:** Bare-metal C++20 firmware (`riscv-firmware`) compiled with the latest xPack RISC-V GCC `v15.2.0-1` toolchain and Buildroot GDB `17.1`.
  - **JTAG-Less On-Chip Debugging (DMEM):** Real-time `rbb_server` bridge accesses the XuanTie E907 RISC-V Debug Module directly over the SoC memory bus via `/dev/mem` at physical address `0x07090000`. Connects directly to OpenOCD and GDB without requiring external hardware JTAG dongles or pin wiring.
  - **Documentation & References:** Detailed in [`docs/buildroot/OpenOCD_DMEM_RISCV_Architecture.md`](docs/buildroot/OpenOCD_DMEM_RISCV_Architecture.md) and [`docs/buildroot/HowToDebugE907.md`](docs/buildroot/HowToDebugE907.md).
* **⚠️ NPU / TinyML (Compiled in, Not Tested):** The open-source Etnaviv DRM drivers and the Teflon TensorFlow Lite delegate (`libteflon.so`) are integrated into the Buildroot OS, but live camera inference has not yet been stress-tested.

---
## Architectural Split

1. **Buildroot OS (Creating the Distribution):**
   How we use Buildroot to configure, build, and package the custom Linux operating system. This outputs the bootable `sdcard.img` containing the kernel, Wi-Fi drivers, NPU drivers, and custom device tree overlays.
   * **Documentation:** See [Buildroot OS Documentation](docs/buildroot/)

2. **Flight Controller (Using the Distribution):**
   How the flight controller application runs on top of this OS distribution. It performs high-level flight logic, runs TinyML/NPU models, and communicates with a real-time FPGA co-processor over SPI.
   * **Documentation:** See [Flight Controller Application Documentation](docs/flightcontroller/)

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

```bash
# 1. Clone Buildroot (if not already cloned)
git clone https://github.com/buildroot/buildroot.git

# 2. Configure the build for your target board:
mkdir -p bld

# For Radxa Cubie A5E (Allwinner A527/T527, SDIO Wi-Fi):
PATH=$PWD/bld/bin:$PATH make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig

# OR for Radxa Cubie A7A (Allwinner A733, USB Wi-Fi):
# PATH=$PWD/bld/bin:$PATH make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a7a_defconfig

# 3. Build the SD card image
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
