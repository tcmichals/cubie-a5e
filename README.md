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

## 🚦 Hardware & Mainline Driver Status Matrix

> [!IMPORTANT]
> **Please read carefully if you are evaluating this repository:**
> Mainline Linux support for Allwinner A527/T527 and A733 is under active bring-up. Below is an honest, verified status matrix of what is working today on real silicon versus what is still under active development.

| Subsystem | Cubie A5E (A527/T527) | Cubie A7A / A7Z (A733) | Driver / Implementation Status |
| :--- | :---: | :---: | :--- |
| **Bootloader & BROM** | **Working** | **Working** | U-Boot 2026.01 + TF-A BL31 + OP-TEE + auto-training LPDDR5/LPDDR4X |
| **Mainline Kernel** | **Working** | **Working** | Linux 7.1.0 with `PREEMPT_RT` patchset, 8-core SMP boot |
| **Serial Debug Console** | **Working** | **Working** | `ttyS0` @ 115200 baud (standard 8250/dw-uart) |
| **eMMC / MicroSD (MMC)** | **Working** | **Working** | Mainline `sunxi-mmc` driver |
| **Gigabit Ethernet (GMAC)** | **Working** | **Working** | Mainline `dwmac-sun55i` / `dwmac-sun8i` |
| **PMIC & Power Regulators** | **Working** | **Working** | AXP717 + AXP323 (A5E) / AXP8191 via RSB (A7A) |
| **Message Box (Mailbox IPC)**| **Working** | **Working** | 4-port hardware crossbar (`sun55i-msgbox.c`), v3 upstream ready |
| **RISC-V E907 / E902 Core** | **Working** | **Working** | Linux `remoteproc` standard (`sunxi_rproc.c`), v3 upstream ready |
| **HiFi4 Audio DSP Core** | **Working** | **Planned** | Remoteproc + SRAM mapping + dedicated ELF test suite |
| **Wi-Fi 6 (AIC8800)** | **Working (SDIO)** | **In Progress (USB)** | Clean FOSS mainline driver (`aic8800-upstream`), 25 MHz SDIO stabilized |
| **NPU AI Accelerator** | **Working** | **Working** | 2.0/3.0 TOPS via open-source Etnaviv DRM driver (`/dev/dri/card0`) + Teflon |
| **3D GPU Core** | **Working** | **Under Dev** | Panfrost Mali-G57 (A5E) / Imagination BXM-4-64 driver needed (A7A) |
| **USB 2.0 / 3.0 Host** | **Working** | **In Progress** | EHCI/OHCI (A5E) / DWC3 + PCK-600 power sequencing bring-up (A7A) |
| **MIPI CSI-2 Cameras** | ⚠️ **NOT Working** | ⚠️ **NOT Working** | **WIP**: V4L2 ISP & media controller bindings not yet mainlined |
| **MIPI DSI Display / HDMI** | ⚠️ **NOT Working** | ⚠️ **NOT Working** | **WIP**: DRM display engine (DE33) driver requires mainline porting |
| **Hardware Video Codec** | ⚠️ **NOT Working** | ⚠️ **NOT Working** | **WIP**: Stateless Cedrus VPU support pending sun55i/sun60i tables |

---

## 🛠️ Supported Boards

| Hardware Feature | Radxa Cubie A5E | Yuzuki Avaota A1 | Radxa Cubie A7A | Radxa Cubie A7Z |
| :--- | :--- | :--- | :--- | :--- |
| **SoC** | Allwinner A527 / T527 | Allwinner A527 / T527 | Allwinner A733 | Allwinner A733 |
| **Form Factor** | Standard SBC (85×56 mm) | Standard SBC (85×56 mm) | Standard SBC (85×56 mm) | Ultra-Compact Zero (65×30 mm) |
| **CPU Architecture** | 8× Arm Cortex-A55 @ 1.8 GHz | 8× Arm Cortex-A55 @ 1.8 GHz | 2× Cortex-A76 + 6× Cortex-A55 | 2× Cortex-A76 + 6× Cortex-A55 |
| **Co-Processors** | XuanTie E907 RISC-V + HiFi4 DSP | XuanTie E907 RISC-V + HiFi4 DSP | XuanTie E902 RISC-V | XuanTie E902 RISC-V |
| **System RAM** | 2 GiB / 4 GiB LPDDR4X | 2 GiB / 4 GiB LPDDR4X | 4 GiB / 6 GiB LPDDR5 | 2 GiB / 4 GiB LPDDR5 |
| **Wi-Fi / BT** | AIC8800D80 (SDIO) | AIC8800D80 (SDIO) | AIC8800D80 (USB) | AIC8800D80 (SDIO) |
| **Device Tree File** | `allwinner/sun55i-a527-cubie-a5e.dtb` | `allwinner/sun55i-t527-avaota-a1.dtb` | `allwinner/sun60i-a733-cubie-a7a.dtb` | `allwinner/sun60i-a733-cubie-a7z.dtb` |

---

## 🚀 Quick Start: Building the OS

### 1. One-Time Machine Setup
Clone the repository and run the setup script to initialize the workspace and toolchains:
```bash
git clone git@github.com:tcmichals/cubie-a5e.git
cd cubie-a5e
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
├── riscv-firmware/              # XuanTie E907 bare-metal firmware & test applications
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
2. **[Radxa Cubie A7A Platform Specification & Bring-Up Guide](docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)**:
   Hardware specs, LPDDR5 training, GICv3 interrupt controller, and register maps.
3. **[Radxa Cubie A7A Chronological Hardware Debug Log](docs/platforms/CUBIE_A7A_DEBUG_LOG.md)**:
   Detailed lab debug notes tracing power rail sequencing, PMIC registers, and peripheral bring-up.
4. **[AIC8800 Wi-Fi 6 SDIO/USB Architecture](docs/buildroot/AIC8800_Porting_Action_Plan.md)**:
   Detailed transport separation, IOPAD delay tuning, and firmware upload stability notes.

---

## 🤝 Acknowledgments & Credits

* **[The Linux-Sunxi Community](https://linux-sunxi.org/)**: Invaluable documentation, mainline porting efforts, and hardware reverse engineering.
* **[YuzukiHD](https://github.com/YuzukiHD)**: For [SyterKit](https://github.com/YuzukiHD/SyterKit) and [FreeRTOS-HIFI4-DSP](https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP) reference implementations.
* **[Radxa](https://radxa.com/)**: For engineering the Cubie A5E, Cubie A7A, and Cubie A7Z single-board computers.
* **Linux Kernel RemoteProc & Mailbox Subsystem Maintainers**: For thorough architectural reviews and feedback.
