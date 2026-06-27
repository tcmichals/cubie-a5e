# Cubie A5E — Copilot Project Context

## What this repo is

Out-of-tree Buildroot external tree for the **Radxa Cubie A5E** (Allwinner A527/T527, 8-core Cortex-A55).
Primary mission: **flight controller platform** with NPU-assisted landing.

## Architecture split

```
Cubie A5E (Linux / this repo)          FPGA (AbstractX repo)
─────────────────────────────          ──────────────────────
High-level flight logic                DSHOT motor output
Mission state machine                  PWM generation/capture
TinyML / NPU inference                 IMU interface
Camera + distance sensor fusion        Hard real-time timing
Navigation / supervisory control
        │                                      │
        └──────── dual SPI (AbstractX) ────────┘
```

AbstractX repo: https://github.com/tcmichals/AbstractX
SPI transport profile: https://github.com/tcmichals/AbstractX/blob/main/docs/ASP_SPI_TRANSPORT.md

## Repo layout

```
buildroot/                             upstream Buildroot (cloned, do NOT edit)
project-cubie-a5e/                     external tree (BR2_EXTERNAL) — edit here
  configs/cubie_a5e_defconfig          primary package/config selection
  board/radxa/cubie_a5e/
    linux.config                       kernel config fragment (PREEMPT_RT enabled)
    u-boot-fragment.config             U-Boot config fragment (overlay support)
    boot.cmd                           U-Boot boot script source
    genimage.cfg                       sdcard.img partition layout
    post-image.sh                      image assembly pipeline
    rootfs-overlay/                    files copied directly into target rootfs
  dts-overlay/allwinner/
    cubie-a5e-flight-stack.dtso        DT overlay (SPI/I2C enablement)
  package/
    aic8800-driver/                    Wi-Fi driver hook
    aic8800-firmware/                  Wi-Fi firmware hook
    sunxi-galcore/                     NPU kernel driver hook (stub — not complete)
    timvx-delegate/                    TIM-VX delegate wiring + npu-smoke-test
  scripts/
    setup-npu-bundle.sh                Helper script to automatically pull NPU libs
docs/                                  organized how-to guides
  buildroot/                           Creating the distribution (OS / bring-up)
  flightcontroller/                    Using the distribution (Guidance / control)
bld/                                   build output (generated, not in repo)
```

## Buildroot external package conventions

- Every custom package has `package/<name>/Config.in` + `package/<name>/<name>.mk`
- Enable packages in `cubie_a5e_defconfig`
- Stubs use `$(eval $(generic-package))` with no source — grow incrementally
- Never modify files under `buildroot/` — upstream source, will be overwritten

## Key enabled packages (defconfig)

- `BR2_PACKAGE_TENSORFLOW_LITE=y` — TFLite inference runtime
- `BR2_PACKAGE_TIMVX_DELEGATE=y` — TIM-VX NPU delegate wiring
- `BR2_PACKAGE_SUNXI_GALCORE=y` — NPU kernel driver (stub)
- `BR2_PACKAGE_PYTHON3=y` + numpy, pyserial — bring-up scripts and ML glue
- `BR2_PACKAGE_LIBGPIOD=y` + tools — GPIO bring-up
- `BR2_PACKAGE_I2C_TOOLS=y` — I2C bus probing
- `BR2_PACKAGE_SPI_TOOLS=y` — SPI bus diagnostics
- `BR2_PACKAGE_LIBV4L=y` + v4l-utils — camera pipeline
- `BR2_PACKAGE_WPA_SUPPLICANT=y` + aic8800 packages — Wi-Fi (AIC8800 chip)
- `BR2_PACKAGE_EUDEV=y` — dynamic /dev, module-driven device management
- `BR2_PACKAGE_DROPBEAR=y` — Dropbear lightweight SSH server
- `CONFIG_PREEMPT_RT=y` in linux.config — realtime kernel baseline

## Device tree / overlay approach

U-Boot `fdt apply` — NOT kernel configfs overlays.

- Base DTB: `sun55i-a527-cubie-a5e.dtb` (kernel intree, untouched)
- Overlay: `project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-flight-stack.dtso` $\rightarrow$ compiled to `.dtbo` by kernel build
- U-Boot loads both into RAM, merges with `fdt apply`, passes merged tree to kernel
- Kernel never knows overlays existed — no `CONFIG_OF_OVERLAY` needed
- Requires in U-Boot: `CONFIG_OF_LIBFDT_OVERLAY=y`, `CONFIG_CMD_FDT=y`

## NPU stack

```
/dev/galcore  (kernel, sunxi-galcore driver — stub in this tree)
     ↓
libovxlib / libtim-vx  (userspace runtime, from prebuilt bundle)
     ↓
TFLite + TIM-VX delegate
     ↓
Python / application code
```

Smoke test on target: `/usr/bin/npu-smoke-test`
Prebuilt bundle path: `BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR` (defaults to `timvx-bundle/` in workspace root)

## Known stubs / active TODOs

- `sunxi-galcore` — kernel NPU driver packaging not yet complete
- TIM-VX runtime bundle — must be supplied externally or fetched via helper script
- AbstractX integration — SPI driver/protocol bring-up pending FPGA hardware

## Build commands (from repo top level)

```bash
# Clone buildroot
git clone https://github.com/buildroot/buildroot.git
# Create output directory
mkdir -p bld
# Configure
PATH=$PWD/bld/bin:$PATH make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig
# Build
PATH=$PWD/bld/bin:$PATH make -C bld
# Flash (replace /dev/sdX)
sudo dd if=$PWD/bld/images/sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
```

## Documentation Map

- `docs/README.md` — index of all guides
- `docs/buildroot/BuildRootHowTo.md` — package management & build commands
- `docs/buildroot/DeviceTreeHowTo.md` — DTB and overlays compilation
- `docs/buildroot/WirelessHowTo.md` — Wi-Fi bring-up
- `docs/buildroot/HowToNPU.md` — NPU drivers, packaging, and setup script
- `docs/flightcontroller/DevelopmentAndDebugging.md` — C++ remote debugging with VS Code / gdbserver
- `docs/buildroot/CameraTesting.md` — Camera bring-up and V4L2 diagnostics

## Real-Time Linux (PREEMPT_RT) & Core Isolation Guidelines

- **Isolate Core 7:** Always configure U-Boot boot arguments with `isolcpus=7 nohz_full=7 rcu_nocbs=7` to reserve CPU Core 7 exclusively for the iNAV flight loop.
- **CPU Governor:** Ensure CPU Core 7 governor is locked to `performance` to prevent latency spikes from clock rate throttling.
- **Lock Address Space:** Always call `mlockall(MCL_CURRENT | MCL_FUTURE)` during initialization to lock all virtual memory pages into RAM and prevent swap/page faults.
- **Pre-fault Stack:** Pre-allocate stack space (e.g. 1MB) and touch all memory pages during initialization to map physical RAM before the loop starts.
- **Real-Time Priority:** Set thread scheduling policy to `SCHED_FIFO` with a priority of `80` (preempts general userspace).
- **Core Pinning:** Explicitly pin the real-time loop thread specifically to CPU Core 7 (`pthread_setaffinity_np`).
- **No Heap Allocations:** Strictly avoid dynamic memory allocations (`malloc`, `free`, `new`, `delete`, or dynamic vector growth) inside the fast real-time loop.
- **Task Isolation (NPU & Media):** Run all heavy background processing (TFLite NPU inference, V4L2 camera capture, video codecs, Wi-Fi telemetry) on CPU Cores 0-6 using standard scheduling (`SCHED_OTHER`). CPU Core 7 must remain reserved *exclusively* for the time-critical iNAV autopilot loop to ensure NPU operations cannot hog resources or impact flight control reliability.

## Coding conventions

- Do not edit `buildroot/` upstream source
- Do not break `cubie_a5e_defconfig` package selections without updating docs
- Mark new stubs explicitly in docs as `(currently stub)`
- Keep `boot.cmd` and `u-boot-fragment.config` in sync
- When adding overlays: update `genimage.cfg` + `boot.cmd` + `defconfig` + `DeviceTreeHowTo.md`
