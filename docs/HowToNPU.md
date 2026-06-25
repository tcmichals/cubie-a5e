# HowToNPU

This guide explains how NPU support is expected to work on this Cubie A5E Buildroot tree.

## 1) Current stack in this repo

- Kernel-side NPU path: **`galcore`** device (`/dev/galcore`)
- Userspace runtime: TIM-VX / OpenVX shared libraries
- ML framework path: TensorFlow Lite + TIM-VX delegate

In this tree:

- `BR2_PACKAGE_TENSORFLOW_LITE=y` is enabled.
- `BR2_PACKAGE_TIMVX_DELEGATE=y` is wired to install helper tooling and optional prebuilt runtime artifacts.
- `BR2_PACKAGE_SUNXI_GALCORE=y` is currently still a stub package.

## 2) Provide TIM-VX runtime bundle

Set Buildroot variable:

- `BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR=/absolute/path/to/timvx-bundle`

Expected bundle layout:

- `<bundle>/lib/*.so*` (required)
- `<bundle>/bin/*` (optional)

Install destination on target:

- libs -> `/usr/lib`
- binaries -> `/usr/bin`

## 3) Build image with NPU runtime bundle

From repo top level:

1. `make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig`
2. `make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR=/absolute/path/to/timvx-bundle`

## 4) Validate on target

Run:

- `/usr/bin/npu-smoke-test`

This checks:

- `/dev/galcore`
- common TIM-VX/OpenVX delegate libs in `/usr/lib`
- optional TensorFlow Lite binary presence
- optional Python module availability

If no delegate/runtime libs are found, the smoke test exits non-zero.

## 5) Realtime validation note

This tree enables PREEMPT_RT baseline kernel options:

- `CONFIG_PREEMPT_RT=y`
- `CONFIG_HIGH_RES_TIMERS=y`
- `CONFIG_HZ_1000=y`

Use this as the basis for deterministic control-path validation before autonomous landing behavior is enabled.

## 6) Flight-stack context

Planned split:

- FPGA: DSHOT/PWM/IMU timing-critical path
- A5E Linux: mission logic + NPU inference

AbstractX references:

- https://github.com/tcmichals/AbstractX
- https://github.com/tcmichals/AbstractX/blob/main/docs/ASP_SPI_TRANSPORT.md
