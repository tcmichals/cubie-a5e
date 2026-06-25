# Docs Index

This folder is the documentation front door for the Cubie A5E out-of-tree Buildroot project.

## What each doc covers

- `BuildRootHowTo.md`
  - Explains package intent and ownership
  - Explains how external packages are created (`Config.in` + `.mk`)
  - Explains why tool groups are enabled (eudev, wireless, SPI/I2C/GPIO tools, camera/video, Python, NPU)
  - Includes build/rebuild/image-generation flow

- `HowToNPU.md`
  - Explains NPU stack path (`galcore` + TIM-VX/OpenVX + TFLite delegate)
  - Documents TIM-VX prebuilt runtime bundle integration
  - Documents `/usr/bin/npu-smoke-test` validation
  - Captures realtime/kernel validation context

- `WirelessHowTo.md`
  - Documents Wi-Fi bring-up flow (`aic8800` + `wpa_supplicant` + DHCP)
  - Documents `/etc/init.d/S40network-wifi` usage
  - Provides quick diagnostics and common failure modes

- `DeviceTreeHow.md`
  - **Two-tier guide**: plain-language intro at the top, deep-dive at the bottom
  - Plain section: what a device tree is, what an overlay is, how to edit the overlay text file
  - Comparison table: Pi firmware vs BeagleBone kernel configfs vs U-Boot `fdt apply` (why we chose U-Boot)
  - Documents required U-Boot config options (`CONFIG_OF_LIBFDT_OVERLAY`, `CONFIG_CMD_FDT`, `CONFIG_CMD_BOOTI`, etc.)
  - Documents `sdcard.img` partition layout and where DT/DTB/DTBO files live
  - Covers how to add and update overlays without touching base DTB
  - Deep-dive section: full boot chain ASCII diagram, RAM memory map, `fdt apply` internals, annotated DTS syntax, on-target verification commands

## Source-of-truth project files (what they do)

### Buildroot external tree core

- `project-cubie-a5e/external.desc`
  - Registers external tree identity (`CUBIE_A5E`)
- `project-cubie-a5e/external.mk`
  - Includes all external package makefiles
- `project-cubie-a5e/Config.in`
  - Adds package menu entries for external packages
- `project-cubie-a5e/configs/cubie_a5e_defconfig`
  - Primary package/config selection baseline for this board

### Board integration

- `project-cubie-a5e/board/radxa/cubie_a5e/linux.config`
  - Kernel config fragment/baseline (including realtime intent options)
- `project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh`
  - Post-build image assembly orchestration
- `project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg`
  - Partition/image layout including `sdcard.img`
- `project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/`
  - Files copied directly into target rootfs

### External packages

- `project-cubie-a5e/package/aic8800-driver/`
  - AIC8800 driver package hook
- `project-cubie-a5e/package/aic8800-firmware/`
  - AIC8800 firmware package hook
- `project-cubie-a5e/package/sunxi-galcore/`
  - NPU kernel-side package hook (currently stub)
- `project-cubie-a5e/package/timvx-delegate/`
  - TIM-VX delegate package wiring (runtime bundle copy + smoke-test install)

## Why we document this way

Goals:

1. Keep board bring-up reproducible.
2. Keep package ownership obvious.
3. Separate “what to run” from “why this exists”.
4. Make flight-controller architecture intent explicit (A5E + FPGA split).

Documentation style rules used here:

- Keep commands copy/paste ready.
- List package symbols when possible.
- Tie every major tool/package to a short purpose statement.
- Prefer concrete file paths over vague references.
- Mark stubs/TODOs explicitly so status is honest.

## Update workflow

When adding or changing packages/config:

1. Update `cubie_a5e_defconfig` (package selection)
2. Update package `Config.in` / `.mk` if behavior changed
3. Update relevant docs in this folder
4. Keep `README.md` high-level and point to docs for details

For major architecture changes, update all of:

- root `README.md`
- `docs/BuildRootHowTo.md`
- feature-specific doc (`HowToNPU.md`, `WirelessHowTo.md`, or `DeviceTreeHow.md`)

For device tree / overlay / U-Boot boot flow changes, update:

- `docs/DeviceTreeHow.md` (both plain-language and deep-dive sections as needed)
- `project-cubie-a5e/board/radxa/cubie_a5e/u-boot-fragment.config` if U-Boot config changed
- `project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd` if boot script changed
- `project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg` if partition layout changed

## Related external references

- Radxa Cubie A5E product: https://radxa.com/products/cubie/a5e
- Radxa Cubie A5E docs: https://docs.radxa.com/en/cubie/a5e
- AbstractX repository: https://github.com/tcmichals/AbstractX
- AbstractX SPI transport profile: https://github.com/tcmichals/AbstractX/blob/main/docs/ASP_SPI_TRANSPORT.md
