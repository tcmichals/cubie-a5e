# Radxa Cubie A5E (Allwinner A527 / T527) - Bring-Up & Feature Roadmap

This document tracks completed milestones, active tasks, and future feature bring-up specifically for the **Radxa Cubie A5E** (Allwinner A527 / T527 Octa-Core ARM Cortex-A55 @ 1.8 GHz, XuanTie E907 RISC-V @ 200 MHz, 2 TOPS NPU, 4K VPU Video Engine).

---

## 1. Verified Silicon Milestones (Completed)

- [x] **XuanTie E907 Core Bootstrap & Lifecycle**: Clean `start` -> `stop` -> `start` cycles via Linux RemoteProc sysfs; zero `/dev/mem` register hacks.
- [x] **Native Kernel Clocks & Resets**: Converted to standard CCU `devm_clk_get()` (`bus`, `core`, `sram`) and `devm_reset_control_get()`.
- [x] **Debugfs Telemetry**: Live ASCII, binary struct, and hex dump logging via `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
- [x] **Hardware Exception Trapping (`testCrash.elf`)**: Machine-mode trap handler captures register autopsy to SRAM (`0xDEADF00D`) without crashing ARM Linux host.
- [x] **VirtIO RPMsg over DDR DRAM**: Hardware Mailbox Channel 8 doorbells, PREEMPT_RT deferred workqueue (`schedule_work`), Name Service announcement, and `/dev/rpmsg0` character device.
- [x] **Standardized 512-Byte Apples-to-Apples Benchmarks** (1,000 packets on live hardware, 0 timeouts):
  - `ping_shm` (Direct SRAM SPSC): **14.59 $\mu\text{s}$ avg RTT**, 63,022 msgs/s, 61.54 MB/s.
  - `ping_rpmsg` (Linux VirtIO RPMsg): **175.64 $\mu\text{s}$ avg RTT**, 5,671 msgs/s, 5.37 MB/s.
  - `ping_dram` (Hybrid SRAM/DDR Carveout): **191.84 $\mu\text{s}$ avg RTT**, 4,710 msgs/s, 4.60 MB/s.
- [x] **Automated RemoteProc Test Suite**: `testBasic`, `testStringBinaryTrace0`, `testCrash`, and `testPingRpmsg` passing 100% via `python3 /usr/bin/run_tests.py`.
- [x] **Kernel Patch Validation Gate**: `tools/validate_kernel_patches.py` asserting clean dry-run and byte identity against `linux-7.1`.
- [x] **Upstream RFC Patch Series (`patches-upstream-rfc/`)**: Complete 5-patch series + cover letter passing `checkpatch.pl` with 0 errors.

---

## 2. Active Feature Bring-Up: Camera & Video Encoding (VPU / Cedrus)

* **Goal**: Capture live camera streams (MIPI-CSI or parallel) and encode them to H.264/H.265 video using the SoC's hardware Video Engine (VEU / Cedrus) with zero CPU overhead.

- [ ] **Camera Sensor Interface Bring-Up**:
  - [ ] Configure MIPI-CSI / parallel camera interface in Device Tree (`sun55i-a523.dtsi` / overlay).
  - [ ] Integrate sensor driver (e.g. OV5640, IMX219, or USB UVC camera input).
  - [ ] Verify V4L2 capture device node `/dev/video0` with `v4l2-ctl --list-formats-ext`.
  - [ ] Test raw frame capture: `v4l2-ctl --stream-mmap --stream-count=100 -d /dev/video0`.
- [ ] **Hardware Video Encoding (VEU / Cedrus Stateless Encoder)**:
  - [ ] Enable `CONFIG_VIDEO_SUNXI_CEDRUS=y` in `linux.config`.
  - [ ] Extend Cedrus driver with H.264 encoding support (`v4l2_m2m` stateless memory-to-memory architecture).
  - [ ] Verify encoder device node `/dev/video-dec` or `/dev/video-enc`.
  - [ ] Test zero-copy pipeline: Pass V4L2 camera `dma-buf` directly to hardware encoder without CPU `memcpy`:
    `gst-launch-1.0 v4l2src device=/dev/video0 ! v4l2h264enc ! h264parse ! mp4mux ! filesink location=test.mp4`
  - [ ] Measure CPU load and frame latency during 1080p30 / 1080p60 encoding.

---

## 3. Active Feature Bring-Up: NPU Acceleration (2 TOPS TinyML)

* **Goal**: Run hardware-accelerated deep learning inference (TensorFlow Lite, ONNX, YOLOv8) on the onboard 2 TOPS Neural Processing Unit (Vivante VIP9000).

- [ ] **Kernel Driver & DRM Device**:
  - [ ] Verify `CONFIG_DRM_ETNAVIV=y` in `linux.config`.
  - [ ] Confirm NPU DT node in `sun55i-a523.dtsi` (`npu: npu@7122000`) with clocks, resets, and power domains.
  - [ ] Verify driver probe on target: `dmesg | grep -i etnaviv`.
  - [ ] Confirm DRM render node appears: `ls -l /dev/dri/renderD128`.
- [ ] **Userspace NPU Runtime & TFLite Delegate**:
  - [ ] Build Mesa with Etnaviv gallium driver and Teflon delegate (`BR2_PACKAGE_MESA3D_TEFLON=y` / `libteflon.so`).
  - [ ] Build `tflite_runtime` (TensorFlow Lite runtime) in Buildroot.
  - [ ] Test sample quantized INT8 model with Teflon delegate:
    ```python
    import tflite_runtime.interpreter as tflite
    delegate = tflite.load_delegate("/usr/lib/libteflon.so")
    interpreter = tflite.Interpreter(model_path="model_quant.tflite", experimental_delegates=[delegate])
    interpreter.allocate_tensors()
    ```
  - [ ] Benchmark inference latency: NPU-accelerated vs CPU-only fallback.

---

## 4. XuanTie E907 Advanced RemoteProc & IPC Tasks

- [ ] **Task 1: Pure On-Chip SRAM VirtIO RPMsg Benchmark (`testPingRpmsgSram`)**:
  - [ ] Set fixed `.da = 0x40000000` in SRAM Space 1 in `resource_table.c` for vrings and payload message buffers (instead of dynamic DDR CMA `FW_RSC_ADDR_ANY`).
  - [ ] Verify `sunxi_rproc.c` handles SRAM mapping without invoking `dma_alloc_coherent()`.
  - [ ] Run benchmark `ping_rpmsg -n 1000 -s 496` over pure on-chip SRAM on live hardware.
  - [ ] Complete the 4-tier architectural performance comparison matrix:
    - Pure SRAM SPSC Polling (`ping_shm`): **14.59 $\mu\text{s}$** avg RTT
    - VirtIO RPMsg in On-Chip SRAM (Projected): **~40–50 $\mu\text{s}$** avg RTT
    - VirtIO RPMsg in DDR CMA (`ping_rpmsg`): **175.64 $\mu\text{s}$** avg RTT
    - Hybrid SRAM/DDR Carveout (`ping_dram`): **191.84 $\mu\text{s}$** avg RTT

- [ ] **Task 2: Automatic Core Recovery & Restart After Crash (`rproc_report_crash`)**:
  - [ ] **Crash Notification Mechanism**: In `testCrash.elf` M-mode trap handler, after writing the autopsy to SRAM (`0xDEADF00D`), trigger a mailbox doorbell alert to the ARM host before halting in `wfi`.
  - [ ] **Kernel Driver Handler**: In `sunxi_rproc.c`, connect the mailbox interrupt to `rproc_report_crash(priv->rproc, RPROC_FATAL_ERROR)`.
  - [ ] **Target Validation with `recovery = enabled`**:
    - Enable recovery: `echo enabled > /sys/class/remoteproc/remoteproc0/recovery`.
    - Trigger crash by loading `testCrash.elf` (illegal instruction / bus error).
    - Verify ARM kernel logs `remoteproc remoteproc0: crash detected in sunxi-rproc: type fatal error`.
    - Verify RemoteProc core framework automatically tears down virtio devices, asserts reset, reloads default firmware ELF, and restarts the core with zero ARM host kernel panic or manual reboot.
    - Verify ping RPMsg immediately recovers after core reboot.

- [ ] **Task 3: System Power Management (Suspend / Resume & Low-Power Sleep)**:
  - [ ] **Driver PM Callbacks**: Implement `dev_pm_ops` (`sunxi_rproc_suspend` and `sunxi_rproc_resume`) in `sunxi_rproc.c`.
  - [ ] **Mode A (Clean Core Stop on Suspend)**:
    - On `echo mem > /sys/power/state`, gracefully stop E907, save state, and restart post-wake.
  - [ ] **Mode B (Always-On Sensor Hub in SRAM)**:
    - Test if on-chip SRAM Space 0/1 retains state during SoC deep sleep.
    - Keep E907 running in `wfi` in SRAM while Cortex-A55 cores sleep; evaluate waking the host via mailbox interrupt.
  - [ ] **Target Validation**: Verify system suspend/resume cycles (`echo mem > /sys/power/state`) while running RPMsg ping traffic; verify no bus lockups, clock stalls, or memory corruption.

- [ ] **Task 4: Multi-Channel Concurrency & High-Load Stress Testing**:
  - [ ] Implement multi-endpoint firmware announcing multiple RPMsg channels (`rpmsg-ping`, `rpmsg-telemetry`, `rpmsg-control`).
  - [ ] Run multi-threaded stress test with 10+ concurrent worker threads hammering `/dev/rpmsg0`..`/dev/rpmsgN` under 100% CPU load (`stress-ng`).
  - [ ] Measure lock contention, PREEMPT_RT latency jitter, and vring buffer exhaustion.

- [ ] **Task 5: Cadence Tensilica HiFi4 DSP RemoteProc Bring-Up**:
  - [ ] Map HiFi4 DSP memory window (`0x00020000`, 320 KB) and clock/reset controls in `sunxi_rproc.c`.
  - [ ] Add DT binding for DSP remoteproc instance in `sun55i-a523.dtsi`.
  - [ ] Compile and validate minimal DSP bring-up firmware ELF.

---

## 5. Upstream Linux Kernel Submission (RFC Ready)

- [x] Patch 1: `dt-bindings: mailbox: add Allwinner sun55i msgbox schema`
- [x] Patch 2: `mailbox: sun55i: add Allwinner sun55i/A523 msgbox driver`
- [x] Patch 3: `dt-bindings: remoteproc: add Allwinner sun55i rproc schema`
- [x] Patch 4: `remoteproc: sunxi: add Allwinner XuanTie RISC-V remoteproc driver`
- [x] Patch 5: `arm64: dts: allwinner: sun55i: add msgbox and remoteproc nodes`
- [x] Cover Letter: `patches-upstream-rfc/0000-cover-letter.patch` (72 char line wrap, 0 errors).
- [ ] Submit RFC series to `linux-sunxi@lists.linux.dev` and `linux-remoteproc@vger.kernel.org` via `git send-email`.
