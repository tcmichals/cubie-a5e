# Allwinner A527/T527 & A733 Mainline Linux Development

[![Linux Kernel](https://img.shields.io/badge/Kernel-7.1.0--PREEMPT__RT-blue.svg)](upstream-remoteproc/)
[![Buildroot](https://img.shields.io/badge/Buildroot-2026.05-orange.svg)](project-cubie-a5e/)
[![RISC-V](https://img.shields.io/badge/Co--Processor-XuanTie_E906_%2F_E902-green.svg)](firmware/)
[![Wi-Fi 6](https://img.shields.io/badge/Wireless-AIC8800_Wi--Fi_6-teal.svg)](docs/common/WIFI_AIC8800_GUIDE.md)
[![Upstream Status](https://img.shields.io/badge/Submissions-v3_sent%2C_v4_in_progress-blue.svg)](upstream-remoteproc/)

This repository is **Linux mainline development** for the **Radxa Cubie A5E** (Allwinner A527/T527) and the **Radxa Cubie A7A** (Allwinner A733).

**The goal is to fully test both boards to Linux standards**, and to replace the vendor BSP kernels (Linux 5.10 with proprietary binary blobs) with drivers that can go upstream. The repo holds the patch series and their reviews, the Buildroot images, the out-of-tree drivers on their way to mainline, the firmware that exercises the drivers, and the test suite.

[AbstractX](https://github.com/tcmichals/AbstractX) is what this work gets used for. It lives in its own repository.

Working on this repo, by hand or with an AI tool? Start with [AGENTS.md](AGENTS.md). [TODO.md](TODO.md) is the only task tracker.

---

## ✅ What "Tested to Linux Standards" Means

The A5E's v3 run set the bar, and both boards are held to it.

| Check | Cubie A5E | Cubie A7A |
| :--- | :--- | :--- |
| **KUnit** | 63/63 at the last sweep; v4 not run on the board | Not at the A5E's level; no camera tests |
| **Full board sweep** | 45/45 on v3 code (2026-10-29); not yet run on v4 | `--target a7a` covers E902 remoteproc only; no results committed |
| **Driver coverage** | msgbox 96.77%, rproc 89.76% (v3, board + UML) | None recorded |
| **PREEMPT_RT and non-RT, lockdep, built-in and modules** | Done on v3 | None recorded |
| **GCC and clang `W=1`, checkpatch, `dt_binding_check`** | Done on v3 | Not done for the A7A-only patches |

Sweep results for the A5E: [test_sweep_results.md](test_sweep_results.md) and [the wiki page](wiki/Full-Sweep-Tests.md).

---

## 📢 Upstream Linux Kernel Patch Submissions

Drivers for the sun55i/sun60i platform go to the kernel mailing lists (`linux-sunxi@lists.linux.dev`, `linux-remoteproc@vger.kernel.org`, `devicetree@vger.kernel.org`). Patch series, cover letters and review responses are in **[`upstream-remoteproc/`](upstream-remoteproc/)**:

| Series | Status | Lore Mailing List Thread | Notes |
| :---: | :---: | :--- | :--- |
| **RFC v1** | *Superseded* | [20260922034711.190253-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/) | 7 patches; initial schema and driver feedback |
| **v2** | *Reviewed* | [20260927002021.797069-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/) | KUnit tests added |
| **v3** | *Sent 2026-11-01, reviewed* | [20261101185735.762559-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/20261101185735.762559-1-tcmichals@gmail.com/) | Review responses tracked for v4 |
| **v4** | **In progress** | Not sent | Fixes on `cubie-linux-7.1`; board test and pre-send checks still to do |

The reasoning and the record are in [`DESIGN_DECISIONS.md`](upstream-remoteproc/DESIGN_DECISIONS.md), [`UPSTREAM_LESSONS_LEARNED.md`](upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md) and [`SUBMISSION_TRACKER.md`](upstream-remoteproc/SUBMISSION_TRACKER.md).

---

## 🚦 Hardware Platform Status

| Board | Done | Next |
| :--- | :--- | :--- |
| **Cubie A5E**<br>*(Allwinner A527 / T527)* | • Mainline Linux 7.1 `PREEMPT_RT` boot (U-Boot + TF-A BL31)<br>• XuanTie E906 remoteproc + message box: sysfs start/stop, trace0 debugfs streaming, exception capture (`0xDEADF00D`)<br>• VirtIO RPMsg and UIO IPC across 3 memory profiles (DRAM, SRAM A3, userspace UIO)<br>• Full sweep passing on v3 code (`tools/run_full_sweep.py --target a5e`)<br>• Upstream v3 sent<br>• Wi-Fi 6 AIC8800D80 (SDIO) on the RFC v3 driver | • Update the sweep and overlay for v4, then run the full test set on v4<br>• v4 pre-send checks, then submit<br>• Camera: OV5647 over MIPI CSI-2<br>• Cadence HiFi4 Audio DSP applications |
| **Cubie A7A**<br>*(Allwinner A733)*<br>Why: E902 real-time coprocessor + THS + hardware VPU are what a flight controller needs from this SoC; all run on the **stock** vendor BL31, so any distribution can use them | • Mainline Linux 7.1 `PREEMPT_RT` boot (U-Boot + TF-A BL31)<br>• USB 2.0 host + FE1.1S hub at High-Speed (480 Mbps)<br>• Wi-Fi 6 AIC8800 (USB) with a DHCP lease on `aic8800-rfcv3` (verified on silicon 2026-11-15: 5 GHz Wi-Fi 6 `80 MHz`, `aic_load_fw.ko` handles `a69c:8d80` BootROM FW upload)<br>• E902 runs code from Linux with the **stock** BL31 and no SCP: no BL31 change needed (2026-11-10, [rebuild guide §6](docs/platforms/CUBIE_A7A_E902_REBUILD_GUIDE.md))<br>• **Linux remoteproc runs the E902** (2026-11-10): `testStringBinaryTrace0` fills `trace0`, 10 stop/start cycles. E902 sees System SRAM A2 (host `0x00040000`) at `0x40000000` but its first 16 KB is the CPUS-only part (A733 User Manual): firmware and the driver use the 192 KB at `0x40004000`<br>• **Thermal sensor (THS) + cpufreq cooling** (2026-11-15): 5 thermal zones (36–39 °C idle, 61.4 °C peak 8-core fan-cooled); `ccu-sun60i-a733-cpu` (`0x08870000`, 26 MHz DCXO) + frequency-only `cpufreq-dt` (`policy0` cpu0-5, `policy6` cpu6-7: `416 / 780 / 1014 MHz` at 800 mV boot rail) + `cpufreq-cpu0`/`cpufreq-cpu6` passive cooling maps (70/90 °C trips) verified on silicon<br>• **Message box** (`sun55i-msgbox` A733 variant, 2 banks) and **RPMsg** to the E902 (2026-11-11): `ping_rpmsg` 1000 round trips, average 120.5 µs, no WARN or hang. Clocks, resets and registers: [A733_E902_REMOTEPROC_CLOCKS_RESETS.md](docs/platforms/A733_E902_REMOTEPROC_CLOCKS_RESETS.md)<br>• **Zero-Copy Video Encoding (Cedrus VPU H.264 & H.265)** (2026-11-15): 720p & 1080p H.264 hardware encoding at **78.3 fps** standalone and **30.02 fps** live CSI→VE_ENC (**0.89% CPU** of 1 core, 0 decode errors); H.265 (HEVC) fixed-QP + CBR/VBR rate control verified at 1 Mbps & 4 Mbps (**1080p30**, **1.55% CPU** of 1 core, 0 decode errors/warnings in `avdec_h265` & `libde265dec`, `v4l2-compliance` 49/49)<br>• **Hardware ISP 602 (`sun60i-isp`, Phases 1–4)** (2026-11-15): In-line ISP driver, DT bindings (`allwinner,sun60i-a733-isp.yaml`), CSI DMA0 NV12, `params`/`stats` metadata nodes, and dedicated `rkisp1`-style `libcamera` `sun60i-isp` pipeline handler & IPA (Software ISP removed) verified at 1080p30 on silicon<br>• **Green Power LED**: Active-low silicon inversion resolved (`PJ26` `GPIO_ACTIVE_LOW`), default-on verified on silicon<br>• Watchdog soft reset via `WDT_SRST_REG` in `sunxi_wdt.c`<br>• 11 E902 ELFs staged (incl. `testFreeRTOS`) | • Board-test `aic8800-rfcv3` SDIO on A5E after USB merge; decide whether to delete `aic8800-upstream` (Avaota A1 / dual-bus)<br>• GMAC210 Ethernet TX DMA timeout fix<br>• `stress_rpmsg` and the full A7A sweep<br>• **cpufreq (A5E + A7A voltage scaling)**: A5E still needs CPU CCU/OPP/cooling; A7A frequency-only cpufreq (`416/780/1014 MHz`) + passive cooling is working, with AXP8191 regulator voltage scaling (> 1014 MHz) + speed-binning next<br>• Bring the v3 and v4 remoteproc and msgbox fixes to this board's branch<br>• Extend the sweep to USB, Wi-Fi, camera and watchdog; run the full test set<br>• Upstream `sun60i-isp` RFC to `linux-media` and backport ISP 602 to A5E (T527) |

### 🌡️ Thermal (2026-11-11)

| Board | Measured | Notes |
| :--- | :--- | :--- |
| **Cubie A5E** | Idle with 4 cores, VPU/GPU/NPU off: **87 °C without fan**, ~47 °C with fan | CPU rails already at 0.90 V (lowest OPP voltage) and cpu4-7 power-gated, so cpufreq cannot lower idle; the 15×15×5 mm heatsink cannot remove the heat. **Not flight-safe without guaranteed airflow.** Heatsink plan: **[CUBIE_A5E_HEATSINK.md](docs/platforms/CUBIE_A5E_HEATSINK.md)**. Results: [logs/a5e-thermal-20261111/RESULTS.md](logs/a5e-thermal-20261111/RESULTS.md) |
| **Cubie A7A** | 36 °C idle, 61.4 °C after 3 min on all 8 cores (fan fitted) | Safe on the bench; trips 90 °C hot, 110 °C critical |
| Both | A7A: frequency-only cpufreq (`416/780/1014 MHz`) + passive cooling (70/90 °C) active; A5E: no cpufreq / no cpuidle yet | Plan: [CPUFREQ_DVFS_PLAN.md](docs/common/CPUFREQ_DVFS_PLAN.md) (A7A AXP8191 voltage scaling + speed-binning, and T527 backport next) |

### 📷 A7A camera & Zero-Copy Hardware Encoding (2026-11-15)

OV5647 1080p30 runs on mainline `sun6i-mipi-csi2` → `sun6i-csi` → hardware ISP `sun60i-isp` with a dedicated `libcamera` `sun60i-isp` pipeline handler & IPA (Software ISP removed). Zero-copy pipelines passing CMA `dma-buf`s directly from CSI capture into the Cedrus hardware encoder operate at steady **30.00–30.02 fps** with **0.89% CPU** (H.264) and **1.55% CPU** (H.265 CBR/VBR) of one core, and 0 decode errors. Standalone 1080p H.264 encoding benchmarks at **78.3 fps** (12.7 ms/frame). Detailed bring-up logs: [RESULTS.md](logs/a7a-ve-enc-20261114/RESULTS.md), [A733_VE_ENCODER_GUIDE.md](docs/common/A733_VE_ENCODER_GUIDE.md), and [CAMERA_V4L2_GUIDE.md](docs/common/CAMERA_V4L2_GUIDE.md).

> [!NOTE]
> Display / HDMI output is still pending mainline bring-up. For full task tracking, see [TODO.md](TODO.md).

---

## 🌿 Kernel Trees and Branches

Each board builds from one kernel tree and one branch, set in `bld.<board>/local.mk` by `tools/setup_workspace.sh`:

| Board | Kernel tree | Branch | Holds |
| :--- | :--- | :--- | :--- |
| Cubie A5E | `linux-v4-71` (worktree of `linux-cubie`) | `cubie-linux-7.1` | v4 remoteproc and message box fixes |
| Cubie A7A | `linux-cubie` | `a7a_remoteproc_camera_usb` | All A7A work: E902 remoteproc, camera, USB |

Material removed from this repo when it was refocused on 2026-11-07 is kept on the [`archive/pre-cleanup-20261107`](https://github.com/tcmichals/cubie-a5e/tree/archive/pre-cleanup-20261107) branch.

---

## 🛠️ Board Comparison

| Hardware Feature | Radxa Cubie A5E | Radxa Cubie A7A |
| :--- | :--- | :--- |
| **SoC** | Allwinner A527 / T527 | Allwinner A733 |
| **Form Factor** | Standard SBC (85×56 mm) | Standard SBC (85×56 mm) |
| **Host CPU** | 8× Arm Cortex-A55 @ 1.8 GHz | 4× Cortex-A76 + 4× Cortex-A55 |
| **Co-Processors** | XuanTie E906 (RV32IMAC) + HiFi4 DSP | XuanTie E902 (RV32EMC) |
| **GPR Registers** | 32 registers (`x0`–`x31`) | 16 registers (`x0`–`x15`) |
| **Co-Proc Memory** | SRAM A3 (32 KB) + DSP RAM (64 KB) | System SRAM A2 (208 KB shared) |
| **System RAM** | 2 GiB / 4 GiB LPDDR4X | 4 GiB / 6 GiB LPDDR5 |
| **Wi-Fi / BT** | AIC8800D80 (SDIO) | AIC8800D80 (USB) |
| **Thermal sensor (THS)** | `sun8i_thermal` A523 support: `ths0` + `ths1`, zones `cpu0`, `cpu4`, `gpu`, `ddr`; ~41-42 °C idle (2026-11-10) | `sun8i_thermal` A733 support (this repo): 5 sensors at `0x02522000`, zones `cpu-big`, `ddr`, `npu`, `cpu-little`, `gpu`; SID calibration; hot 90 °C / critical 110 °C, no cooling device yet |
| **Base DTB** | `sun55i-a527-cubie-a5e.dtb` | `sun60i-a733-cubie-a7a.dtb` |

The tree also has configuration for the Yuzuki Avaota A1 and the Radxa Cubie A7Z, which are not part of the test goal.

---

## 🌡️ Thermal Sensors (THS)

Both boards report CPU, GPU and DRAM temperatures through mainline `sun8i_thermal` (`/sys/class/thermal/thermal_zone*/temp`, millidegree Celsius; also hwmon).

| | Cubie A5E (A527/T527) | Cubie A7A (A733) |
| :--- | :--- | :--- |
| Driver support | A523 (`ths0` + `ths1`), in the tree | A733, added on `a7a_remoteproc_camera_usb` (2026-11-10) |
| Sensors / zones | `cpu0`, `cpu4`, `gpu`, `ddr` | THS at `0x02522000`, 5 sensors: `cpu-big` (0), `ddr` (1), `npu` (2), `cpu-little` (3), `gpu` (4) |
| Clocks, IRQ | A523 CCU `bus-ths`, `gpadc0/1` | CCU `THS0_BGR` `0x0fe4` (gate + reset), `GPADC0_24M` `0x0fc0`; IRQ 71 (GIC SPI 39) |
| Conversion | A523 two-segment formula | Vendor `sun60iw2`: code > 1769: `62 × (2822 − code)` m°C, else `59 × (2835 − code)` m°C |
| Calibration | SID cells `0x38` and `0x44` | SID `0x44`, 72 bits: factory temperature (0.1 °C) + one 12-bit reading per sensor → per-sensor offset in `THSn_CDATA` |
| Trips | A523 DT | hot 90 °C, critical 110 °C (shutdown); no cooling device yet (no fan or cpufreq in the DT) |
| Measured | ~41-42 °C idle | ~35-39 °C idle; 30 s on all 8 cores: ~49-51 °C, back to ~42 °C 10 s after |

The A733 values come from the A733 User Manual (7.1, 4.1.6.170-172) and the vendor `sunxi_thermal.c` / `sun60iw2p1.dtsi`; details in [CUBIE_A7A_DEBUG_LOG.md](docs/platforms/CUBIE_A7A_DEBUG_LOG.md) Milestone 22.

## 🧪 Automated Silicon Verification Suite

The test runner connects to a board over SSH, or watches the serial console, and validates the co-processor stack:

```bash
# Radxa Cubie A5E (XuanTie E906)
python3 tools/run_full_sweep.py --target a5e --ip <board-ip>

# Radxa Cubie A7A (XuanTie E902)
python3 tools/run_full_sweep.py --target a7a --ip <board-ip>
```

The A5E sweep checks:
1. **KUnit**: the remoteproc and message box suites, at boot on the board.
2. **ELF lifecycle**: starts and stops the co-processor via sysfs `state` without kernel panics or bus hangs.
3. **Crash capture**: `testCrash.elf` triggers traps and the register dump is verified (`mepc`, `mcause`, `mtval`, `0xDEADF00D`).
4. **VirtIO RPMsg**: 1,000 pings and 100,000 stress messages with no loss, with the pool in DRAM and in SRAM.
5. **UIO, FreeRTOS and extra RAM**: the UIO profile, preemptive scheduling, and each extra memory region.
6. **Kernel health and history**: a clean `dmesg`, and metrics in range of the last passing runs.
7. **Report**: results in `test_sweep_results.md` and `.json` (`test_sweep_results_a7a.*` for the A7A).

---

## 🚀 Quick Start: Building & Flashing

### 1. Workspace Initialization
Clone the repository recursively (including the FreeRTOS kernel submodule) and run the setup script:
```bash
git clone --recursive git@github.com:tcmichals/cubie-a5e.git
cd cubie-a5e

# Initialize submodules and build environment:
./tools/setup_workspace.sh
```

### 2. Multi-PC Git Synchronization
If developing across multiple machines, use `tools/sync_kernel.sh` to keep your local kernel trees in sync:
```bash
./tools/sync_kernel.sh status     # Check git status across repos
./tools/sync_kernel.sh push       # Push working commits
./tools/sync_kernel.sh pull       # Pull latest commits
./tools/sync_kernel.sh rebuild    # Trigger kernel rebuild in Buildroot
```

### 3. Build Full System SD Card Images

* **Radxa Cubie A5E (Allwinner A527 / T527):**
  ```bash
  make -C bld.a5e
  ```
  Output image: `bld.a5e/images/sdcard.img`

* **Radxa Cubie A7A (Allwinner A733):**
  ```bash
  make -C bld.a7a
  ```
  Output image: `bld.a7a/images/sdcard.img`

### 4. Flashing to MicroSD Card
```bash
sudo dd if=bld.a5e/images/sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```
*(Replace `/dev/sdX` with your target SD card block device; verify carefully before executing).*

---

## 📁 Repository Structure

```text
cubie-a5e/
├── AGENTS.md                    # Entry point for AI tools; rules live in .agents/rules/
├── TODO.md                      # The only task tracker
├── upstream-remoteproc/         # Upstream RemoteProc & Mailbox patch hub
│   ├── DESIGN_DECISIONS.md      # Binding, memory-region, and architectural decisions
│   ├── UPSTREAM_LESSONS_LEARNED.md # Maintainer feedback and pre-submission checklist
│   ├── SUBMISSION_TRACKER.md    # Versions, lore links, and status
│   └── v1/, v2/, v3/, v4/       # Patch series, cover letters, and review responses
├── aic8800-rfcv3/               # AIC8800 Wi-Fi driver, RFC v3: A5E (SDIO) and A7A (USB, silicon-verified 2026-11-15)
├── aic8800-upstream/            # Older AIC8800 driver (kept pending Avaota A1 / dual-bus retirement decision)
├── project-cubie-a5e/           # Buildroot external tree (BR2_EXTERNAL)
│   ├── board/radxa/             # Board configurations (cubie_a5e, cubie_a7a)
│   ├── configs/                 # Defconfigs (cubie_a5e_defconfig, cubie_a7a_defconfig)
│   ├── dts-overlay/             # Runtime Device Tree Overlays
│   └── package/                 # Custom Buildroot packages (aic8800 driver, riscv-firmware)
├── firmware/                    # Co-processor firmware that exercises the drivers
│   ├── common/                  # Shared startup, HAL, and linker scripts
│   ├── riscv-firmware/          # Bare-metal and FreeRTOS test applications
│   └── hifi4-dsp/               # Cadence HiFi4 Audio DSP firmware
├── tools/                       # Sweep, coverage, and workspace scripts
│   ├── run_full_sweep.py        # Board test suite
│   ├── sweep_gcov.py            # Driver coverage from board and UML runs
│   ├── sync_kernel.sh           # Multi-PC git synchronization
│   └── setup_workspace.sh       # One-time workspace setup
├── test_sweep_results.*         # Latest sweep results (with test_sweep_history.*, uml_coverage.json)
├── wiki/                        # Sweep results page, generated by the sweep
└── docs/                        # Architecture docs, register maps, hardware specs, articles
    ├── architecture/            # RemoteProc & Mailbox developer guides
    ├── platforms/               # Platform bring-up guides
    ├── common/                  # Subsystem guides (AIC8800 Wi-Fi 6, etc.)
    └── articles/                # Long-form engineering articles
```

---

## 📖 Deep-Dive Architecture & Platform Guides

1. **[Linux RemoteProc and Mailbox Driver Guide](docs/architecture/LINUX_REMOTEPROC_AND_MAILBOX_DRIVER_GUIDE.md)**:  
   Architectural invariants, lifecycle state machines, SMP concurrency, and MMIO memory mapping for heterogeneous cores.
2. **[AIC8800 Wi-Fi 6 Common Subsystem Guide](docs/common/WIFI_AIC8800_GUIDE.md)**:  
   Unified dual-transport driver architecture (SDIO on A5E, USB on A7A) and firmware lifecycle.
3. **[Radxa Cubie A7A: XuanTie E902 Remoteproc Rebuild & Bring-Up Guide](docs/platforms/CUBIE_A7A_E902_REBUILD_GUIDE.md)**:  
   RISC-V firmware build, kernel driver, SD card image, hardware verification, and §6: why no BL31 change is needed and the E902 start sequence.
4. **[Radxa Cubie A7A Chronological Hardware Debug Log](docs/platforms/CUBIE_A7A_DEBUG_LOG.md)**:  
   Lab notes on power rail sequencing, PMIC registers, analog PHY tuning, BL31 sector patching, and peripheral bring-up.
5. **[Allwinner Heterogeneous RISC-V Architecture Reference](docs/platforms/ALLWINNER_HETEROGENEOUS_RISCV_REFERENCE.md)**:  
   Silicon reference comparing XuanTie E906 (T527) and XuanTie E902 (A733), bus routing, cache topologies, and security controllers.
6. **[Radxa Cubie A7A Platform Specification](docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)**:  
   Hardware specs, LPDDR5 training, GICv3 interrupt controller, and register maps.
7. **[Radxa Cubie A7A Bring-Up & Known Issues](docs/buildroot/A7A_BRINGUP_AND_KNOWN_ISSUES.md)**:  
   Hardware issue registry, USB DWC3 + FE1.1S hub bring-up, and watchdog reboot resolutions.
8. **[Allwinner A733 Video Encoding Guide](docs/common/A733_VE_ENCODER_GUIDE.md)**:  
   Cedrus hardware video encoder (`0x01c10000`) architecture, zero-copy MIPI CSI-2 → Cedrus DMA-buf pipeline, stateful V4L2 encoder commands, and 1080p performance benchmarks.
9. **[Allwinner A733 Camera & Hardware ISP Guide](docs/common/CAMERA_V4L2_GUIDE.md)**:  
   MIPI CSI-2 receiver configuration, Video4Linux2 Media Controller topology, zero-copy CMA pipelines, and Allwinner Hardware ISP 602 (`0x05900000`) in-line debayering architecture.

---

## 📚 Engineering Articles: Heterogeneous RISC-V on Allwinner Silicon

A 6-part series on the silicon bring-up, Linux `remoteproc` driver development, cache hazards, and bare-metal firmware across the Allwinner T527 and A733:

| Part | Title & Architectural Focus | Platform / Silicon | Links |
| :---: | :--- | :--- | :--- |
| **1** | **Architecture and Memory-Mapped Debugging**<br>• *Silicon taxonomy, T527 register maps, SRAM / TCM boot windows, and JTAG-less DMEM debugging.* | Cubie A5E<br>(T527 / E906) | [Hackster.io](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063) • [Local](docs/articles/part1_heterogeneous_riscv_intro_architecture.md) |
| **2** | **Linux `remoteproc` Driver & Hardware Verification Suite**<br>• *Developing `sunxi_rproc.c`, `.resource_table` trace buffers, and the 6-test validation suite.* | Cubie A5E<br>(T527 / E906) | [Hackster.io](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8) • [Local](docs/articles/part2_building_remoteproc_and_hardware_proof.md) |
| **3** | **Ultra-Low-Latency IPC: Linux to RISC-V Messaging**<br>• *Hardware Mailbox doorbells, zero-copy shared SRAM ring buffers, and VirtIO RPMsg.* | Cubie A5E<br>(T527 / E906) | [Article](docs/articles/part3_baremetal_firmware_ipc_and_coroutines_intro.md) |
| **4** | **C++20 Coroutines vs. FreeRTOS on Bare-Metal RISC-V**<br>• *Stackless coroutine HALO optimization, 19× context switch speedup, sub-microsecond latency.* | Cubie A5E<br>(T527 / E906) | [Article](docs/articles/part4_deep_dive_baremetal_cpp_coroutines.md) |
| **5** | **MicroPython on Both Processors: Experiment First, Compile Later**<br>• *Live interactive MicroPython on E906 co-processor, dynamic pin muxing, sub-microsecond binary telemetry.* | Cubie A5E<br>(T527 / E906) | [Blog Post](https://allthingsembedded.blogspot.com/2026/10/micropython-on-e906.html) • [AbstractX Tree](https://github.com/tcmichals/AbstractX/tree/main/docs/articles/micropython-on-e906) |
| **6** | **Heterogeneous RemoteProc Across Silicon: Cubie A7A (E902) vs. Cubie A5E (E906)**<br>• *Comparative silicon bring-up, D-cache coherency hazards in SRAM, TF-A BL31 security un-gating, unified FreeRTOS port.* | Cubie A7A vs A5E<br>(A733 vs T527) | [Blog Post](https://allthingsembedded.blogspot.com/2026/10/heterogeneous-risc-v-remoteproc-across.html) • [Local](docs/articles/part5_a733_e902_remoteproc_and_heterogeneous_comparison.md) |
| **Feature** | **Turning an 8-Core Desktop Processor into a Real-Time Robotics & Industrial Engine**<br>• *Industrial hacker's guide to unlocking on-die XuanTie RISC-V cores (E902 / E906) across A733 & T527; eliminating external companion MCUs (STM32/RP2040) across robotics, smart agriculture, CNC machine control, and flight systems; autopsy of false assumptions (TrustZone SError myth vs reset vector race); the 16 KB reserved SRAM trap; unlocking stock Arm TF-A BL31 by stripping `scp.fex` from TOC1; mainline `remoteproc` driver; sub-15 µs IPC benchmarks; and roadmap for hardware H.265 & 3 TOPS open NPU.* | Cubie A7A & A5E<br>(A733 / T527) | [Hackaday / Hackster](docs/articles/hackster_a733_e902_remoteproc.md) |

---

## 🤝 Acknowledgments & Credits

* **[The Linux-Sunxi Community](https://linux-sunxi.org/)**: Invaluable documentation, mainline porting efforts, and hardware reverse engineering.
* **[YuzukiHD](https://github.com/YuzukiHD)**: For [SyterKit](https://github.com/YuzukiHD/SyterKit) and [FreeRTOS-HIFI4-DSP](https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP) reference implementations.
* **[Radxa](https://radxa.com/)**: For engineering the Cubie A5E, Cubie A7A, and Cubie A7Z single-board computers.
* **Linux Kernel RemoteProc & Mailbox Subsystem Maintainers**: For thorough architectural reviews and constructive feedback.
