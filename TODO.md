# Cubie Project Master Roadmap & Task Dashboard

This is the **single centralized source of truth** for all tasks, hardware bring-up roadmaps, and software milestones across the entire repository.

* **Target Hardware 1**: **Radxa Cubie A5E** (Allwinner A527 / T527)
* **Target Hardware 2**: **Radxa Cubie A7A** (Allwinner A733)

> **Rule**: Do NOT create secondary `TODO.md`, `TODO_*.md`, or nested tracking files in subdirectories. All tasks must be recorded and updated in this root document.

---

## Executive Board Status Matrix

| Board Platform | SoC / Architecture | Co-Processor | Current Status | Primary Active Milestones |
| :--- | :--- | :--- | :--- | :--- |
| **Radxa Cubie A5E** | Allwinner A527 / T527<br>(8× Cortex-A55 @ 1.8 GHz) | XuanTie E907<br>(200 MHz, RV32IMAFDC) | **Production Bring-Up**<br>• RemoteProc & Mailbox RFC Ready<br>• Sub-15 $\mu$s IPC Verified | 1. Camera Capture & VPU Encoding<br>2. 2 TOPS NPU (Etnaviv/Teflon)<br>3. Restart After Crash & Suspend/Resume |
| **Radxa Cubie A7A** | Allwinner A733<br>(4× A76 + 4× A55) | XuanTie E902<br>(200 MHz, RV32EMC) | **Active Silicon Bring-Up**<br>• Hub & Wi-Fi Power Configured<br>• U-Boot Ethernet Verified | 1. USB Hub & Wi-Fi Enumeration<br>2. GMAC210 TX DMA Watchdog Fix<br>3. E902 Dual-Mode RemoteProc |

---

# Part I: Radxa Cubie A5E (Allwinner A527 / T527)

## 1. Verified Silicon Milestones (Completed)

- [x] **XuanTie E907 Core Bootstrap & Dynamic Switching**: Clean `start` -> `stop` -> `start` cycles via Linux RemoteProc sysfs; zero `/dev/mem` register hacks.
- [x] **Native Kernel Clocks & Resets**: Converted to standard CCU `devm_clk_get()` (`bus`, `core`, `sram`) and `devm_reset_control_get()`.
- [x] **Debugfs Telemetry**: Live ASCII, binary struct, and hex dump logging via `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
- [x] **Hardware Exception Trapping (`testCrash.elf`)**: Machine-mode trap handler captures register autopsy to SRAM (`0xDEADF00D`) without crashing ARM Linux host.
- [x] **VirtIO RPMsg over DDR DRAM**: Hardware Mailbox Channel 8 doorbells, PREEMPT_RT deferred workqueue (`schedule_work`), Name Service announcement, and `/dev/rpmsg0` character device.
- [x] **Standardized 512-Byte Apples-to-Apples Benchmarks** (1,000 packets on live hardware, 0 timeouts):
  - `ping_shm` (Direct SRAM SPSC, C++): **14.47 $\mu\text{s}$ avg RTT**, 63,139 msgs/s, 61.66 MB/s (100% success, 0 corruptions).
  - `ping_uio` (Lite-libmetal UIO, C++): **14.55 $\mu\text{s}$ avg RTT**, 60,498 msgs/s (100% success, 0 corruptions).
  - `ping_uio.py` (Lite-libmetal UIO, Python): **180.79 $\mu\text{s}$ avg RTT**, 4,480 msgs/s (100% success, 0 corruptions).
  - `ping_rpmsg` (Linux VirtIO RPMsg, C++): **182.43 $\mu\text{s}$ avg RTT**, 5,457 msgs/s (100% success, 0 corruptions).
  - `ping_rpmsg.py` (Linux VirtIO RPMsg, Python): **126.54 $\mu\text{s}$ avg RTT**, 5,982 msgs/s (100% success, 0 corruptions).
  - `ping_dram` (Hybrid SRAM/DDR Carveout): **191.84 $\mu\text{s}$ avg RTT**, 4,710 msgs/s, 4.60 MB/s.
- [x] **Full 3-Profile Silicon Test Sweep Without Code Modifications**:
  - **Profile 1 (DDR VirtIO RPMsg)**: `testBasic`, `testStringBinaryTrace0`, `testCrash`, `testPingRpmsg`, `testDRAMMsg` all PASS. Both C++ and Python RPMsg benchmarks pass 100%.
  - **Profile 2 (Pure On-Chip SRAM Space 1 VirtIO)**: `testBasic`, `testStringBinaryTrace0`, `testPingRpmsgSram` all PASS. Both C++ and Python SRAM RPMsg benchmarks pass 100%.
  - **Profile 3 (Userspace UIO Direct Mailbox & SRAM)**: `testPing` PASS. Both C++ (`ping_shm`, `ping_uio`) and Python (`ping_uio.py`) benchmarks pass 100%.
- [x] **Middle Ground Data Integrity Verification**:
  - 4-byte `"PONG"` tag + sequence counter validation verified across all ping benchmarks with zero corruptions/mismatches.
  - Automatic stale packet drain on RPMsg open implemented in `ping_rpmsg.cpp` and `ping_rpmsg.py`.
  - Pure scalar memory access implemented for ARM64 `PROT_DEVICE_nGnRnE` mappings in `ping_uio.cpp` and `ping_uio.py` (`ctypes.Structure`), resolving hardware bus error (SIGBUS 135).
- [x] **Automated RemoteProc Test Suite**: Complete test suite passing 100% across all profiles via `python3 /usr/bin/run_tests.py`.
- [x] **Kernel Patch Validation Gate**: `tools/validate_kernel_patches.py` asserting clean dry-run and byte identity against `linux-7.1`.
- [x] **Upstream RFC Patch Series (`patches-upstream-rfc/`)**: Complete 5-patch series + cover letter passing `checkpatch.pl` with 0 errors.
- [x] **Cadence Tensilica HiFi4 Audio DSP Testing Framework (`dsp-hifi4/`)**:
  - `apps/testBasic`: Remoteproc ELF parsing, execution startup, and `trace0` buffer output (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`).
  - `apps/testMsgbox`: Bidirectional hardware mailbox validation over Channels 4 (ARM -> DSP) and 5 (DSP -> ARM).
  - Memory carveout DTS overlays: 1MB `shared-dma-pool` at `0x40000000` (`coproc_shm`), 4MB firmware execution segment at `0x40100000` (`coproc_firmware`).
  - Hardware mailbox driver isolation testing via `CONFIG_MAILBOX_TEST=m` and `cubie-a5e-mailbox-test.dtso`.
  - Target validation script `tools/validate_coproc.sh` and `/usr/bin/run_tests.py` integration.


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

- [x] **Task 1: Pure On-Chip SRAM VirtIO RPMsg Benchmark (`testPingRpmsgSram`)**:
  - [x] Declared dynamic `FW_RSC_ADDR_ANY` vrings in `resource_table.c` / `resource_table.h` with `translate_da()` in `hal::Rpmsg` to map host physical addresses to core local DA.
  - [x] Built `testPingRpmsgSram.elf` and verified memory layout.
  - [x] **Enforce Device Tree Parity Rule**: Static driver carveouts rejected. Pure SRAM VirtIO dynamically governed by Devicetree overlays without hardcoding in C.
  - [x] Created `cubie-a5e-rpmsg-sram.dtso` defining `rproc_sram1: sram1@72c0000` (`0x072c0000`, 256 KB) and overriding `&rproc { memory-region = <&rproc_sram1>; };`.
  - [x] Implement dynamic `memory-region` parsing (`rproc_mem_entry_init` / `rproc_add_carveout`) in `sunxi_rproc.c` to support multi-region Devicetree configurations.
  - [x] Implement active Device Tree profile detection and fail-stop gate in `run_tests.py` to halt incompatible tests and output exact reboot/overlay commands.
  - [x] Configured `dtoverlay=cubie-a5e-flight-stack cubie-a5e-rpmsg-sram` in `/boot/config.txt`, rebooted target board, and verified live kernel binding to `sram1@72c0000`.
  - [x] Benchmarked `ping_rpmsg -n 1000 -s 496` over pure on-chip SRAM Space 1 on live hardware: **141.71 $\mu\text{s}$ avg RTT**, 7,024.7 msgs/sec, 6.65 MB/s, 100% success (0 timeouts).
  - [x] Verified Profile 3 (UIO Mode) on live hardware: **14.29 $\mu\text{s}$ avg RTT**, 64,416.9 msgs/sec, 62.91 MB/s, 100% success (0 timeouts).
  - [x] Complete the 4-tier architectural performance comparison matrix (Silicon Verified):
    - Direct SRAM SPSC (`ping_shm`, Profile 3): **14.29 $\mu\text{s}$** avg RTT | 64,417 msgs/s | 62.9 MB/s
    - Pure SRAM VirtIO RPMsg (`testPingRpmsgSram`, Profile 2): **141.71 $\mu\text{s}$** avg RTT | 7,025 msgs/s | 6.65 MB/s
    - DDR CMA VirtIO RPMsg (`testPingRpmsg`, Profile 1): **175.64 $\mu\text{s}$** avg RTT | 5,671 msgs/s | 5.37 MB/s
    - Hybrid SRAM/DDR Carveout (`testDRAMMsg`, Profile 1): **191.84 $\mu\text{s}$** avg RTT | 4,710 msgs/s | 4.60 MB/s
  - [x] **Test-Named Device Tree Overlays**: Created, compiled, and deployed 1-to-1 test-named overlays in `/boot/` (`cubie-a5e-testBasic.dtbo`, `cubie-a5e-testStringBinaryTrace0.dtbo`, `cubie-a5e-testCrash.dtbo`, `cubie-a5e-testPingRpmsg.dtbo`, `cubie-a5e-testDRAMMsg.dtbo`, `cubie-a5e-testPingRpmsgSram.dtbo`, `cubie-a5e-testPing.dtbo`). Updated `run_tests.py` to identify test overlays, support canonical test names and short aliases, and output exact test-named `dtoverlay` configuration commands when mismatched.
  - [x] **Lightweight Data Integrity Verification ("Middle Ground")**: Implemented 4-byte magic tag (`"PONG"`) + sequence number verification across `ping_rpmsg.cpp`, `ping_rpmsg.py`, `ping_shm.cpp`, `ping_uio.cpp`, and `ping_uio.py`. Verified 1,000 consecutive pings at 5,500 msgs/sec on live target with 100.00% success and 0 corruptions/mismatches. All Profile 1 tests verified PASS.

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

### 5.0 Patch Series (Completed)

- [x] Patch 1: `dt-bindings: mailbox: add Allwinner sun55i msgbox schema`
- [x] Patch 2: `mailbox: sun55i: add Allwinner sun55i/A523 msgbox driver`
- [x] Patch 3: `dt-bindings: remoteproc: add Allwinner sun55i rproc schema`
- [x] Patch 4: `remoteproc: sunxi: add Allwinner XuanTie RISC-V remoteproc driver`
- [x] Patch 5: `arm64: dts: allwinner: sun55i: add msgbox and remoteproc nodes`
- [x] Cover Letter: `patches-upstream-rfc/0000-cover-letter.patch` (72 char line wrap, 0 errors).

---

### 5.1 Test Hardening Plan (Pre-Submission — Closes Coverage Gaps)

> **Context**: Coverage analysis identified **17 gaps in `sunxi_rproc.c`** and **19 gaps in `sun55i-msgbox.c`**. The happy-path silicon tests are excellent (all 3 profiles pass 100%), but upstream reviewers (Bjorn Andersson, Mathieu Poirier, Jassi Brar) will probe error paths, edge cases, and untested channels. This plan closes every identified gap.

---

#### Workstream A: DT Binding Schema Fix [BLOCKER — closes msgbox M19]

> **Priority: CRITICAL** — `make dt_binding_check` and `make dtbs_check` will **FAIL** with the current binding. This blocks submission.

- [x] **A.1: Fix `sun55i-msgbox` DT binding YAML** — `reg: maxItems: 1` must become `minItems: 4 / maxItems: 4` with `reg-names` items (`arm`, `dsp`, `cpus`, `rv`). `interrupts: maxItems: 1` must become `minItems: 1 / maxItems: 4`. **Also fixed DTS `reg` order** to match driver `regs[]` indexing (`arm=0, dsp=1, cpus=2, rv=3`). Quoted description string to ensure 100% valid YAML safe-load.
  - File: `Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml`
  - Patch: Update `patches-upstream-rfc/0001-dt-bindings-mailbox-add-Allwinner-sun55i-msgbox-schema.patch`
  - Also update: `project-cubie-a5e/patches/linux/0012b-dt-bindings-mailbox-add-allwinner-sun55i-msgbox.patch`
- [x] **A.2: Host YAML Schema Validation** — Verified all RFC and Buildroot DT binding schemas parse cleanly with 0 errors via `yaml.safe_load`.
- [ ] **A.3: Run `make dtbs_check`** against `sun55i-a523.dtsi` with the remoteproc+msgbox nodes and verify 0 errors/warnings.

---

#### Workstream B: KUnit Test Suite for `sunxi_rproc.c` [closes rproc #1-9, #11, #14, #17]

> **Priority: HIGH** — Zero in-kernel unit tests currently exist. KUnit tests run at boot or via `kunit_tool` without needing real hardware and catch regressions in CI.

- [x] **B.1: Create `drivers/remoteproc/sunxi_rproc_test.c`** — New KUnit module under `CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST`. Bundled into self-contained patch `0013-remoteproc-sunxi-add-kunit-tests.patch`.
- [x] **B.2: `da_to_va` address translation tests** (pure logic, verified host-side compilation with `aarch64-linux-gcc`):
  - [x] `test_da_to_va_sram_space0_core_da` — DA=`0x3FFC0000`+offset → `r_sram_va` + offset
  - [x] `test_da_to_va_sram_space0_host_phys` — DA=`0x07280000`+offset → `r_sram_va` + offset
  - [x] `test_da_to_va_sram_space0_alt_0x3ff80000` — DA=`0x3FF80000` fallback view
  - [x] `test_da_to_va_sram_space0_alt_0x3ffc0000` — DA=`0x3FFC0000` primary view
  - [x] `test_da_to_va_sram_space1_host_phys` — DA=`r_sram1_phys` → `r_sram1_va`
  - [x] `test_da_to_va_sram_space1_core_da_0x40000000` — DA=`0x40000000` → `r_sram1_va`
  - [x] `test_da_to_va_sram_space1_core_da_0x40040000` — DA=`0x40040000` → `r_sram1_va`
  - [x] `test_da_to_va_dram_carveout` — DA=`dram_phys`+offset → `dram_va` + offset
  - [x] `test_da_to_va_trace_region` — DA=`trace_phys`+offset → `trace_va` + offset, `*is_iomem=false`
  - [x] `test_da_to_va_zero_length_returns_null` — len=0 → NULL [closes rproc #gap in len==0 guard]
  - [x] `test_da_to_va_out_of_range_returns_null` — unmapped DA → NULL (falls through to core carveout table)
  - [x] `test_da_to_va_overflow_wraps_returns_null` — DA + len overflows u64 → NULL
  - [x] `test_da_to_va_is_iomem_sram_true` — SRAM windows set `*is_iomem = true`
  - [x] `test_da_to_va_is_iomem_dram_false` — DRAM/trace windows set `*is_iomem = false`
- [ ] **B.3: `start` / `stop` logic tests**:
  - [ ] `test_start_bootaddr_over_u32_max` — `rproc->bootaddr = 0x100000000ULL` → returns `-EINVAL` [closes rproc #9]
  - [ ] `test_start_writes_boot_vector` — verify `writel(bootaddr, cfg_va + E906_STA_ADD_REG)` with mock `cfg_va`
  - [ ] `test_stop_asserts_core_reset` — verify `reset_control_assert(rst_core)` called
  - [ ] `test_stop_cancels_work_sync` — verify `cancel_work_sync(&priv->vq_work)` called before reset
- [ ] **B.4: `prepare` / `unprepare` error cascade tests** [closes rproc #1, #2]:
  - [ ] `test_prepare_all_clocks_resets_ok` — happy path, all succeed
  - [ ] `test_prepare_cfg_reset_fail_returns_error` — `rst_cfg` deassert fails → immediate return
  - [ ] `test_prepare_sram_reset_fail_unwinds_cfg` — `rst_sram` fails → `rst_cfg` re-asserted
  - [ ] `test_prepare_msgbox_reset_fail_unwinds_sram_cfg` — `rst_msgbox` fails → `rst_sram` + `rst_cfg` asserted
  - [ ] `test_prepare_parent_clk_fail_unwinds_all_resets` — `clk_parent` fails → all 3 resets asserted
  - [ ] `test_prepare_bus_clk_fail_unwinds_parent` — `clk_bus` fails → `clk_parent` disabled + resets asserted
  - [ ] `test_prepare_sram_clk_fail_unwinds_bus` — `clk_sram` fails → `clk_bus` + `clk_parent` disabled + resets
  - [ ] `test_prepare_msgbox_clk_fail_unwinds_sram` — `clk_msgbox` fails → full unwind
  - [ ] `test_prepare_core_clk_fail_unwinds_msgbox` — `clk_core` fails → full unwind
  - [ ] `test_unprepare_symmetry` — verify exact reverse order of clk_disable + reset_assert
- [ ] **B.5: `kick` and `parse_fw` tests** [closes rproc #10, #11, #14]:
  - [ ] `test_kick_null_tx_chan_noop` — `priv->tx_chan = NULL` → early return, no crash
  - [ ] `test_kick_sends_vqid` — `mbox_send_message` called with correct vqid pointer
  - [ ] `test_kick_send_failure_ratelimited` — `mbox_send_message` returns error → `dev_err_ratelimited`
  - [ ] `test_parse_fw_no_resource_table` — `rproc_elf_load_rsc_table` returns `-EINVAL` → `parse_fw` returns 0
  - [ ] `test_parse_fw_with_resource_table` — `rproc_elf_load_rsc_table` returns 0 → `parse_fw` returns 0
- [ ] **B.6: Probe error path tests** [closes rproc #3, #4, #5, #6, #8]:
  - [ ] `test_probe_clk_parent_error_returns_probe_err` — `devm_clk_get_optional("parent")` returns IS_ERR
  - [ ] `test_probe_clk_bus_error_returns_probe_err`
  - [ ] `test_probe_clk_core_error_returns_probe_err`
  - [ ] `test_probe_clk_sram_error_returns_probe_err`
  - [ ] `test_probe_clk_msgbox_error_returns_probe_err`
  - [ ] `test_probe_rst_core_error_returns_probe_err`
  - [ ] `test_probe_rst_cfg_error_returns_probe_err`
  - [ ] `test_probe_rst_sram_error_returns_probe_err`
  - [ ] `test_probe_rst_msgbox_error_returns_probe_err`
  - [ ] `test_probe_r_sram_ioremap_fail_returns_enomem` — r_sram ioremap → `-ENOMEM`
  - [ ] `test_probe_mbox_tx_eprobe_defer` — TX channel returns `-EPROBE_DEFER` → propagated
  - [ ] `test_probe_rproc_add_fail_cleanup` — `rproc_add` fails → mbox + reserved mem released
- [x] **B.7: Add Kconfig entry and Makefile rule** for `CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST`:
  - [x] Added to `drivers/remoteproc/Kconfig` (0 checkpatch errors/warnings)
  - [x] Added to `drivers/remoteproc/Makefile`
  - [x] Added to buildroot `linux.config`: `CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST=y`
  - [x] Compiled `drivers/remoteproc/sunxi_rproc_test.o` with `aarch64-linux-gcc`: 0 warnings, 0 errors

---

#### Workstream C: KUnit Test Suite for `sun55i-msgbox.c` [closes msgbox M17, M18]

> **Priority: HIGH** — Routing table and register offset macros are exercised only for channels 8-9. A single wrong offset silently corrupts adjacent hardware registers.

- [x] **C.1: Create `drivers/mailbox/sun55i_msgbox_test.c`** — New KUnit module under `CONFIG_SUN55I_MSGBOX_KUNIT_TEST`. Bundled into self-contained patch `0014-mailbox-sun55i-add-kunit-tests.patch`.
- [x] **C.2: Channel routing table tests** — verify `sun55i_chan_to_route()` for all 12 channels:
  - [x] `test_chan_to_route_cpus_ch0` — chan=0 → `local_n=0, p=0, remote_id=2, remote_n=0`
  - [x] `test_chan_to_route_cpus_ch1` — chan=1 → `local_n=0, p=1, remote_id=2, remote_n=0`
  - [x] `test_chan_to_route_cpus_ch2` — chan=2 → `local_n=0, p=2, remote_id=2, remote_n=0`
  - [x] `test_chan_to_route_cpus_ch3` — chan=3 → `local_n=0, p=3, remote_id=2, remote_n=0`
  - [x] `test_chan_to_route_dsp_ch4` — chan=4 → `local_n=1, p=0, remote_id=1, remote_n=0`
  - [x] `test_chan_to_route_dsp_ch5_6_7` — channels 5-7 likewise
  - [x] `test_chan_to_route_rv_ch8` — chan=8 → `local_n=2, p=0, remote_id=3, remote_n=2`
  - [x] `test_chan_to_route_rv_ch9_10_11` — channels 9-11 likewise
- [x] **C.3: Register offset macro tests** — verify computed offsets match hardware manual:
  - [x] `test_msgbox_offset_0` — `SUNXI_MSGBOX_OFFSET(0)` = `0x000`
  - [x] `test_msgbox_offset_1` — `SUNXI_MSGBOX_OFFSET(1)` = `0x100`
  - [x] `test_msgbox_offset_2` — `SUNXI_MSGBOX_OFFSET(2)` = `0x200`
  - [x] `test_read_irq_enable_offsets` — verify for local_n=0,1,2
  - [x] `test_read_irq_status_offsets` — verify for local_n=0,1,2
  - [x] `test_write_irq_enable_offsets` — verify for local_n=0,1,2
  - [x] `test_fifo_status_offsets` — verify for all (n, p) combos
  - [x] `test_msg_status_offsets` — verify for all (n, p) combos
  - [x] `test_msg_fifo_offsets` — verify for all (n, p) combos → most critical, wrong offset = silent register corruption
- [x] **C.4: IRQ enable/pending bit position tests**:
  - [x] `test_rd_irq_en_bit_p0` — `RD_IRQ_EN_BIT(0)` = `0x01`
  - [x] `test_rd_irq_en_bit_p1` — `RD_IRQ_EN_BIT(1)` = `0x04`
  - [x] `test_rd_irq_en_bit_p2` — `RD_IRQ_EN_BIT(2)` = `0x10`
  - [x] `test_rd_irq_en_bit_p3` — `RD_IRQ_EN_BIT(3)` = `0x40`
- [ ] **C.5: Functional logic tests** (with mock `readl`/`writel`):
  - [ ] `test_send_data_null_sends_zero` — `data=NULL` → `writel(0, fifo)`
  - [ ] `test_send_data_valid_sends_value` — `data=&val` → `writel(val, fifo)`
  - [ ] `test_last_tx_done_fifo_empty` — `MSG_STATUS=0` → true
  - [ ] `test_last_tx_done_fifo_partial` — `MSG_STATUS=4` → true (4 < 8)
  - [ ] `test_last_tx_done_fifo_full` — `MSG_STATUS=8` → false
  - [ ] `test_peek_data_empty` — `MSG_STATUS=0` → false
  - [ ] `test_peek_data_available` — `MSG_STATUS=3` → true
- [x] **C.6: Add Kconfig + Makefile for `CONFIG_SUN55I_MSGBOX_KUNIT_TEST`**:
  - [x] Added to `drivers/mailbox/Kconfig` (0 checkpatch errors/warnings)
  - [x] Added to `drivers/mailbox/Makefile`
  - [x] Added to buildroot `linux.config`: `CONFIG_SUN55I_MSGBOX_KUNIT_TEST=y`
  - [x] Compiled `drivers/mailbox/sun55i_msgbox_test.o` with `aarch64-linux-gcc`: 0 warnings, 0 errors

---

#### Workstream D: On-Target Stress & Lifecycle Tests [closes rproc #12, #13, #15]

> **Priority: MEDIUM** — Catches clock refcount leaks, race conditions, and SRAM clearing issues that only manifest under rapid cycling.

- [ ] **D.1: Rapid start/stop lifecycle stress test** (50+ iterations) [closes rproc #12]:
  ```bash
  #!/bin/sh
  for i in $(seq 1 50); do
    echo "testBasic.elf" > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state
    sleep 0.1
    echo stop > /sys/class/remoteproc/remoteproc0/state
  done
  # Verify: dmesg clean, no oops, clock refcounts balanced
  ```
  - [ ] Add to `run_tests.py` as `test_lifecycle_stress`
  - [ ] Verify `dmesg` has zero warnings/errors after 50 cycles
  - [ ] Verify `cat /sys/kernel/debug/clk/clk_summary` shows balanced enable counts
- [ ] **D.2: Double-start rejection test** [closes rproc #13]:
  - [ ] `echo start` when already `running` → verify graceful rejection (no crash/oops)
  - [ ] Add to `run_tests.py`
- [ ] **D.3: SRAM zeroing verification** [closes rproc #15]:
  - [ ] After `echo start` with `testBasic.elf`, stop and re-start → verify SRAM was cleanly re-zeroed (no stale data from prior run visible in `trace0`)
  - [ ] Add to `run_tests.py`
- [ ] **D.4: RPMsg rapid connect/disconnect cycling**:
  - [ ] Start `testPingRpmsg.elf`, open `/dev/rpmsg0`, send 10 pings, close, repeat 20 times
  - [ ] Verify no vring leaks, channel re-registration is clean, `dmesg` clean

---

#### Workstream E: Msgbox Multi-Channel & FIFO Tests [closes msgbox M1-M8, M16]

> **Priority: MEDIUM** — 10 of 12 channels have zero silicon validation. Tests here prove the routing table and register offsets work for all ports.

- [ ] **E.1: Multi-channel mailbox loopback test module** [closes M1-M4]:
  - [ ] Create a minimal kernel test module that opens channels 0 (CPUS), 4 (DSP), and 8 (RV) simultaneously
  - [ ] Send a known 32-bit token on each TX FIFO
  - [ ] Read back the remote port's RX FIFO status register to verify the message landed in the correct hardware FIFO
  - [ ] Proves `arm_routes[]` entries and register bank (`regs[remote_id]`) mappings are correct
- [ ] **E.2: FIFO depth stress test** [closes M5-M6]:
  - [ ] Burst-write 8 messages to a single TX FIFO without draining
  - [ ] Verify `last_tx_done()` returns `true` for count < 8 and `false` at count == 8
  - [ ] Attempt 9th write and document hardware behavior (drop? block? error?)
- [ ] **E.3: Stale FIFO flush verification** [closes M16]:
  - [ ] Start `testPingRpmsg.elf`, send several RPMsg pings, then `echo stop` **without** cleanly shutting down firmware
  - [ ] Immediately re-start → verify `sun55i_msgbox_startup()` flushes stale FIFO entries and no ghost messages appear
- [ ] **E.4: Spurious IRQ handling test** [closes M7]:
  - [ ] With no channels active, manually trigger the shared IRQ line
  - [ ] Verify handler returns `IRQ_NONE` and no crash or data corruption
- [ ] **E.5: Multi-IRQ line verification** [closes M8]:
  - [ ] Document which of the 4 DTS IRQs (`SPI 0`, `SPI 1`, `SPI 181`, `SPI 174`) fires for each processor port
  - [ ] Verify at least 2 different IRQ lines fire during concurrent multi-channel tests

---

#### Workstream F: Driver Unbind/Rebind & Remove Path Testing [closes rproc remove gap, msgbox M2 remove]

> **Priority: MEDIUM** — `remove()` is never tested during normal start/stop cycles; only fires on driver unbind.

- [ ] **F.1: Remoteproc unbind/rebind cycle**:
  ```bash
  echo stop > /sys/class/remoteproc/remoteproc0/state
  echo "7130000.remoteproc" > /sys/bus/platform/drivers/sunxi-rproc/unbind
  sleep 1
  echo "7130000.remoteproc" > /sys/bus/platform/drivers/sunxi-rproc/bind
  # Verify: clean re-probe, can start firmware again
  echo "testBasic.elf" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state
  cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
  ```
  - [ ] Verify `dmesg` shows clean `rproc_del` + `mbox_free_channel` + `of_reserved_mem_device_release`
  - [ ] Verify no memory leaks (check `/proc/meminfo` before/after)
- [ ] **F.2: Msgbox unbind/rebind cycle**:
  ```bash
  echo "3003000.mailbox" > /sys/bus/platform/drivers/sun55i-msgbox/unbind
  sleep 1
  echo "3003000.mailbox" > /sys/bus/platform/drivers/sun55i-msgbox/bind
  ```
  - [ ] Verify `mbox_controller_unregister` + `clk_disable_unprepare` in `dmesg`
  - [ ] Verify subsequent remoteproc start with RPMsg still works
- [ ] **F.3: Unbind while running** (negative test):
  - [ ] Attempt to unbind remoteproc driver while core is `running`
  - [ ] Verify driver handles this gracefully (stops core first, or refuses unbind)
- [ ] **F.4: Add unbind/rebind to `run_tests.py`** as optional advanced test tier

---

#### Workstream G: Automated CI Integration

- [x] **G.1: Add KUnit test configs to buildroot**:
  - [x] Configured `CONFIG_KUNIT=y`, `CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST=y`, `CONFIG_SUN55I_MSGBOX_KUNIT_TEST=y` in `linux.config`.
  - [x] Verified native compilation with `aarch64-linux-gcc` produces valid test suites.
- [ ] **G.2: Extend `run_tests.py`** to include:
  - [ ] Lifecycle stress test (Workstream D.1)
  - [ ] Double-start rejection test (D.2)
  - [ ] SRAM re-zeroing test (D.3)
  - [ ] RPMsg rapid cycling test (D.4)
  - [ ] Unbind/rebind test (F.1, optional tier)
- [x] **G.3: Extend `validate_kernel_patches.py`** to validate all 5 patches:
  - [x] Added `0013` and `0014` to defconfigs, dry-run, live tree comparison, and checkpatch verification (100% PASS).

---

### 5.2 Submit to Upstream

- [ ] Submit RFC series to `linux-sunxi@lists.linux.dev` and `linux-remoteproc@vger.kernel.org` via `git send-email`.


---

# Part II: Radxa Cubie A7A (Allwinner A733)

## 1. Hardware Overview & Current Bring-Up Status

| Subsystem | Silicon / Component | Status | Next Milestone |
| :--- | :--- | :--- | :--- |
| **SoC / Bootloader** | Allwinner A733 / U-Boot 2024 / Linux 7.1 | **Operational** | Clean Buildroot patch gate |
| **USB & Hub** | FE1.1S Hub + AIC8800 Wi-Fi 6 | **Active Bring-Up** | Hub & Wi-Fi device enumeration on target |
| **Ethernet** | GMAC210 + Maxio MAE0621A-Q3C | **Active Blocker** | Resolve TX DMA watchdog timeout (multi-MSI) |
| **Co-Processor** | XuanTie E902 (200 MHz, RV32EMC) | **Architecture Defined** | Dual-mode RemoteProc bring-up (Mode 2) |
| **PMIC / Regulators** | AXP717 / GPIO switches (PL2, PM5, PM0, PM1) | **Configured** | Voltage verification under load |

---

## 2. USB and Power Subsystem (Active Priority)

* **Goal**: Enable the onboard FE1.1S USB 2.0 4-port hub on Host 1 (`ehci1`) and verify enumeration of the integrated AIC8800 Wi-Fi 6 / BT 5.4 module on hub downstream port 4.

- [x] **Schematic & Power Sequencing Analysis**:
  - [x] Decoded V1.10 schematic sheets 4, 13–15, and 18.
  - [x] Configured GPIO power switches as `regulator-always-on` and `regulator-boot-on`:
    - Port 0 VBUS: `PL2` (`USB0-DRVVBUS`)
    - Port 1 / Hub VBUS: `PM5` (`USB_HOST_EN`)
    - Wi-Fi Power: `PM0` (`WL_REG_ON` / 3.3V power gate)
    - Wi-Fi Chip Enable: `PM1` (`WL_WAKE_AP` / reset-enable)
- [x] **CCU Interconnect & HCI Clocks**:
  - [x] Un-gated `0x05C0` (`AHB_GATE_SW_CFG` bit 9) in CCU probe.
  - [x] Updated `bus_usb0_clk` and `bus_usb1_clk` to mask `BIT(4) | BIT(0)` (`0x1304`/`0x130c`), clocking EHCI DMA engines and OHCI.
- [x] **PHY SIDDQ & Shared Resets**:
  - [x] Added `sun60i_a733_cfg` in `phy-sun4i-usb.c` clearing `PHY_CTL_SIDDQ | PHY_CTL_H3_SIDDQ` on PMU1.
  - [x] Switched to `devm_reset_control_get_shared()` to prevent `-EBUSY` collisions between USB host controllers.
- [ ] **Target Hardware Validation**:
  - [ ] Boot newly compiled Linux kernel image on Radxa Cubie A7A hardware.
  - [ ] Verify `dmesg | grep -i -E "ehci|ohci|phy-sun4i"` shows clean probe without `-EBUSY`.
  - [ ] Run `lsusb` to confirm FE1.1S 4-port USB 2.0 hub enumerates (`1a40:0101`).
  - [ ] Confirm AIC8800 Wi-Fi 6 device enumerates on hub downstream port 4 (`0xA69C:0x8800`).
  - [ ] Load `aic8800_fdrv` out-of-tree kernel driver and verify `wlan0` interface appears.

---

## 3. Ethernet Subsystem (Active Blocker: TX DMA Timeout)

* **Goal**: Establish stable, bidirectional Gigabit Ethernet connectivity on `eth0` without TX queue watchdog timeouts.

- [x] **PHY & Link Discovery (Completed)**:
  - [x] MDIO discovers Maxio MAE0621A-Q3C at PHY address 1.
  - [x] U-Boot target proof: pings to live peer `192.168.1.2` succeeded (proves magnetics, cable, RGMII routing, and PHY reset/power are physically intact).
  - [x] Target Linux boot establishes link: `Link is Up - 1Gbps/Full`.
  - [x] Added `ethtool` (`/usr/sbin/ethtool`) and `phytool` (`/usr/bin/phytool`) to target image.
- [x] **Failure Mode Identified**:
  - [x] Repeated console message: `NETDEV WATCHDOG: transmit queue 0 timed out` every 5–6 seconds.
  - [x] TX byte count stalls at ~1,314 bytes with 0 RX bytes received; adapter continuously resets and re-negotiates link.
- [ ] **Diagnosis & Driver Fix**:
  - [ ] Capture `/proc/interrupts` before and after traffic to see if only `macirq` triggers while queue IRQs remain 0.
  - [ ] Vendor GMAC210 driver uses split multi-MSI with dedicated TX0/RX0 IRQs; mainline `dwmac-sun55i` currently lacks `STMMAC_FLAG_MULTI_MSI_EN`.
  - [ ] Add standard queue IRQ names in DTS and enable multi-MSI in A733 stmmac glue driver.
  - [ ] Retest ping on target silicon and confirm TX DMA packet retirement.

---

## 4. Co-Processor & Real-Time Control: A733 Dual-Mode Architecture

* **Goal**: Support XuanTie E902 (RV32EMC @ 200 MHz, no FPU, 208 KB System SRAM A2) co-processor execution via Linux RemoteProc.

- [x] **Silicon & Security Discovery**:
  - [x] Identified co-processor as XuanTie E902 (distinct from A5E's E907).
  - [x] Stock BL31 write-protects `0x07032204` for factory `scp.fex`.
- [x] **Dual-Mode Operating Model**:
  - **Mode 1 (Suspend/Resume)**: Stock TOC1 with `scp.fex` for consumer S3 deep sleep.
  - **Mode 2 (Real-Time Control / Linux RemoteProc)**: 24/7 industrial real-time control without suspend/resume.
- [ ] **Mode 2 Execution Tasks (Post-USB Bring-Up)**:
  - [ ] **TF-A (BL31)**: Configure `sunxi_security.c` to unlock `R_SPC` (`0x07002000`) and `R_TZMA` (`0x07003000`) so Non-Secure Linux EL1 can access `0x07032000` and System SRAM A2 (`0x00040000`).
  - [ ] **U-Boot**: Ensure U-Boot RSB driver powers PMIC `DCDC1` and `ALDO1` when `scp.fex` is omitted from TOC1.
  - [ ] **Device Tree**: Add `cubie-a7a-rproc.dtso` overlay defining `&rproc` and `0x4E000000` DMA carveout pool.
  - [ ] **Kernel Driver**: Update `sunxi_rproc.c` with `"allwinner,sun60i-a733-rproc"` to map SRAM A2 (`0x00040000`, 208 KB) and manage E902 lifecycle.

---

## 5. Mandatory Patch Gate & Engineering Rules

- [x] Clean Buildroot validation gate passing: `make -C bld.a7a linux-dirclean && make -C bld.a7a linux`.
- [ ] Rerun clean validation gate before committing any new A7A patch.
- [ ] Keep permanent kernel patches in `project-cubie-a5e/patches/linux/` (never leave fixes isolated in `bld.a7a/`).
- [ ] Maintain diagnostic record in `docs/platforms/CUBIE_A7A_DEBUG_LOG.md`.

---

# Part III: Bare-Metal Firmware & Bootloader Research: YuzukiHD/SyterKit Evaluation

* **Repository**: [YuzukiHD/SyterKit](https://github.com/YuzukiHD/SyterKit)
* **Scope**: Cross-reference bare-metal drivers, clock trees, DRAM initialization, and coprocessor loaders for both **Allwinner T527 (Radxa Cubie A5E / Avaota-A1)** and **Allwinner A733 (Radxa Cubie A7A)** to ensure no hardware quirks, errata workarounds, or register bits are missed in our upstream Linux and U-Boot port.

## 1. Allwinner T527 / A527 (Cubie A5E / Avaota-A1) Follow-Up Tasks
- [ ] **HiFi4 DSP & XuanTie E907 Bare-Metal Loaders**:
  - [ ] Inspect SyterKit's `load_hifi4` implementation to compare DSP reset release sequence, `HIFI4_CTRL_REG0` stall bits, and `HIFI4_ALT_RESET_VEC` vector setup against our `sunxi_rproc.c` and `cubie-a5e-dsp.dtso`.
  - [ ] Compare SyterKit's SRAM remap register settings (`0x07140364` / `0x07010364`) to confirm DSP private memory isolation vs host shared window.
- [ ] **CCU / MCU CCU Clock Gating & Reset Assertions**:
  - [ ] Audit SyterKit clock tree tables for `CLK_DSP`, `CLK_DSP_CFG`, `CLK_BUS_MSGBOX`, and `CLK_BUS_MCU_PUBSRAM` to verify if any auxiliary parent PLLs (e.g. `PLL_PERI0_2X`) require specific pre-dividers under high load.
- [ ] **PMIC Sequencing & Voltage Rails**:
  - [ ] Cross-check AXP PMIC power rail voltages for the DSP/MCU core voltage under active 600 MHz DSP clocking.

## 2. Allwinner A733 (Cubie A7A) Follow-Up Tasks
- [ ] **GMAC210 TX DMA Watchdog Timeout Investigation**:
  - [ ] Review SyterKit's Ethernet / GMAC initialization for A733 to see if DMA burst length, AXI bus arbitration, or interrupt moderation settings differ from mainline `dwmac-sun55i.c`.
  - [ ] Check if SyterKit configures additional interconnect / system bus gating registers for the GMAC DMA controller.
- [ ] **USB 2.0 PHY & Host Subsystem**:
  - [ ] Compare SyterKit's USB PHY SIDDQ power-down disable and PMU register sequences against our `phy-sun4i-usb.c` (`sun60i_a733_cfg`) implementation.
  - [ ] Verify if any additional USB hub reset timing or VBUS enable delays are specified.
- [ ] **XuanTie E902 Co-Processor Initialization**:
  - [ ] Check if SyterKit contains early bootstrap code or linker scripts for the A733 E902 core running out of System SRAM A2 (`0x00040000`).

