# NPU & TinyML Acceleration Guide

This guide explains how the Neural Processing Unit (NPU) and TinyML hardware-accelerated inference stack is configured and validated on the Radxa Cubie A5E flight controller.

---

## 1. NPU Software Stack

To run TensorFlow Lite models using NPU hardware acceleration instead of CPU-bound inference, the following components are integrated:

```text
┌─────────────────────────────────────────────────────────┐
│        Python / C++ Application (Landing Assist)        │
├─────────────────────────────────────────────────────────┤
│            TensorFlow Lite Inference Engine             │
├─────────────────────────────────────────────────────────┤
│        TensorFlow Lite TIM-VX NPU Delegate             │
├─────────────────────────────────────────────────────────┤
│    TIM-VX / OpenVX Userspace HAL (NPU Driver Libraries) │
├─────────────────────────────────────────────────────────┤
│   galcore Kernel Module (/dev/galcore NPU device node)  │
└─────────────────────────────────────────────────────────┘
```

In this Buildroot tree:
* `BR2_PACKAGE_TENSORFLOW_LITE=y` is enabled.
* `BR2_PACKAGE_TIMVX_DELEGATE=y` installs Userspace HAL libraries and `/usr/bin/npu-smoke-test`.
* `BR2_PACKAGE_SUNXI_GALCORE=y` provides the kernel driver stub (currently in development).

---

## 2. TIM-VX Prebuilt Runtime Bundle

Because NPU userspace libraries are proprietary and closed-source, they cannot be built from source by Buildroot. You must supply these compiled libraries to package them into the image.

To automate this setup, a helper script is provided at `project-cubie-a5e/scripts/setup-npu-bundle.sh`.

### A. Populating the Bundle Automatically

The script can automatically extract the necessary NPU libraries from a running board or an official Radxa OS `.img` file.

**Option 1: Pull from a running Radxa Cubie board via SSH**
Ensure your board is powered on, connected to the same network, and run:
```bash
./project-cubie-a5e/scripts/setup-npu-bundle.sh ssh [board_ip] [username]
```
*(If no IP/username is provided, the script defaults to `rock@192.168.1.100`)*

**Option 2: Extract from a local Radxa Debian/Ubuntu image**
If you have downloaded the official Radxa OS `.img` file to your build machine, run:
```bash
./project-cubie-a5e/scripts/setup-npu-bundle.sh image /path/to/radxa-image.img
```
*(This mounts the image read-only using a loop device and extracts the required `.so` files into the bundle).*

### B. Workspace Layout

The script creates the workspace-level `timvx-bundle/` directory at the project root:
```text
/home/tcmichals/projects/cubie-a5e/
├── bld/
├── project-cubie-a5e/
└── timvx-bundle/          <-- Populated by the script
    ├── lib/
    │   └── libtim-vx.so   (and other .so files)
    └── bin/
        └── vpm_run        (optional test tool)
```
If this directory exists, Buildroot will find and copy the files automatically into `/usr/lib/` and `/usr/bin/` during compilation. No extra command-line arguments are required!

### C. Custom Location Override

If you prefer to manually store the precompiled bundle in a different directory on your system, override the path during build:
```bash
PATH=$PWD/bld/bin:$PATH make -C bld \
    BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR=/home/tcmichals/my-timvx-folder
```

---

## 3. On-Target Validation (NPU Smoke Test)

The image includes `/usr/bin/npu-smoke-test` to verify the NPU stack's health on the physical board. 

Run on the target shell:
```bash
/usr/bin/npu-smoke-test
```

This script validates:
* Visibility of the kernel driver node (`/dev/galcore`).
* Presence of the required TIM-VX userspace `.so` files in `/usr/lib/`.
* Presence of TensorFlow Lite delegate bindings.

If any required driver or delegate library is missing, the smoke test will exit with a non-zero code and describe the failure.

