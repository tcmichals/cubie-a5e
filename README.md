# Allwinner A527/T527 & A733 Mainline Linux Development

[![Linux Kernel](https://img.shields.io/badge/Kernel-7.1.0--PREEMPT__RT-blue.svg)](upstream-remoteproc/)
[![Buildroot](https://img.shields.io/badge/Buildroot-2026.05-orange.svg)](project-cubie-a5e/)
[![RISC-V](https://img.shields.io/badge/Co--Processor-XuanTie_E906_%2F_E902-green.svg)](firmware/)
[![Wi-Fi 6](https://img.shields.io/badge/Wireless-AIC8800_Wi--Fi_6-teal.svg)](docs/common/WIFI_AIC8800_GUIDE.md)
[![Upstream Status](https://img.shields.io/badge/Submissions-v3_sent%2C_v4_in_progress-blue.svg)](upstream-remoteproc/)

Mainline Linux 7.1 (`PREEMPT_RT`) bring-up, Buildroot images, co-processor firmware, and upstreamable drivers for the **Radxa Cubie A5E** (Allwinner A527/T527) and **Radxa Cubie A7A** (Allwinner A733), replacing vendor Linux 5.10/5.15 BSP blobs with mainline-ready drivers.

This platform stack powers [AbstractX](https://github.com/tcmichals/AbstractX). Contributors and AI assistants: start with [AGENTS.md](AGENTS.md) and [TODO.md](TODO.md).

---

## 🚦 Platform Status Overview

### Subsystem Readiness

| Subsystem | Cubie A5E (A527 / T527) | Cubie A7A (A733) |
| :--- | :--- | :--- |
| **Boot & Kernel** | Mainline Linux 7.1 `PREEMPT_RT` (U-Boot + TF-A BL31) | Mainline Linux 7.1 `PREEMPT_RT` on **stock** vendor BL31 (no SCP needed; [rebuild guide §6](docs/platforms/CUBIE_A7A_E902_REBUILD_GUIDE.md)) |
| **RISC-V Co-Processor** | **XuanTie E906**: `remoteproc` + `sun55i-msgbox`, `trace0`, crash capture (`0xDEADF00D`), VirtIO RPMsg & UIO IPC (DRAM + SRAM A3) | **XuanTie E902**: `remoteproc` + 2-bank `sun55i-msgbox` (`rproc@54000` in SRAM A2: `ping_rpmsg` **82.5 µs** avg / **132.0 µs** max; `stress_rpmsg` **20k/20k** 0 drops, **25,474 msgs/s**), `trace0`, 192 KB SRAM A2 (`0x40004000`), 11 ELFs incl. `testFreeRTOS` |
| **Thermal (THS) & CPUFreq** | `sun8i_thermal` (`ths0`/`ths1`: `cpu0`, `cpu4`, `gpu`, `ddr`, ~41–42 °C idle; **87 °C** under 4-core load without fan). No cpufreq yet ([heatsink plan](docs/platforms/CUBIE_A5E_HEATSINK.md)) | `sun8i_thermal` 5 sensors (`cpu-big`, `ddr`, `npu`, `cpu-little`, `gpu`, ~29–39 °C idle); `ccu-sun60i-a733-cpu` + **AXP8191 DVFS** (`416–1794 MHz` little / `416–2002 MHz` big, `800–1050 mV`) + passive cooling (70/90 °C) |
| **Camera, ISP & VPU** | Pending (OV5647 MIPI CSI-2 + ISP 602 backport) | **OV5647 1080p30** (`sun6i-mipi-csi2` → `sun6i-csi` → **Hardware ISP 602** `sun60i-isp` + `libcamera` pipeline/IPA) → **zero-copy Cedrus H.264 & H.265** (`v4l2-compliance` 49/49) |
| **Wi-Fi 6 (AIC8800D80)** | **SDIO**: `aic8800-rfcv3` (`aic8800_bsp` + `aic8800_fdrv`) | **USB**: `aic8800-rfcv3` + `aic_load_fw` (`a69c:8d80` → `8d81`), 5 GHz Wi-Fi 6 (`80 MHz`) + DHCP verified |
| **USB, Ethernet & LEDs** | USB, Ethernet, standard DT peripherals | USB 2.0 High-Speed + FE1.1S hub; active-low green LED (`PJ26`); `WDT_SRST_REG` reset; GMAC210 TX DMA fix pending |
| **Audio DSP / NPU** | Cadence HiFi4 DSP framework (`dsp-hifi4/`) | **3.0 TOPS NPU (`npu@3600000`, `VIP9000Nano-DI` `0x9202`, `HW_V83`)**: mainline `etnaviv` DRM live (`840 MHz`, `800 mV` `dcdc2`, `GIC_SPI 65`); **PPU compute shader verified** (`0.092–0.220 ms`, 5/5 bit-exact IEEE-754 float32); **MMUv2 PDMODE** + **Mesa 26.1.8 Teflon** (`TP_ENGINE=0` + `HW_V83` slice-stride & FP post-scale) built & staged for final board sweep |

### Next Priorities

- **Cubie A7A**: Run final post-power-cycle live verification of kernel MMUv2 PDMODE + Mesa 26.1.8 Teflon `npu-smoke-test`; root-cause 1-second Linux-side RPMsg stall & `stress_rpmsg` drops ([TODO.md §2](TODO.md)); add `sun50i-cpufreq-nvmem` multi-bin (`vfXXXX`) selection; fix GMAC210 TX DMA timeout; build A733 BL31 from source; upstream `sun60i-isp` RFC to `linux-media`.
- **Cubie A5E**: Port v4 remoteproc/msgbox fixes to `cubie-linux-7.1`, run full v4 board sweep + KUnit, and submit v4; re-test `aic8800-rfcv3` SDIO on board; add CPU CCU/OPP/cooling; bring up OV5647 + backport ISP 602.

> [!NOTE]
> Display / HDMI output is out of scope for this repository. For complete task tracking, see [TODO.md](TODO.md).

---

## 📷 A7A Camera, Hardware ISP 602 & Zero-Copy Video Encoding

OV5647 1080p30 runs end-to-end on mainline `sun6i-mipi-csi2` → `sun6i-csi` → **Hardware ISP 602 (`sun60i-isp`)** (in-line ISP, CSI DMA0 NV12, `params`/`stats` metadata nodes) with a dedicated `rkisp1`-style `libcamera` `sun60i-isp` pipeline handler and IPA (Software ISP removed).

| Pipeline / Benchmark | Result | CPU Load | Notes |
| :--- | :--- | :--- | :--- |
| **Standalone 1080p H.264 (`sunxi-cedrus`)** | **78.3 fps** (`12.7 ms/frame`) | — | Hardware VPU (`0x01c10000`) |
| **Live CSI → VE_ENC H.264 (zero-copy `dma-buf`)** | **30.02 fps** | **0.89%** (1 core) | 0 decode errors |
| **Live CSI → VE_ENC H.265 CBR/VBR (zero-copy)** | **30.00 fps** (`1 & 4 Mbps`) | **1.55%** (1 core) | `v4l2-compliance` 49/49; 0 errors in `avdec_h265` / `libde265dec` |

- **Camera & ISP Bring-Up Guide**: [docs/common/CAMERA_V4L2_GUIDE.md](docs/common/CAMERA_V4L2_GUIDE.md)
- **`libcamera` `sun60i-isp` Pipeline & IPA Patch**: [0001-libcamera-add-sun60i-isp-pipeline-handler-and-IPA.patch](project-cubie-a5e/board/radxa/cubie_a7a/patches/packages/libcamera/0001-libcamera-add-sun60i-isp-pipeline-handler-and-IPA.patch)
- **Hardware Video Encoder Guide (H.264 / H.265)**: [docs/common/A733_VE_ENCODER_GUIDE.md](docs/common/A733_VE_ENCODER_GUIDE.md)
- **Silicon Bring-Up Logs & Captures**: [logs/a7a-ve-enc-20261114/RESULTS.md](logs/a7a-ve-enc-20261114/RESULTS.md)

---

## 🧠 A7A 3.0 TOPS NPU (`etnaviv` DRM + PPU Compute + Mesa 26.1.8 Teflon)

The Allwinner A733's **VeriSilicon `VIP9000Nano-DI`** (`GC9000 rev 0x9202`, `customer_id 0x1000003b`, `HW_V83`, **8 NN cores**, **1 PPU shader core**, **`TP_ENGINE = 0`**, **512 KB VIP SRAM**) runs on mainline Linux 7.1 `etnaviv` (`/dev/dri/renderD128`) without proprietary VIPLite blobs or vendor `.nb` toolchains.

| Workload / Verification | Result | Notes |
| :--- | :--- | :--- |
| **Mainline Kernel Bring-Up (`3600000.npu`)** | **Live on silicon** | `PD_NPU` (domain 4), `dcdc2 = 800 mV`, `pll-npu = 840 MHz`, `mbus-npu = 600 MHz`, `bus-npu = 200 MHz`, 3 resets (`0x0b04`), `GIC_SPI 65`, NPU temp `35.9 °C` |
| **Path B: Vivante PPU Compute Shader (`npu_ppu_compute_test.c`)** | **0.092–0.220 ms** | **5/5 bit-exact IEEE-754 float32** outputs (`fill = 1, 2, 3, 5, 7`), 0 MMU faults, 0 hangs |
| **Path A: `HW_V83` NN Engine + Mesa 26.1.8 Teflon** | **0.11 ms** Conv2D dispatch; fixes built & staged | Live silicon sweeps proved `TP_ENGINE = 0`, `NN_CMD_SUPPORT_SLICE = 1` (`y_stride = x * y`), `FP_INIMAGE_POST_SCALE = 1` (raw IEEE-754 exponent), and `MMU_PAGE_DESCRIPTOR = 1` (PDMODE `0x003B4` + `0x00388 = 0x21`); kernel PDMODE + Mesa `0002` patch built & staged for final `npu-smoke-test` run |

- **NPU Acceleration Guide & Community Q&A / Fixes**: [docs/common/NPU_ACCELERATION_GUIDE.md](docs/common/NPU_ACCELERATION_GUIDE.md)
- **Deep-Dive Hackster.io Article (Silicon Dataflow, `HW_V83` Findings & Community Issues)**: [docs/articles/hackster_a733_vip9000_npu_etnaviv_teflon.md](docs/articles/hackster_a733_vip9000_npu_etnaviv_teflon.md)
- **Mesa 26.1.8 Teflon Patches**: [0001-etnaviv-hwdb](project-cubie-a5e/board/radxa/cubie_a7a/patches/packages/mesa3d/0001-etnaviv-hwdb-add-allwinner-npu-feature-database.patch), [0002-hw-v83-and-no-tp](project-cubie-a5e/board/radxa/cubie_a7a/patches/packages/mesa3d/0002-etnaviv-teflon-add-hw-v83-and-no-tp-support.patch)
- **Standalone PPU Compute Tool**: [npu_ppu_compute_test.c](project-cubie-a5e/board/radxa/cubie_a7a/src/npu_ppu_compute_test.c)

---

## 🌡️ Thermal Sensors (THS) & CPUFreq DVFS

Both boards report CPU, GPU, and DRAM temperatures through mainline `sun8i_thermal` (`/sys/class/thermal/thermal_zone*/temp` and `hwmon`).

| Parameter | Cubie A5E (A527 / T527) | Cubie A7A (A733) |
| :--- | :--- | :--- |
| **Sensors / Zones** | `ths0` + `ths1`: `cpu0`, `cpu4`, `gpu`, `ddr` | THS at `0x02522000`, 5 sensors: `cpu-big`, `ddr`, `npu`, `cpu-little`, `gpu` |
| **Clocks, IRQ & Calibration** | A523 CCU `bus-ths`, `gpadc0/1`; SID `0x38` & `0x44` | CCU `THS0_BGR` `0x0fe4`, `GPADC0_24M` `0x0fc0`; SPI 39 (IRQ 71); SID `0x44` |
| **CPUFreq & PMIC** | Pending ([CPUFREQ_DVFS_PLAN.md](docs/common/CPUFREQ_DVFS_PLAN.md)) | `ccu-sun60i-a733-cpu` (`0x08870000`, 26 MHz DCXO) + `axp20x-regulator` (AXP8191 `dcdc5`/`dcdc3`, `vf0400`: `416–1794 / 2002 MHz`, `800–1050 mV`) |
| **Trips & Cooling** | A523 DT; no cpufreq cooling yet | Passive 70 °C (`cpu-little`) / 90 °C (`cpu-big`/`gpu`/`npu`/`ddr`) via `cpufreq-cpu0` & `cpufreq-cpu6`; critical 110 °C |
| **Measured Thermals** | ~41–42 °C idle; **87 °C** (4 cores, no fan), ~47 °C with fan | ~29–39 °C idle; **61.4 °C** peak (3 min, 8 cores, fan fitted) |

- **A5E Thermal Results & Heatsink Plan**: [logs/a5e-thermal-20261111/RESULTS.md](logs/a5e-thermal-20261111/RESULTS.md), [CUBIE_A5E_HEATSINK.md](docs/platforms/CUBIE_A5E_HEATSINK.md)
- **A7A THS Bring-Up Notes**: [CUBIE_A7A_DEBUG_LOG.md](docs/platforms/CUBIE_A7A_DEBUG_LOG.md) (Milestone 22)

---

## 🛠️ Hardware Platform Comparison

| Hardware Feature | Radxa Cubie A5E | Radxa Cubie A7A |
| :--- | :--- | :--- |
| **SoC** | Allwinner A527 / T527 | Allwinner A733 |
| **Host CPU** | 8× Cortex-A55 @ 1.8 GHz | 4× Cortex-A76 + 4× Cortex-A55 |
| **Co-Processor** | XuanTie E906 (RV32IMAC, 32 GPRs) + HiFi4 DSP | XuanTie E902 (RV32EMC, 16 GPRs) |
| **Co-Proc Memory** | SRAM A3 (32 KB) + DSP RAM (64 KB) | System SRAM A2 (192 KB usable at `0x40004000`; first 16 KB CPUS-only) |
| **System RAM** | 2 GiB / 4 GiB LPDDR4X | 4 GiB / 6 GiB LPDDR5 |
| **Wi-Fi 6 / BT** | AIC8800D80 (SDIO) | AIC8800D80 (USB `a69c:8d80` → `8d81`) |
| **Base DTB** | `sun55i-a527-cubie-a5e.dtb` | `sun60i-a733-cubie-a7a.dtb` |

*(Configurations for Yuzuki Avaota A1 and Radxa Cubie A7Z are also present in `project-cubie-a5e/configs/`, outside the primary test goal.)*

---

## 📢 Upstream Linux Kernel Submissions

Patch series, cover letters, and maintainer review tracking for `sun55i`/`sun60i` remoteproc and mailbox live in **[`upstream-remoteproc/`](upstream-remoteproc/)**:

| Series | Status | Lore Mailing List Thread | Notes |
| :---: | :---: | :--- | :--- |
| **RFC v1** | *Superseded* | [20260922034711.190253-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/) | 7 patches; initial schema and driver feedback |
| **v2** | *Reviewed* | [20260927002021.797069-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/) | KUnit tests added |
| **v3** | *Sent 2026-11-01, reviewed* | [20261101185735.762559-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/20261101185735.762559-1-tcmichals@gmail.com/) | Review responses tracked for v4 |
| **v4** | **In progress** | Not sent | Fixes on `cubie-linux-7.1`; board test and pre-send checks pending |

Design rationale and pre-submission gates: [`DESIGN_DECISIONS.md`](upstream-remoteproc/DESIGN_DECISIONS.md), [`UPSTREAM_LESSONS_LEARNED.md`](upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md), [`SUBMISSION_TRACKER.md`](upstream-remoteproc/SUBMISSION_TRACKER.md).

---

## 🧪 Verification Standards & Automated Silicon Sweep

| Check | Cubie A5E | Cubie A7A |
| :--- | :--- | :--- |
| **KUnit** | 63/63 on v3; v4 pending board run | Pending (no camera KUnit tests yet) |
| **Full Board Sweep** | 45/45 on v3 (2026-10-29); v4 pending | `--target a7a` covers E902 remoteproc only |
| **Driver Coverage** | `msgbox` 96.77%, `rproc` 89.76% (v3, board + UML) | Not yet recorded |
| **RT / Lockdep / `W=1`** | Done on v3 (GCC + clang `W=1`, checkpatch, `dt_binding_check`) | Pending for A7A-only patches |

Run the automated sweep over SSH/serial (results in [test_sweep_results.md](test_sweep_results.md) and [the wiki page](wiki/Full-Sweep-Tests.md)):

```bash
# Radxa Cubie A5E (XuanTie E906)
python3 tools/run_full_sweep.py --target a5e --ip <board-ip>

# Radxa Cubie A7A (XuanTie E902)
python3 tools/run_full_sweep.py --target a7a --ip <board-ip>
```

The sweep validates KUnit suites, sysfs ELF lifecycle, trap/crash capture (`0xDEADF00D`), VirtIO RPMsg (1,000-pkt ping + 100k stress), UIO/FreeRTOS/DRAM profiles, and clean `dmesg`.

---

## 🚀 Quick Start: Kernel Trees, Building & Flashing

### 1. Kernel Trees & Branches

Set per board in `bld.<board>/local.mk` by `tools/setup_workspace.sh`:

| Board | Kernel Tree | Branch | Purpose |
| :--- | :--- | :--- | :--- |
| **Cubie A5E** | `linux-v4-71` (worktree of `linux-cubie`) | `cubie-linux-7.1` | v4 remoteproc and message box fixes |
| **Cubie A7A** | `linux-cubie` | `a7a_remoteproc_camera_usb` | E902 remoteproc, camera/ISP/VPU, USB, AXP8191 DVFS |

*(Pre-cleanup material from 2026-11-07 is preserved on [`archive/pre-cleanup-20261107`](https://github.com/tcmichals/cubie-a5e/tree/archive/pre-cleanup-20261107).)*

### 2. Initialize, Sync & Build SD Card Images

```bash
git clone --recursive git@github.com:tcmichals/cubie-a5e.git
cd cubie-a5e
./tools/setup_workspace.sh

# Multi-PC sync helper (status | push | pull | rebuild)
./tools/sync_kernel.sh status

# Build full Buildroot SD card images
make -C bld.a5e    # -> bld.a5e/images/sdcard.img
make -C bld.a7a    # -> bld.a7a/images/sdcard.img

# Flash to microSD (verify /dev/sdX carefully)
sudo dd if=bld.a7a/images/sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

---

## 📁 Repository Structure

```text
cubie-a5e/
├── AGENTS.md                    # Entry point for AI tools; rules live in .agents/rules/
├── TODO.md                      # Single centralized task tracker
├── upstream-remoteproc/         # Upstream RemoteProc & Mailbox patch series (v1–v4) and lore logs
├── aic8800-rfcv3/               # AIC8800 Wi-Fi 6 RFC v3 driver (SDIO for A5E, USB for A7A)
├── project-cubie-a5e/           # Buildroot BR2_EXTERNAL (boards, configs, dts-overlay, packages)
├── firmware/                    # Co-processor firmware (common HAL, bare-metal/FreeRTOS E906/E902, HiFi4 DSP)
├── tools/                       # run_full_sweep.py, sweep_gcov.py, sync_kernel.sh, setup_workspace.sh
├── logs/                        # Dated hardware test and benchmark logs
├── wiki/                        # Generated sweep reports
└── docs/                        # Architecture, platform, subsystem guides, and engineering articles
```

---

## 📖 Documentation Index

### Architecture & Subsystem Guides
- **[Linux RemoteProc and Mailbox Driver Guide](docs/architecture/LINUX_REMOTEPROC_AND_MAILBOX_DRIVER_GUIDE.md)** — Lifecycle state machines, SMP concurrency, and MMIO mapping.
- **[Allwinner A733 Camera & Hardware ISP Guide](docs/common/CAMERA_V4L2_GUIDE.md)** — MIPI CSI-2, V4L2 Media Controller, zero-copy CMA, and ISP 602 (`sun60i-isp`) + `libcamera`.
- **[Allwinner A733 Video Encoding Guide](docs/common/A733_VE_ENCODER_GUIDE.md)** — Cedrus H.264/H.265 encoder (`0x01c10000`) and zero-copy CSI → VE `dma-buf` pipelines.
- **[AIC8800 Wi-Fi 6 Subsystem Guide](docs/common/WIFI_AIC8800_GUIDE.md)** — `aic8800-rfcv3` SDIO/USB architecture and `aic_load_fw` firmware lifecycle.
- **[CPUFreq / DVFS Plan](docs/common/CPUFREQ_DVFS_PLAN.md)** — A733 AXP8191 & A527 OPP/speed-binning roadmap.

### Platform References & Debug Logs
- **[Cubie A7A: XuanTie E902 Rebuild & Bring-Up Guide](docs/platforms/CUBIE_A7A_E902_REBUILD_GUIDE.md)** — Firmware build, stock BL31 (`scp.fex` removal), and E902 start sequence.
- **[Cubie A7A Chronological Hardware Debug Log](docs/platforms/CUBIE_A7A_DEBUG_LOG.md)** — Rail sequencing, PMIC registers, PHY tuning, and peripheral bring-up.
- **[Allwinner Heterogeneous RISC-V Reference](docs/platforms/ALLWINNER_HETEROGENEOUS_RISCV_REFERENCE.md)** — E906 (T527) vs E902 (A733) silicon comparison.
- **[Cubie A7A Platform Specification](docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)** & **[Known Issues](docs/buildroot/A7A_BRINGUP_AND_KNOWN_ISSUES.md)** — Hardware specs, GICv3, USB DWC3, and watchdog notes.

---

## 📚 Engineering Articles: Heterogeneous RISC-V on Allwinner Silicon

| Part | Title & Focus | Platform | Links |
| :---: | :--- | :--- | :--- |
| **1** | **Architecture and Memory-Mapped Debugging** — *T527 register maps, SRAM/TCM boot windows, JTAG-less DMEM debugging.* | Cubie A5E | [Hackster.io](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063) • [Local](docs/articles/part1_heterogeneous_riscv_intro_architecture.md) |
| **2** | **Linux `remoteproc` Driver & Verification Suite** — *`sunxi_rproc.c`, `.resource_table` trace buffers, 6-test suite.* | Cubie A5E | [Hackster.io](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8) • [Local](docs/articles/part2_building_remoteproc_and_hardware_proof.md) |
| **3** | **Ultra-Low-Latency IPC: Linux to RISC-V Messaging** — *Hardware mailbox doorbells, zero-copy SRAM rings, VirtIO RPMsg.* | Cubie A5E | [Local](docs/articles/part3_baremetal_firmware_ipc_and_coroutines_intro.md) |
| **4** | **C++20 Coroutines vs. FreeRTOS on Bare-Metal RISC-V** — *HALO optimization, 19× context-switch speedup.* | Cubie A5E | [Local](docs/articles/part4_deep_dive_baremetal_cpp_coroutines.md) |
| **5** | **MicroPython on Both Processors** — *Interactive MicroPython on E906, dynamic pin muxing, binary telemetry.* | Cubie A5E | [Blog Post](https://allthingsembedded.blogspot.com/2026/10/micropython-on-e906.html) • [AbstractX](https://github.com/tcmichals/AbstractX/tree/main/docs/articles/micropython-on-e906) |
| **6** | **Heterogeneous RemoteProc: Cubie A7A (E902) vs. A5E (E906)** — *Cache hazards, BL31 un-gating, unified FreeRTOS port.* | A7A vs A5E | [Blog Post](https://allthingsembedded.blogspot.com/2026/10/heterogeneous-risc-v-remoteproc-across.html) • [Local](docs/articles/part5_a733_e902_remoteproc_and_heterogeneous_comparison.md) |
| **Feature** | **Turning an 8-Core Desktop Processor into a Real-Time Robotics & Industrial Engine** — *E902/E906 unlock, `scp.fex` discovery, 16 KB SRAM trap, sub-15 µs IPC, H.265 & NPU roadmap.* | A7A & A5E | [Local](docs/articles/hackster_a733_e902_remoteproc.md) |

---

## 🤝 Acknowledgments & Credits

- **[The Linux-Sunxi Community](https://linux-sunxi.org/)**: Documentation, mainline porting, and hardware reverse engineering.
- **[YuzukiHD](https://github.com/YuzukiHD)**: [SyterKit](https://github.com/YuzukiHD/SyterKit) and [FreeRTOS-HIFI4-DSP](https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP) references.
- **[Radxa](https://radxa.com/)**: Engineering the Cubie A5E, Cubie A7A, and Cubie A7Z boards.
- **Linux Kernel RemoteProc & Mailbox Maintainers**: Architectural reviews and feedback.
