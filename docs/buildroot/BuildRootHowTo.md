# BuildRootHowTo

This guide explains how this out-of-tree Buildroot project is structured, how custom packages are created, and why each major package group exists (device management/eudev, wireless, NPU, boot chain, flight stack).

## 1) Project layout and ownership

From repo top level:

- `buildroot/` -> upstream Buildroot source
- `project-cubie-a5e/` -> external tree (`BR2_EXTERNAL`)
- `bld/` -> output directory (`O=`)

Key external-tree files:

- `project-cubie-a5e/external.desc` -> registers external tree identity (`CUBIE_A5E`)
- `project-cubie-a5e/external.mk` -> includes all package `.mk` files
- `project-cubie-a5e/Config.in` -> adds external package menu entries
- `project-cubie-a5e/configs/cubie_a5e_defconfig` -> default full-board config

---

## 2) How we create custom packages

Buildroot external packages live under:

- `project-cubie-a5e/package/<pkg-name>/Config.in`
- `project-cubie-a5e/package/<pkg-name>/<pkg-name>.mk`

Creation pattern used in this repo:

1. Add package symbol in `Config.in`
2. Add install/build logic in `.mk`
3. Enable package in `cubie_a5e_defconfig` when required by board baseline

Why this approach:

- Keeps board-specific integrations out of upstream Buildroot core
- Makes package ownership explicit per subsystem
- Lets us grow stubs into full packages incrementally without breaking board bring-up

---

## 3) Why these package groups exist

### Device management (`eudev`)

Purpose: predictable runtime device-node handling for bring-up and service startup order.

Related pieces in defconfig:

- `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y`
- `BR2_PACKAGE_EUDEV=y`

Why we use it:

- Dynamic `/dev` population as devices appear/disappear
- More robust behavior for module-driven devices (Wi-Fi/NPU/other peripherals)
- Better fit than static device tables for iterative hardware bring-up

Companion GPIO userspace stack:

- `libgpiod` + `libgpiod-tools`
- Why: modern character-device GPIO API tooling (`gpiodetect`, `gpioinfo`, `gpioset`, `gpioget`) for board and flight-I/O validation.

### Wireless (AIC8800 + userspace tools)

Purpose: provide field bring-up and telemetry networking.

Related pieces:

- `aic8800-driver` (kernel module packaging hook)
- `aic8800-firmware` (firmware payload hook)
- `wpa_supplicant`, `iw`, `wireless_tools`, `linux-firmware`
- init script: `/etc/init.d/S40network-wifi`

The Wi-Fi init script does:

- `modprobe aic8800_fdrv`
- `wpa_supplicant ...`
- `udhcpc ...`

These tools are included specifically so wireless bring-up can be done entirely on-target without additional debugging images.

### Peripheral bus tools (SPI / I2C / GPIO)

Purpose: board-level sensor and FPGA link bring-up before full flight-stack integration.

Related pieces:

- `spi-tools`
- `i2c-tools`
- `libgpiod` + `libgpiod-tools`

Why we use them:

- quick hardware sanity checks
- register/device probing during integration
- deterministic test procedures for IMU, distance sensor, and FPGA-side interfaces

### Camera and video bring-up

Purpose: validate camera pipeline before TinyML landing-assist logic.

Related pieces:

- `libv4l`
- `v4l-utils`

Why we use them:

- inspect `/dev/video*` device capabilities (`v4l2-ctl`)
- verify format/framerate negotiation early
- isolate camera-driver issues from ML/inference code

### NPU / TinyML

Purpose: enable accelerated inference path for flight-assist ML workloads.

Related pieces:

- `tensorflow-lite`
- `timvx-delegate` (now wired for runtime bundle install + smoke test)
- `sunxi-galcore` (currently stub; kernel-side NPU driver packaging still to be completed)

Current validation entry point:

- `/usr/bin/npu-smoke-test`

### Boot and trusted firmware (`BL31`, often said as "AT31")

Purpose: complete AArch64 boot chain for Cubie A5E.

Related pieces:

- ARM Trusted Firmware-A config in defconfig (`BR2_TARGET_ARM_TRUSTED_FIRMWARE_*`)
- `BR2_TARGET_ARM_TRUSTED_FIRMWARE_BL31=y`
- U-Boot integration uses BL31 artifact in boot flow

If you meant "AT31", this project maps that to **TF-A `BL31`** stage in the boot chain.

### Flight-stack/board integration

Purpose: deterministic board image with overlays and post-image packaging.

Related pieces:

- rootfs overlay: `board/radxa/cubie_a5e/rootfs-overlay`
- post-image pipeline: `board/radxa/cubie_a5e/post-image.sh`
- image layout: `board/radxa/cubie_a5e/genimage.cfg`

---

## 4) Tools and repositories: what and why

### Repositories

- `buildroot/` (upstream Buildroot)
	- Why: stable build system and package ecosystem for reproducible embedded Linux images.
- `project-cubie-a5e/` (this external tree)
	- Why: board-specific configuration/packages without forking upstream Buildroot.
- `AbstractX` (sibling project)
	- Why: FPGA transport/control framework over SPI for real-time offload architecture.
	- Repo: https://github.com/tcmichals/AbstractX

### Core tools in this flow

- `make` + Buildroot (`BR2_EXTERNAL`, `O=`)
	- Why: reproducible image generation with out-of-tree board ownership.
- `genimage` + `dosfstools` + `mtools`
	- Why: assemble boot/rootfs partitions and final `sdcard.img` layout.
- `mkimage` (from host U-Boot tools)
	- Why: compile `boot.cmd` into `boot.scr` for boot script execution.
- `dd`
	- Why: straightforward SD-card image write for board bring-up.

### Runtime tools in target image

- `wpa_supplicant`, `iw`, `udhcpc`
	- Why: wireless bring-up and DHCP networking.
- `wireless_tools`
	- Why: compatibility utilities for Wi-Fi diagnostics in mixed environments.
- `spi-tools`, `i2c-tools`
	- Why: low-level peripheral bus diagnostics during board bring-up.
- `libgpiod`, `libgpiod-tools`
	- Why: modern GPIO control/inspection aligned with kernel chardev API.
- `python3`, `numpy`, `pyserial`
	- Why: rapid bring-up scripts, telemetry tooling, and TinyML integration glue.
- `libv4l`, `v4l-utils`
	- Why: camera/video node probing and format validation.
- `tensorflow-lite` (+ TIM-VX delegate path)
	- Why: inference runtime baseline and hardware-acceleration path.

---

## 5) Get Buildroot

If not already cloned:

- `git clone https://github.com/buildroot/buildroot.git`

## 6) Configure and build

From the repository top level:

1. Create an output directory:
   ```bash
   mkdir -p bld
   ```
2. Configure Buildroot for Cubie A5E:
   ```bash
   PATH=$PWD/bld/bin:$PATH make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig
   ```
3. Build the full image:
   ```bash
   PATH=$PWD/bld/bin:$PATH make -C bld
   ```

## 7) Rebuild

To rebuild components after editing code or changing options, simply run:
```bash
PATH=$PWD/bld/bin:$PATH make -C bld
```

To reset the entire configuration and build output:
* Remove the `bld/` directory.
* Run the configuration and build steps again.

## 8) SD card image output

The final bootable SD card image will be created at:
* `bld/images/sdcard.img`

To write it to an SD card (replace `/dev/sdX` with your card's device node):
```bash
sudo dd if=$PWD/bld/images/sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

## 9) TIM-VX NPU Prebuilt Integration

By default, the build looks for precompiled NPU driver libraries at the workspace root directory in `timvx-bundle/`. 

If you store the bundle elsewhere on your host machine, you can override the path during compilation:
```bash
PATH=$PWD/bld/bin:$PATH make -C bld \
    BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR=/home/tcmichals/timvx-release
```

Once loaded onto the target, you can validate the NPU stack using the onboard smoke test:
```bash
/usr/bin/npu-smoke-test
```

## 10) Board references

- Product: https://radxa.com/products/cubie/a5e
- Docs: https://docs.radxa.com/en/cubie/a5e
- Hardware/interface section: https://docs.radxa.com/en/cubie/a5e#5-interface-description

## 11) Design intent summary

- FPGA side handles hard real-time/control-timing domains (DSHOT/PWM/IMU timing path).
- A5E Linux side handles mission logic, networking, and ML inference orchestration.
- Buildroot external tree keeps this integration reproducible and board-specific.

---

## 12) U-Boot Environment Configuration (`uboot.env`)

U-Boot allows configuring runtime parameters using a persistent environment file named `uboot.env` located on the boot partition of the SD card.

### Default Environment File
In this project, a default U-Boot environment is compiled and placed in the FAT partition:
* **Source**: `project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt`
* **Binary Output**: `uboot.env` (packaged into the boot partition by `genimage.cfg`).

### How to use and customize:
1. **Modify the Source**: Edit the text file `project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt` to add, remove, or modify environment variables (e.g., custom boot arguments, delay times, or boot commands).
2. **Rebuild**: Rebuilding the project will automatically compile `uboot-env.txt` into `uboot.env` using `mkenvimage` and package it into the final `sdcard.img`.
3. **Target modifications**: You can view and edit these variables directly on the target's U-Boot command prompt using the `printenv`, `setenv`, and `saveenv` commands.

---

## 13) Customizing the Linux Kernel Configuration

This project configures the Linux kernel using the standard ARM64 architecture default configuration (`defconfig`) as a base, and overlays custom configuration options on top of it using a **config fragment** file.

* **Base Configuration**: Standard ARM64 architecture defaults (enabling the serial console, MMC, ext4, USB, networks, etc.).
* **Config Fragment Source**: `project-cubie-a5e/board/radxa/cubie_a5e/linux.config`. This file overlays specific flight-controller requirements (e.g. `CONFIG_PREEMPT_RT=y` for real-time scheduling, high-resolution timers, and a `1000Hz` tick rate).

### How to modify the kernel config:
1. **Edit the Fragment**: Open the text file `project-cubie-a5e/board/radxa/cubie_a5e/linux.config` and append or modify the options you need (e.g., `CONFIG_SOME_DRIVER=y` or `# CONFIG_OTHER_DRIVER is not set`).
2. **Clean and Rebuild**: To force Buildroot to clean and rebuild the kernel with your updated fragment:
   ```bash
   make -C bld linux-dirclean
   PATH=$PWD/bld/bin:$PATH make -C bld
   ```


