# Allwinner sun55i (T527/A527) & sun60i (A733) Mainline Linux BSP

[![Linux Kernel](https://img.shields.io/badge/Kernel-7.1.0--PREEMPT__RT-blue.svg)](upstream-remoteproc/)
[![Buildroot](https://img.shields.io/badge/Buildroot-2024.02-orange.svg)](project-cubie-a5e/)
[![RISC-V](https://img.shields.io/badge/Co--Processor-XuanTie_E906_%2F_E902-green.svg)](firmware/)
[![Wi-Fi 6](https://img.shields.io/badge/Wireless-AIC8800_Wi--Fi_6_%2B_BT-teal.svg)](docs/common/WIFI_AIC8800_GUIDE.md)
[![Upstream Status](https://img.shields.io/badge/Submissions-v3_Ready_(checkpatch_clean)-success.svg)](upstream-remoteproc/)

This repository provides reproducible **Mainline Linux (7.1+)** bring-up, device drivers, firmware pipelines, and Buildroot integration for the **Radxa Cubie A5E**, **Yuzuki / Pine64 Avaota A1** (Allwinner A527/T527), and **Radxa Cubie A7A / A7Z** (Allwinner A733) single-board computers.

The primary mission of this project is **upstream kernel enablement**: replacing legacy vendor BSP kernels (Linux 5.10 with proprietary binary blobs) with modern, clean, upstream-submissible drivers, pure FOSS stacks, and `PREEMPT_RT` real-time support.

---

## 📚 Multi-Part Engineering Series: Heterogeneous RISC-V on Allwinner Silicon

A comprehensive 6-part engineering publication series documenting the complete silicon bring-up, Linux `remoteproc` driver development, cache hazard mitigation, and bare-metal firmware architectures across Allwinner T527 and A733:

| Part | Title & Architectural Focus | Platform / Silicon | Links |
| :---: | :--- | :--- | :--- |
| **1** | **Architecture and Memory-Mapped Debugging**<br>• *Silicon taxonomy, T527 register maps, SRAM / TCM boot windows, and JTAG-less DMEM debugging.* | Cubie A5E<br>(T527 / E906) | [Hackster.io](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063) • [Local](docs/articles/part1_heterogeneous_riscv_intro_architecture.md) |
| **2** | **Linux `remoteproc` Driver & Hardware Verification Suite**<br>• *Developing `sunxi_rproc.c`, `.resource_table` trace buffers, and the 6-test validation suite.* | Cubie A5E<br>(T527 / E906) | [Hackster.io](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8) • [Local](docs/articles/part2_building_remoteproc_and_hardware_proof.md) |
| **3** | **Ultra-Low-Latency IPC: Linux to RISC-V Messaging**<br>• *Hardware Mailbox doorbells, zero-copy shared SRAM ring buffers, and VirtIO RPMsg.* | Cubie A5E<br>(T527 / E906) | [Article](docs/articles/part3_baremetal_firmware_ipc_and_coroutines_intro.md) |
| **4** | **C++20 Coroutines vs. FreeRTOS on Bare-Metal RISC-V**<br>• *Stackless coroutine HALO optimization, 19× context switch speedup, sub-microsecond latency.* | Cubie A5E<br>(T527 / E906) | [Article](docs/articles/part4_deep_dive_baremetal_cpp_coroutines.md) |
| **5** | **MicroPython on Both Processors: Experiment First, Compile Later**<br>• *Live interactive MicroPython on E906 co-processor, dynamic pin muxing, sub-microsecond binary telemetry.* | Cubie A5E<br>(T527 / E906) | [Blog Post](https://allthingsembedded.blogspot.com/2026/10/micropython-on-e906.html) • [AbstractX Tree](https://github.com/tcmichals/AbstractX/tree/main/docs/articles/micropython-on-e906) |
| **6** | **Heterogeneous RemoteProc Across Silicon: Cubie A7A (E902) vs. Cubie A5E (E906)**<br>• *Comparative silicon bring-up, D-cache coherency hazards in SRAM, TF-A BL31 security un-gating, unified FreeRTOS port.* | Cubie A7A vs A5E<br>(A733 vs T527) | [Blog Post](https://allthingsembedded.blogspot.com/2026/10/heterogeneous-risc-v-remoteproc-across.html) • [Local](docs/articles/part5_a733_e902_remoteproc_and_heterogeneous_comparison.md) |

---

## 📢 Upstream Linux Kernel Patch Submissions

We are actively upstreaming drivers for the Allwinner sun55i/sun60i platform to the official Linux kernel mailing lists (`linux-sunxi@lists.linux.dev`, `linux-remoteproc@vger.kernel.org`, `devicetree@vger.kernel.org`).

All patch series, cover letters, reviewer discussions, and automated AI review tracking are organized in **[`upstream-remoteproc/`](upstream-remoteproc/)**:

| Series | Status | Lore Mailing List Thread | Notes / Feedback |
| :---: | :---: | :--- | :--- |
| **RFC v1** | *Superseded* | [20260922034711.190253-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/) | 7 patches; initial schema & driver feedback |
| **v2** | *Reviewed* | [20260927002021.797069-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/) | 68 KUnit tests added; Sashiko AI review received |
| **v3** | **Ready for Submission** | *Threaded under v1/v2* | **All 17 Sashiko AI review items fixed**; 100% checkpatch clean; builds with 0 errors |

See [`upstream-remoteproc/README.md`](upstream-remoteproc/README.md), [`upstream-remoteproc/reviews/sashiko_protocols.md`](upstream-remoteproc/reviews/sashiko_protocols.md), and [`upstream-remoteproc/reviews/REVIEW_TRACKER.md`](upstream-remoteproc/reviews/REVIEW_TRACKER.md) for the complete protocol execution guide and issue resolution matrix.

---

## 🚦 Hardware Platform Status

| Board | Done | Next |
| :--- | :--- | :--- |
| **Cubie A5E**<br>*(Allwinner A527 / T527)* | • Mainline Linux 7.1 `PREEMPT_RT` boot (U-Boot + TF-A BL31)<br>• XuanTie E906 remoteproc + message box: dynamic sysfs start/stop, trace0 debugfs streaming, exception autopsy capture (`0xDEADF00D`)<br>• VirtIO RPMsg and UIO IPC passing across 3 hardware memory profiles (DDR DRAM, SRAM A3, Userspace UIO)<br>• Automated silicon test sweep passing 100% (`tools/run_full_sweep.py --target a5e`)<br>• Upstream v3 patch series prepared: all 17 Sashiko review items resolved, 68 KUnit tests, 0 checkpatch errors<br>• Wi-Fi 6 AIC8800D80 (SDIO) fully verified (433 Mbps throughput) | • Board-test the v3 message box fixes, then submit v3 upstream<br>• Camera: OV5647 over MIPI CSI-2<br>• Cadence HiFi4 Audio DSP applications |
| **Cubie A7A**<br>*(Allwinner A733)* | • Mainline Linux 7.1 `PREEMPT_RT` boot (U-Boot + TF-A BL31)<br>• USB 2.0 Host + FE1.1S 4-port hub operating at High-Speed (480 Mbps)<br>• Wi-Fi 6 AIC8800 (USB) fully functional with active DHCP lease<br>• TF-A BL31 security un-gating verified on physical SD card (`R_SPC` Port 3 un-gated for E902 boot vector)<br>• XuanTie E902 remoteproc: running out of System SRAM A2 (`0x00040000`, 208 KB), dynamic sysfs lifecycle verified<br>• E902 stop trap resolved: single reset line `<&r_ccu 12>` (`RISCV_CFG_RST`) made optional in driver<br>• Hardware watchdog soft reset verified via `WDT_SRST_REG` (`0x02050008` = `0x16aa0001`) in `sunxi_wdt.c`<br>• Multi-core firmware pipeline: unified `portASM_e90x.S`, software atomics (`atomics.c`), 11 E902 ELFs staged (incl. `testFreeRTOS`)<br>• Autonomous silicon sweep runner implemented (`tools/run_full_sweep.py --target a7a`) | • Execute automated silicon sweep on physical board<br>• Verify userspace reboot after new kernel Image boot<br>• GMAC210 Ethernet TX DMA watchdog fix<br>• Upstream A733 remoteproc compatible series |

> [!NOTE]
> Not yet enabled on either board: MIPI CSI camera capture, display / HDMI output, and hardware video codec. For full task tracking, see [TODO.md](TODO.md).

---

## 🛠️ Supported Boards & Hardware Comparison

| Hardware Feature | Radxa Cubie A5E | Yuzuki Avaota A1 | Radxa Cubie A7A | Radxa Cubie A7Z |
| :--- | :--- | :--- | :--- | :--- |
| **SoC** | Allwinner A527 / T527 | Allwinner A527 / T527 | Allwinner A733 | Allwinner A733 |
| **Form Factor** | Standard SBC (85×56 mm) | Standard SBC (85×56 mm) | Standard SBC (85×56 mm) | Ultra-Compact Zero (65×30 mm) |
| **Host CPU** | 8× Arm Cortex-A55 @ 1.8 GHz | 8× Arm Cortex-A55 @ 1.8 GHz | 4× Cortex-A76 + 4× Cortex-A55 | 4× Cortex-A76 + 4× Cortex-A55 |
| **Co-Processors** | XuanTie E906 (RV32IMAC) + HiFi4 DSP | XuanTie E906 (RV32IMAC) + HiFi4 DSP | XuanTie E902 (RV32EMC) | XuanTie E902 (RV32EMC) |
| **GPR Registers** | 32 registers (`x0`–`x31`) | 32 registers (`x0`–`x31`) | 16 registers (`x0`–`x15`) | 16 registers (`x0`–`x15`) |
| **Co-Proc Memory** | SRAM A3 (32 KB) + DSP RAM (64 KB) | SRAM A3 (32 KB) + DSP RAM (64 KB) | System SRAM A2 (208 KB shared) | System SRAM A2 (208 KB shared) |
| **System RAM** | 2 GiB / 4 GiB LPDDR4X | 2 GiB / 4 GiB LPDDR4X | 4 GiB / 6 GiB LPDDR5 | 2 GiB / 4 GiB LPDDR5 |
| **Wi-Fi / BT** | AIC8800D80 (SDIO) | AIC8800D80 (SDIO) | AIC8800D80 (USB) | AIC8800D80 (SDIO) |
| **Base DTB** | `sun55i-a527-cubie-a5e.dtb` | `sun55i-t527-avaota-a1.dtb` | `sun60i-a733-cubie-a7a.dtb` | `sun60i-a733-cubie-a7z.dtb` |

---

## 🧪 Automated Silicon Verification Suite

The repository includes an autonomous silicon test runner that connects over SSH/Wi-Fi or serial console to comprehensively validate the co-processor stack across both boards:

```bash
# Validate Radxa Cubie A5E (XuanTie E906)
python3 tools/run_full_sweep.py --target a5e --ip 192.168.1.15

# Validate Radxa Cubie A7A (XuanTie E902)
python3 tools/run_full_sweep.py --target a7a --ip 192.168.1.12
```

The test runner automatically validates:
1. **ELF Lifecycle**: Starts and stops co-processor via sysfs `state` without kernel panics or bus hangs.
2. **Crash Autopsy**: Loads `testCrash.elf` to trigger division-by-zero / unaligned traps, verifying register dump capture (`mepc`, `mcause`, `mtval`, `0xDEADF00D`).
3. **VirtIO RPMsg Round-Trip**: Performs 1,000 bidirectional pings (`testPingRpmsg.elf`) verifying 0% drop and sub-millisecond round-trip time.
4. **Stress Testing**: Runs 100,000 throughput messages (`stress_rpmsg`) to guarantee zero buffer stalls.
5. **FreeRTOS Real-Time**: Executes preemptive task scheduling and context switching via unified `portASM_e90x.S`.
6. **Report Generation**: Automatically outputs a structured Markdown test summary (`test_sweep_results_<target>.md`).

---

## 🚀 Quick Start: Building & Flashing

### 1. Workspace Initialization
Clone the repository recursively (including FreeRTOS kernel submodule) and run the setup script:
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
├── upstream-remoteproc/         # Upstream Linux RemoteProc & Mailbox patch hub (v1, v2, v3)
│   ├── README.md                # Submission tracking, lore links, and maintainer guidelines
│   ├── DESIGN_DECISIONS.md      # Binding, memory-region, and architectural decisions
│   ├── UPSTREAM_LESSONS_LEARNED.md # Maintainer feedback and pre-submission checklist
│   ├── reviews/                 # Sashiko AI review items, audit prompts, review tracker
│   └── v1/, v2/, v3/            # Formatted patch series and cover letters
├── project-cubie-a5e/           # Buildroot external tree (BR2_EXTERNAL)
│   ├── board/radxa/             # Board configurations (cubie_a5e, cubie_a7a)
│   │   ├── linux.config         # Minimal PREEMPT_RT kernel configurations
│   │   ├── rootfs-overlay/      # Firmware staging, /etc/modules, diagnostic binaries
│   │   └── genimage.cfg         # Dual-partition SD card layout (FAT32 /boot + EXT4 rootfs)
│   ├── configs/                 # Defconfigs (cubie_a5e_defconfig, cubie_a7a_defconfig)
│   ├── dts-overlay/             # Runtime Device Tree Overlays (cubie-a7a-rproc.dtso, etc.)
│   └── package/                 # Custom Buildroot packages (aic8800 driver, riscv-firmware)
├── firmware/                    # Co-processor firmware trees
│   ├── common/                  # Shared multi-core startup, HAL, and linker scripts
│   │   ├── arch_riscv/          # startup.S, atomics.c polyfills, e906_sram.ld, e902_sram.ld
│   │   ├── hal/                 # msgbox.cpp, rpmsg.cpp, pmp.cpp, timer.cpp, trace.cpp
│   │   └── common.mk            # Core detection, ABI compiler flags (-mabi=ilp32 vs ilp32e)
│   ├── riscv-firmware/          # Bare-metal and FreeRTOS application suites
│   │   ├── apps/                # testBasic, testPingRpmsg, testFreeRTOS, testCrash, ...
│   │   └── tools/               # Memory inspection (riscv_mem.py), test harnesses
│   └── hifi4-dsp/               # Cadence HiFi4 Audio DSP bare-metal firmware
├── docs/                        # Architecture documentation, register maps, hardware specs
│   ├── articles/                # Long-form engineering publications and SVG vector assets
│   ├── platforms/               # Platform bring-up guides (Cubie A7A, Avaota A1)
│   ├── architecture/            # RemoteProc & Mailbox developer guides
│   └── common/                  # Unified subsystem guides (AIC8800 Wi-Fi 6, etc.)
└── tools/                       # Test runners, silicon sweep tools, and workspace sync scripts
    ├── run_full_sweep.py        # Automated multi-core silicon validation suite
    ├── sync_kernel.sh           # Multi-PC git synchronization
    └── setup_workspace.sh       # One-time toolchain and submodule setup
```

---

## 📖 Deep-Dive Architecture & Platform Guides

1. **[Linux RemoteProc and Mailbox Driver Guide](docs/architecture/LINUX_REMOTEPROC_AND_MAILBOX_DRIVER_GUIDE.md)**:  
   Architectural invariants, lifecycle state machines, SMP concurrency, and MMIO memory mapping for heterogeneous cores.
2. **[AIC8800 Wi-Fi 6 Common Subsystem Guide](docs/common/WIFI_AIC8800_GUIDE.md)**:  
   Unified dual-transport driver architecture (SDIO on A5E, USB on A7A), firmware lifecycle, and 433 Mbps silicon verification.
3. **[Radxa Cubie A7A: XuanTie E902 Remoteproc Rebuild & Bring-Up Guide](docs/platforms/CUBIE_A7A_E902_REBUILD_GUIDE.md)**:  
   Step-by-step instructions for RISC-V firmware build, kernel driver compilation, TF-A BL31 security un-gating, SD card image packaging, and hardware verification.
4. **[Radxa Cubie A7A Chronological Hardware Debug Log](docs/platforms/CUBIE_A7A_DEBUG_LOG.md)**:  
   Detailed lab debug notes tracing power rail sequencing, PMIC registers, analog PHY tuning, BL31 sector patching, and peripheral bring-up.
5. **[Allwinner Heterogeneous RISC-V Architecture Reference](docs/platforms/ALLWINNER_HETEROGENEOUS_RISCV_REFERENCE.md)**:  
   Complete silicon reference comparing XuanTie E906 (T527) and XuanTie E902 (A733), bus routing, cache topologies, and security controllers.
6. **[Radxa Cubie A7A Platform Specification](docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)**:  
   Hardware specs, LPDDR5 training, GICv3 interrupt controller, and register maps.
7. **[Radxa Cubie A7A Bring-Up & Known Issues](docs/buildroot/A7A_BRINGUP_AND_KNOWN_ISSUES.md)**:  
   Comprehensive hardware issue registry, USB DWC3 + FE1.1S hub bring-up, and watchdog reboot resolutions.

---

## 🤝 Acknowledgments & Credits

* **[The Linux-Sunxi Community](https://linux-sunxi.org/)**: Invaluable documentation, mainline porting efforts, and hardware reverse engineering.
* **[YuzukiHD](https://github.com/YuzukiHD)**: For [SyterKit](https://github.com/YuzukiHD/SyterKit) and [FreeRTOS-HIFI4-DSP](https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP) reference implementations.
* **[Radxa](https://radxa.com/)**: For engineering the Cubie A5E, Cubie A7A, and Cubie A7Z single-board computers.
* **Linux Kernel RemoteProc & Mailbox Subsystem Maintainers**: For thorough architectural reviews and constructive feedback.
