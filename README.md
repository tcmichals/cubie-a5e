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
| **Cubie A7A**<br>*(Allwinner A733)* | • Mainline Linux 7.1 `PREEMPT_RT` boot (U-Boot + TF-A BL31)<br>• USB 2.0 host + FE1.1S hub at High-Speed (480 Mbps)<br>• Wi-Fi 6 AIC8800 (USB) with a DHCP lease, on the older `aic8800-upstream` driver<br>• TF-A BL31 un-gating verified on SD card (`R_SPC` Port 3 for the E902 boot vector)<br>• XuanTie E902 remoteproc in System SRAM A2 (`0x00040000`, 208 KB), sysfs lifecycle verified<br>• Watchdog soft reset via `WDT_SRST_REG` in `sunxi_wdt.c`<br>• 11 E902 ELFs staged (incl. `testFreeRTOS`)<br>• Camera capture (CSI-2) as work in progress | • Board-test Wi-Fi USB on the RFC v3 driver, then retire `aic8800-upstream`<br>• GMAC210 Ethernet TX DMA timeout fix<br>• Bring the v3 and v4 remoteproc and msgbox fixes to this board's branch<br>• Extend the sweep to USB, Wi-Fi, camera and watchdog; run the full test set<br>• Finish camera capture |

> [!NOTE]
> Not yet enabled on either board: display / HDMI output and hardware video codec. For full task tracking, see [TODO.md](TODO.md).

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
| **Base DTB** | `sun55i-a527-cubie-a5e.dtb` | `sun60i-a733-cubie-a7a.dtb` |

The tree also has configuration for the Yuzuki Avaota A1 and the Radxa Cubie A7Z, which are not part of the test goal.

---

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
├── aic8800-rfcv3/               # AIC8800 Wi-Fi driver, RFC v3 (A5E on SDIO)
├── aic8800-upstream/            # Older AIC8800 driver (A7A on USB, until the port is board-tested)
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
   RISC-V firmware build, kernel driver compilation, TF-A BL31 un-gating, SD card image packaging, and hardware verification.
4. **[Radxa Cubie A7A Chronological Hardware Debug Log](docs/platforms/CUBIE_A7A_DEBUG_LOG.md)**:  
   Lab notes on power rail sequencing, PMIC registers, analog PHY tuning, BL31 sector patching, and peripheral bring-up.
5. **[Allwinner Heterogeneous RISC-V Architecture Reference](docs/platforms/ALLWINNER_HETEROGENEOUS_RISCV_REFERENCE.md)**:  
   Silicon reference comparing XuanTie E906 (T527) and XuanTie E902 (A733), bus routing, cache topologies, and security controllers.
6. **[Radxa Cubie A7A Platform Specification](docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)**:  
   Hardware specs, LPDDR5 training, GICv3 interrupt controller, and register maps.
7. **[Radxa Cubie A7A Bring-Up & Known Issues](docs/buildroot/A7A_BRINGUP_AND_KNOWN_ISSUES.md)**:  
   Hardware issue registry, USB DWC3 + FE1.1S hub bring-up, and watchdog reboot resolutions.

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

---

## 🤝 Acknowledgments & Credits

* **[The Linux-Sunxi Community](https://linux-sunxi.org/)**: Invaluable documentation, mainline porting efforts, and hardware reverse engineering.
* **[YuzukiHD](https://github.com/YuzukiHD)**: For [SyterKit](https://github.com/YuzukiHD/SyterKit) and [FreeRTOS-HIFI4-DSP](https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP) reference implementations.
* **[Radxa](https://radxa.com/)**: For engineering the Cubie A5E, Cubie A7A, and Cubie A7Z single-board computers.
* **Linux Kernel RemoteProc & Mailbox Subsystem Maintainers**: For thorough architectural reviews and constructive feedback.
