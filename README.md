# Cubie A5E Buildroot (Out-of-Tree)

This repository provides a **Buildroot external tree** for the **Radxa Cubie A5E**.

Goal: produce a working Buildroot image using this out-of-tree project.

## Documentation

Detailed how-to guides explaining what every piece does and why live in the [`docs/`](docs/README.md) folder.

Start there if you are new to this project — it explains packages, device tree, wireless, NPU, and the build system in plain language.

## AI assistant context

This repo includes project-context files so AI coding assistants start with full architecture awareness:

- VS Code Copilot: [`.github/copilot-instructions.md`](.github/copilot-instructions.md)
- Cursor / antigravity IDE: [`.cursorrules`](.cursorrules)

Both files contain the architecture split, package conventions, device tree approach, known stubs, and coding rules.

## Project goals

- Primary goal: use the Cubie A5E as a **flight controller** platform.
- Enable the board's **NPU** in the software stack so Python-based AI/ML workloads can use hardware acceleration (not CPU-only inference).

## Flight-stack architecture plan (A5E + FPGA)

Planned control architecture:

- **Cubie A5E (Linux side)**: high-level flight logic, navigation, mission state machine, TinyML/NPU workloads.
- **FPGA side**: deterministic low-level I/O and timing-critical functions:
	- DSHOT motor output generation
	- PWM generation/capture
	- IMU interface and fast sensor timing domain

Interface plan:

- Use **dual SPI links** between Cubie A5E and FPGA.
- Integrate through the **AbstractX framework**:
	- Repo: <https://github.com/tcmichals/AbstractX>
	- SPI transport profile: <https://github.com/tcmichals/AbstractX/blob/main/docs/ASP_SPI_TRANSPORT.md>
	- Typical local sibling path: `../AbstractX`

Design intent:

- Keep hard real-time behavior in FPGA.
- Keep adaptive/AI logic and supervisory control on A5E.
- Exchange compact state/control packets across SPI with explicit timing and watchdog/failsafe behavior.

## NPU / TinyML status in this tree

- `BR2_PACKAGE_TENSORFLOW_LITE=y` is enabled in `cubie_a5e_defconfig`.
- `BR2_PACKAGE_TIMVX_DELEGATE=y` installs a target smoke-test tool and can copy TIM-VX runtime artifacts from a host-side prebuilt bundle.
- `BR2_PACKAGE_SUNXI_GALCORE=y` is still a stub package in this tree.

### TIM-VX delegate runtime wiring

`timvx-delegate` now supports a configurable prebuilt bundle path:

- Buildroot option: `BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR`
- Expected bundle layout:
	- `<bundle>/lib/*.so*` (required)
	- `<bundle>/bin/*` (optional)

Example build using a local prebuilt bundle path:

```bash
make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e \
	BR2_PACKAGE_TIMVX_DELEGATE_PREBUILT_DIR=/absolute/path/to/timvx-bundle
```

At install time, libraries are copied into `/usr/lib` and executables into `/usr/bin`.

### On-target NPU smoke test

This tree installs:

- `/usr/bin/npu-smoke-test`

Run on target:

```bash
/usr/bin/npu-smoke-test
```

The script checks:

- kernel device visibility (`/dev/galcore`)
- presence of known delegate/runtime libs under `/usr/lib`
- optional TensorFlow Lite binary presence
- optional Python module availability

If no TIM-VX/OpenVX delegate libs are found, the script exits non-zero.

## Landing-assist concept (camera + distance sensor + NPU)

One target use case is **assisted landing**:

1. Camera provides visual target/marker detection.
2. Distance sensor (ToF/LiDAR/sonar) provides altitude and final approach confidence.
3. TinyML model (int8) estimates landing-zone quality or marker pose.
4. Flight logic fuses vision + range data and reduces descent rate near touchdown.

Keep the control path deterministic: run low-level attitude/rate loops separately, and use ML output only as a bounded guidance input.

## Minimal TinyML example (bring-up path)

Start simple: run inference at low rate (e.g., 5–10 Hz) and log only confidence + altitude before commanding motion.

```python
# Minimal landing-assist logic sketch (integration example)
while True:
	frame = camera.read()
	altitude_m = distance_sensor.read_meters()

	# TODO: replace with real TFLite + TIM-VX delegate inference call
	landing_confidence = model_infer(frame)  # 0.0 .. 1.0

	if altitude_m < 1.0 and landing_confidence > 0.85:
		controller.set_vertical_speed(-0.15)  # slow final descent
	elif altitude_m < 0.3:
		controller.set_vertical_speed(-0.05)  # flare / touchdown phase
	else:
		controller.set_vertical_speed(-0.30)  # nominal descent
```

Recommended sequence:

1. Validate sensor I/O and timestamps.
2. Validate CPU inference first (functional baseline).
3. Enable TIM-VX delegate path.
4. Compare CPU vs NPU latency and verify identical model outputs within tolerance.
5. Gate autonomous landing behind safety checks and manual override.

## Real-time kernel intent (PREEMPT_RT)

The kernel config in this tree includes a realtime validation baseline:

- `CONFIG_PREEMPT_RT=y`
- `CONFIG_HIGH_RES_TIMERS=y`
- `CONFIG_HZ_1000=y`

Goal: boot and validate realtime behavior for control/transport workloads (especially SPI-linked FPGA interaction) before enabling autonomous behaviors.

## Board links

- Radxa Cubie A5E product page: <https://radxa.com/products/cubie/a5e>
- Radxa Cubie A5E documentation: <https://docs.radxa.com/en/cubie/a5e>
- Cubie A5E hardware/interface documentation: <https://docs.radxa.com/en/cubie/a5e#5-interface-description>

## Repository layout

- `buildroot/` → upstream Buildroot source tree (cloned locally)
- `project-cubie-a5e/` → this external tree (`BR2_EXTERNAL`)

## Build instructions (out-of-tree)

From the repository **top level** (the directory containing `project-cubie-a5e/`):

1. Pull Buildroot (if not already present):

   ```bash
   git clone https://github.com/buildroot/buildroot.git
   ```

2. Create an output directory (recommended):

	```bash
	mkdir -p bld
	```

3. Configure Buildroot for Cubie A5E using the external tree:

	```bash
	make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig
	```

4. Build:

	```bash
	make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e
	```

## Rebuild workflow

After the initial configuration, rebuild with:

```bash
make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e
```

If you need to reset configuration, remove `bld/` and run the configure step again.

## Flashing the SD card image (`dd`)

After a successful build, the SD card image is:

- `bld/images/sdcard.img`

From the repository top level:

1. Insert the SD card and identify its device name (for example `/dev/sdX` or `/dev/mmcblk0`).
2. Unmount any auto-mounted SD card partitions.
3. Write the image with `dd`:

	```bash
	sudo dd if=$PWD/bld/images/sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
	sync
	```

4. Remove and reinsert the SD card, then boot the board.

> [!WARNING]
> Double-check `of=/dev/...` before running `dd`. Using the wrong device will overwrite data on that drive.

