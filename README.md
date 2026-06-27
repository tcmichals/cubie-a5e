# Cubie A5E Flight Controller

This repository contains the files to build a custom Linux distribution for the **Radxa Cubie A5E** and run the flight controller application stack.

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

This repository includes project-context files that are automatically read by AI coding assistants to enforce system architecture, package layouts, and coding conventions:
* **Cursor / Antigravity:** Automatically reads [`.cursorrules`](.cursorrules) on workspace startup.
* **VS Code Copilot:** Automatically reads [`.github/copilot-instructions.md`](.github/copilot-instructions.md) to bootstrap chat and inline completion context.

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
