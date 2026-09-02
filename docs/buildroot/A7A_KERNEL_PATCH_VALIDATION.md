# Cubie A7A Kernel Patch Validation Gate

All A7A kernel changes are carried by `project-cubie-a5e/patches/linux/` and consumed by the Buildroot output directory `bld.a7a/`.

> **Rule**: Do not consider a kernel patch ready to commit because it works in `bld.a7a/build/linux-7.1/`. That tree is already patched. Before every commit or series update, Buildroot must apply the complete patch series to a clean Linux 7.1 source tree and build it successfully.

## Authoritative build paths

| Purpose | Path |
|---|---|
| Buildroot output and validation environment | `bld.a7a/` |
| Live, already-patched kernel working tree | `bld.a7a/build/linux-7.1/` |
| A7A Buildroot external tree | `cubie-a5e/project-cubie-a5e/` |
| Kernel patch series | `cubie-a5e/project-cubie-a5e/patches/linux/` |
| A7A Buildroot configuration | `cubie-a5e/project-cubie-a5e/configs/cubie_a7a_defconfig` |

The external configuration sets `BR2_LINUX_KERNEL_PATCH` to the complete `patches/linux/` directory. Buildroot applies the patches in lexical order:

1. `0001-arm64-dts-allwinner-add-sun60i-a733-cubie-a7a.patch`
2. `0002-remoteproc-sunxi-add-allwinner-riscv-remoteproc.patch`
3. `0003-clk-sunxi-ng-add-allwinner-a733-ccu-and-prcm.patch`
4. `0004-pinctrl-sunxi-add-allwinner-a733-pinctrl.patch`
5. `0005-arm64-dts-allwinner-add-a523-remoteproc-and-msgbox.patch`
6. `0006-phy-allwinner-sun4i-usb-use-shared-reset-control.patch`
7. `0007-net-ethernet-stmmac-sun55i-support-a733-dedicated-syscfg.patch`
8. `0008-net-phy-add-maxio-mae0621a-phy-driver.patch`

## Required validation before commit

From the workspace root, run the following Buildroot targets in order:

1. `make -C bld.a7a linux-dirclean`
2. `make -C bld.a7a linux`

`linux-dirclean` removes the unpacked kernel source and its stamps. The subsequent `linux` target re-downloads or reuses the cached Linux 7.1 source, reapplies all eight patches to an unmodified source tree, configures the kernel, builds it with Buildroot's external Bootlin toolchain, and installs the A7A DTB. This is the clean-tree patch-application test.

A successful incremental `linux-rebuild` is useful while iterating, but it **does not replace** the clean-tree validation because it may leave an already-patched working tree in place.

## DTS-specific gate

After the clean Buildroot Linux build succeeds, verify that this file exists:

- `bld.a7a/images/sun60i-a733-cubie-a7a.dtb`

For USB changes, additionally inspect the built DTB to confirm that only intended nodes and regulators are enabled. The current bring-up policy is:

- USB PHY, EHCI, OHCI, and DWC3 nodes remain `status = "disabled"`.
- VBUS regulators `usb0-vbus` (PL2 / `USB0-DRVVBUS`) and `usb1-vbus` (PM5 / `USB_HOST_EN`) must not be `regulator-always-on` while their consumers are disabled.
- Do not enable USB to validate Ethernet or remoteproc.

## Editing and synchronization workflow

1. Make the focused change in `bld.a7a/build/linux-7.1/`.
2. Synchronize new-file patch hunks with:
   `python3 cubie-a5e/project-cubie-a5e/scripts/sync_patches_from_build.py --write`
3. Review the resulting patch diff. The synchronizer updates only `/dev/null` new-file hunks; ordinary modified-file hunks require manual review and hunk-count verification.
4. Run the clean-tree validation above.
5. Record the result, Buildroot log tail, and produced artifacts in `CUBIE_A7A_DEBUG_LOG.md`.

## Permanent patch and RISC-V firmware locations

- **Permanent A7A kernel patches** belong only in `cubie-a5e/project-cubie-a5e/patches/linux/`. `bld.a7a/build/linux-7.1/` is a generated, already-patched Buildroot working tree; changes made there must be synchronized into the external patch series before commit.
- **RISC-V firmware source** belongs in `cubie-a5e/riscv-firmware/`, currently `apps/exampleRiscv/`.
- **RISC-V Buildroot package integration** belongs in `cubie-a5e/project-cubie-a5e/package/riscv-firmware/`. The package is enabled by `BR2_PACKAGE_RISCV_FIRMWARE=y` in `configs/cubie_a7a_defconfig`.
- **A7A output** belongs in `bld.a7a/`. It is the only place Buildroot should build/install the deployable firmware and final image.

After changing RISC-V firmware source, build it for the A7A image with:

1. `make -C bld.a7a riscv-firmware-rebuild`
2. `make -C bld.a7a`

The first target compiles `apps/exampleRiscv/firmware.elf` and `firmware.bin`, then installs them into the target root filesystem as:

- `/lib/firmware/riscv-firmware.elf`
- `/lib/firmware/riscv-firmware.bin`

The second target repacks the complete SD-card image. Validate the installed ELF before deployment at `bld.a7a/target/lib/firmware/riscv-firmware.elf`.

## Failure handling

If `linux-dirclean` followed by `linux` fails while applying a patch, do not repair the live kernel tree only. Fix the patch under `project-cubie-a5e/patches/linux/`, repeat the clean-tree test, and commit the patch only after it applies and builds from scratch.
