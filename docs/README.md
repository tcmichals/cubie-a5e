# Documentation Index

This folder is the documentation front door for the Cubie A5E flight controller project. The project is divided into two distinct areas: the **Buildroot OS base system** and the **Flight Controller software stack**.

---

## 1. Buildroot OS & Board Bring-Up (`docs/buildroot/`)

These documents cover the base operating system build, hardware interfaces, and kernel configurations.

* **[Buildroot System How-To](buildroot/BuildRootHowTo.md)**
  * Details out-of-tree package development (`Config.in` and `.mk` files).
  * Explains tool groups (eudev, SPI/I2C/GPIO tools, camera/video, Python, NPU).
  * Outlines the build/rebuild/image-generation flow.
* **[Device Tree & Overlay Guide](buildroot/DeviceTreeHowTo.md)**
  * Details how overlays are loaded using U-Boot (`fdt apply`) and why it is chosen over other architectures.
  * Documents the `sdcard.img` partition layout and overlay compilation commands.
* **[Wireless Configuration How-To](buildroot/WirelessHowTo.md)**
  * Documents Wi-Fi bring-up flow (`aic8800` driver + firmware + `wpa_supplicant`).
  * Describes network startup scripts (`/etc/init.d/S40network-wifi`) and wireless diagnostics.
* **[NPU Packaging & Driver Setup](buildroot/HowToNPU.md)**
  * Explains the NPU graphics driver/runtime stack (`galcore` + TIM-VX/OpenVX + TFLite delegate).
  * Documents integrating the prebuilt TIM-VX runtime binary bundle.
  * Explains running the on-target `/usr/bin/npu-smoke-test` validation script.

---

## 2. Flight Controller & Application Stack (`docs/flightcontroller/`)

These documents cover flight stack architecture, FPGA integration, and intelligent TinyML/NPU applications.

* **[Flight Controller Architecture & AbstractX](flightcontroller/ArchitectureAndAbstractX.md)**
  * Details the split-responsibility model between the high-level Linux A5E and the real-time FPGA.
  * Explains SPI links, timing domains, safety watchdogs, and the AbstractX integration.
* **[Vision-Based Landing Assist](flightcontroller/LandingAssistML.md)**
  * Details the landing-assist guidance system, sensor fusion (camera + ToF rangefinder), and autonomous safety gating.
  * Provides the reference Python TinyML inference loop.
* **[C/C++ Development and Debugging Guide](flightcontroller/DevelopmentAndDebugging.md)**
  * Documents cross-compiling application code using the Buildroot toolchain.
  * Explains remote debugging using VS Code task scripts (automating scp and spawning remote `gdbserver`).

---

## Source-of-Truth Project Files

### Buildroot External Tree Core
* `project-cubie-a5e/external.desc` — Registers the external tree identity (`CUBIE_A5E`).
* `project-cubie-a5e/external.mk` — Includes all external package makefiles.
* `project-cubie-a5e/Config.in` — Adds package menu entries for external packages.
* `project-cubie-a5e/configs/cubie_a5e_defconfig` — Primary configuration and package selection for this board.

### Board Integration
* `project-cubie-a5e/board/radxa/cubie_a5e/linux.config` — Kernel configuration fragment (including Real-Time Kernel flags).
* `project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh` — Orchestrates post-build image assembly.
* `project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg` — Configuration for partition layout of the final `sdcard.img`.
* `project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/` — Files copied directly into the target filesystem at build time.

### External Packages
* `project-cubie-a5e/package/aic8800-driver/` — AIC8800 Wi-Fi/BT kernel driver.
* `project-cubie-a5e/package/aic8800-firmware/` — AIC8800 firmware binaries.
* `project-cubie-a5e/package/sunxi-galcore/` — NPU driver kernel package.
* `project-cubie-a5e/package/timvx-delegate/` — TIM-VX delegate integration (copies host runtime bundle and NPU smoke test tool).
