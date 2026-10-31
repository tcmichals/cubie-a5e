# Allwinner sun55i (T527/A527) & sun60i (A733) Mainline Linux BSP

This repository provides reproducible **Mainline Linux (7.1+)** bring-up, device drivers, and Buildroot integration for the **Radxa Cubie A5E**, **Yuzuki / Pine64 Avaota A1** (Allwinner A527/T527), and **Radxa Cubie A7A / A7Z** (Allwinner A733) single-board computers.

The primary mission of this project is **upstream kernel enablement**: replacing legacy vendor BSP kernels (Linux 5.10 with out-of-tree blobs) with modern, clean, upstream-submissible drivers, pure FOSS stacks, and `PREEMPT_RT` real-time support.

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

## 🚦 Status

| Board | Done | Next |
| :--- | :--- | :--- |
| **Cubie A5E**<br>(A527 / T527) | • Linux 7.1 `PREEMPT_RT` boot (U-Boot + TF-A)<br>• XuanTie E906 remoteproc + message box: start/stop dynamic lifecycle, trace0 debugfs streaming, exception autopsy capture (`0xDEADF00D`)<br>• VirtIO RPMsg and UIO IPC passing across 3 hardware profiles (DDR DRAM, SRAM A3, Userspace UIO)<br>• Automated silicon test sweep runner passing 100% via `tools/run_full_sweep.py --target a5e`<br>• Upstream v3 patch series prepared: all 17 Sashiko review items resolved, 68 KUnit tests, 0 checkpatch errors<br>• Wi-Fi 6 AIC8800 (SDIO) | • Board-test the v3 message box fixes, then submit v3 upstream<br>• Camera: OV5647 over MIPI CSI-2<br>• E906 D-cache coherency maintenance<br>• Cadence HiFi4 Audio DSP applications |
| **Cubie A7A**<br>(A733) | • Linux 7.1 `PREEMPT_RT` boot (U-Boot + TF-A)<br>• USB 2.0 host + FE1.1S 4-port hub operating at High-Speed (480 Mbps)<br>• Wi-Fi 6 AIC8800 (USB) fully functional with active DHCP lease<br>• TF-A BL31 security un-gating verified on physical SD card (`R_SPC` Port 3 un-gated for E902 boot vector)<br>• XuanTie E902 remoteproc: running out of System SRAM A2 (`0x00040000`, 208 KB), dynamic sysfs lifecycle verified<br>• E902 stop trap resolved: mapped `core` reset to `<&r_ccu 12>` (`RISCV_CFG_RST`) instead of CPU configuration reset<br>• Hardware watchdog soft reset verified via `WDT_SRST_REG` (`0x02050008` = `0x16aa0001`) in `sunxi_wdt.c`<br>• Multi-core firmware pipeline: unified `portASM_e90x.S`, software atomics (`atomics.c`), 11 E902 ELFs staged (incl. `testFreeRTOS`)<br>• Autonomous silicon sweep runner implemented (`tools/run_full_sweep.py --target a7a`) | • Execute automated silicon sweep on physical board<br>• Verify userspace reboot after new kernel Image boot<br>• GMAC210 Ethernet TX DMA watchdog fix<br>• Upstream A733 remoteproc compatible series |

Not working on either board yet: MIPI CSI cameras, display / HDMI, hardware video codec.
Task details: [TODO.md](TODO.md). A7A history: [docs/platforms/CUBIE_A7A_DEBUG_LOG.md](docs/platforms/CUBIE_A7A_DEBUG_LOG.md).

---

## 🛠️ Supported Boards

| Hardware Feature | Radxa Cubie A5E | Yuzuki Avaota A1 | Radxa Cubie A7A | Radxa Cubie A7Z |
| :--- | :--- | :--- | :--- | :--- |
| **SoC** | Allwinner A527 / T527 | Allwinner A527 / T527 | Allwinner A733 | Allwinner A733 |
| **Form Factor** | Standard SBC (85×56 mm) | Standard SBC (85×56 mm) | Standard SBC (85×56 mm) | Ultra-Compact Zero (65×30 mm) |
| **CPU Architecture** | 8× Arm Cortex-A55 @ 1.8 GHz | 8× Arm Cortex-A55 @ 1.8 GHz | 4× Cortex-A76 + 4× Cortex-A55 | 4× Cortex-A76 + 4× Cortex-A55 |
| **Co-Processors** | XuanTie E906 RISC-V + HiFi4 DSP | XuanTie E906 RISC-V + HiFi4 DSP | XuanTie E902 RISC-V | XuanTie E902 RISC-V |
| **System RAM** | 2 GiB / 4 GiB LPDDR4X | 2 GiB / 4 GiB LPDDR4X | 4 GiB / 6 GiB LPDDR5 | 2 GiB / 4 GiB LPDDR5 |
| **Wi-Fi / BT** | AIC8800D80 (SDIO) | AIC8800D80 (SDIO) | AIC8800D80 (USB) | AIC8800D80 (SDIO) |
| **Device Tree File** | `allwinner/sun55i-a527-cubie-a5e.dtb` | `allwinner/sun55i-t527-avaota-a1.dtb` | `allwinner/sun60i-a733-cubie-a7a.dtb` | `allwinner/sun60i-a733-cubie-a7z.dtb` |

---

## 🚀 Quick Start: Building the OS

### 1. One-Time Machine Setup
Clone the repository recursively (including FreeRTOS kernel submodule) and run the setup script to initialize the workspace and toolchains:
```bash
git clone --recursive git@github.com:tcmichals/cubie-a5e.git
cd cubie-a5e

# Or if already cloned:
git submodule update --init --recursive

# When pulling updates:
git pull --recurse-submodules

./tools/setup_workspace.sh
```

### 2. Multi-PC Git Synchronization
If you develop across multiple machines, use `tools/sync_kernel.sh` to keep your local kernel repository in sync:
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
*(Replace `/dev/sdX` with your target SD card device; double check to avoid data loss).*

---

## 📁 Repository Structure

```text
cubie-a5e/
├── upstream-remoteproc/         # Upstream Linux RemoteProc & Mailbox patch hub (v1, v2, v3)
│   ├── README.md                # Submission tracking, lore links, and guidelines
│   ├── scripts/                 # Automated review download and tracker generation scripts
│   ├── reviews/                 # Audit prompts, review tracker matrix, incoming emails
│   ├── v1/, v2/, v3/            # Formatted patch sets and cover letters
│   └── lore_emails/             # Archived raw emails from lore.kernel.org
├── project-cubie-a5e/           # Buildroot external tree (BR2_EXTERNAL)
│   ├── board/radxa/cubie_a5e/   # Linux kernel configs, genimage configs, rootfs overlays
│   └── package/                 # Custom Buildroot packages (AIC8800 driver, etc.)
├── riscv-firmware/              # XuanTie E906 bare-metal firmware & test applications
├── dsp-hifi4/                   # Tensilica HiFi4 Audio DSP bare-metal firmware
├── docs/                        # Architecture documentation, register maps, hardware specs
│   ├── platforms/               # Platform bring-up guides (Cubie A7A, Avaota A1)
│   ├── architecture/            # RemoteProc & Mailbox developer guides
│   └── reviews/                 # Hardware audit bundles (A7A USB, etc.)
└── tools/                       # Workspace setup, kernel synchronization, and test scripts
```

---

## 📖 Deep-Dive Architecture Guides

1. **[Linux RemoteProc and Mailbox Driver Guide](docs/architecture/LINUX_REMOTEPROC_AND_MAILBOX_DRIVER_GUIDE.md)**:
   Architectural invariants, lifecycle state machines, SMP concurrency, and MMIO memory mapping for heterogeneous cores.
2. **[AIC8800 Wi-Fi 6 Common Subsystem Guide](docs/common/WIFI_AIC8800_GUIDE.md)**:
   Unified dual-transport driver architecture (SDIO on A5E, USB on A7A), firmware lifecycle, and 433 Mbps silicon verification.
3. **[Radxa Cubie A7A Bring-Up & Known Issues](docs/buildroot/A7A_BRINGUP_AND_KNOWN_ISSUES.md)**:
   Comprehensive hardware issue registry, USB DWC3 + FE1.1S hub bring-up, and watchdog reboot resolutions.
4. **[Radxa Cubie A7A Chronological Hardware Debug Log](docs/platforms/CUBIE_A7A_DEBUG_LOG.md)**:
   Detailed lab debug notes tracing power rail sequencing, PMIC registers, analog PHY tuning, and peripheral bring-up.
5. **[Radxa Cubie A7A Platform Specification](docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)**:
   Hardware specs, LPDDR5 training, GICv3 interrupt controller, and register maps.
6. **[Radxa Cubie A7A: XuanTie E902 Remoteproc Rebuild & Bring-Up Guide](docs/platforms/CUBIE_A7A_E902_REBUILD_GUIDE.md)**:
   Step-by-step instructions for RISC-V firmware build, kernel driver compilation, TF-A BL31 security un-gating, SD card image packaging, and hardware verification.


---

## 🤝 Acknowledgments & Credits

* **[The Linux-Sunxi Community](https://linux-sunxi.org/)**: Invaluable documentation, mainline porting efforts, and hardware reverse engineering.
* **[YuzukiHD](https://github.com/YuzukiHD)**: For [SyterKit](https://github.com/YuzukiHD/SyterKit) and [FreeRTOS-HIFI4-DSP](https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP) reference implementations.
* **[Radxa](https://radxa.com/)**: For engineering the Cubie A5E, Cubie A7A, and Cubie A7Z single-board computers.
* **Linux Kernel RemoteProc & Mailbox Subsystem Maintainers**: For thorough architectural reviews and feedback.
