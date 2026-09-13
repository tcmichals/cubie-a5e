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

- [x] Patch 1: `dt-bindings: mailbox: add Allwinner sun55i msgbox schema`
- [x] Patch 2: `mailbox: sun55i: add Allwinner sun55i/A523 msgbox driver`
- [x] Patch 3: `dt-bindings: remoteproc: add Allwinner sun55i rproc schema`
- [x] Patch 4: `remoteproc: sunxi: add Allwinner XuanTie RISC-V remoteproc driver`
- [x] Patch 5: `arm64: dts: allwinner: sun55i: add msgbox and remoteproc nodes`
- [x] Cover Letter: `patches-upstream-rfc/0000-cover-letter.patch` (72 char line wrap, 0 errors).
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
