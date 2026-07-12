# Cubie A5E Flight Controller

This repository contains the files to build a custom Linux distribution for the **Radxa Cubie A5E** and run the flight controller application stack.

---

## Current Project Status

As of the current bring-up phase, here is the functional status of the flight stack hardware and software components:

* **✅ Base OS & Bootloader (Tested/Functional):** U-Boot successfully loads the custom device tree overlays. The Linux kernel (`PREEMPT_RT`) boots correctly, successfully isolates CPU Core 7, mounts the rootfs, and brings up the AIC8800 Wi-Fi driver and `wpa_supplicant`.
* **⚠️ RISC-V Co-processor (Code Ready, Not Hardware Tested):** The bare-metal C++ firmware (`riscv-firmware`) and the ARM Linux real-time IPC bridge (`rbb-server`) are fully compiled, utilizing hardware Mailbox doorbells and lock-free shared memory. However, the end-to-end telemetry loop has not yet been physically verified on the board.
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
* **Antigravity Profiles:** Loads architectural bounds and engineering mandates from [`.antigravity/profiles.json`](.antigravity/profiles.json). This defines the host domain (ARM Cortex-A55 mainline Linux) vs. the real-time domain (XuanTie RISC-V bare-metal/Melis), and states mandates like using mainline Linux syntax/vb2_dma_contig allocator and compiling the AIC8800 driver against standard mainline wireless stacks.
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

# 2. Configure the build
mkdir -p bld
PATH=$PWD/bld/bin:$PATH make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig

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
