# 📖 Avionics Flight Stack & Board Documentation Index

This repository contains the complete embedded Linux OS, real-time co-processor firmware, and flight control software stack for the **Radxa Cubie family** (Cubie A5E & Cubie A7A).

---

## 🧭 Master Documentation Roadmap

```
docs/
├── README.md                                  <-- You are here (Master Index & Sitemap)
│
├── common/                                    <-- Shared Subsystem Architectures
│   ├── WIFI_AIC8800_GUIDE.md                  <-- AIC8800 Wi-Fi 6 / BT (Dual SDIO / USB Architecture)
│   ├── CAMERA_V4L2_GUIDE.md                   <-- Camera Subsystem, Media Controller & V4L2 Pipelines
│   ├── RISCV_REMOTEPROC_GUIDE.md              <-- XuanTie E907 Co-Processor & Linux remoteproc Framework
│   └── REALTIME_FLIGHT_ARCHITECTURE.md        <-- Real-Time Linux OS Isolation (isolcpus=7, UIO Mailbox, Ringbuffers)
│
├── platforms/                                 <-- Dedicated Hardware & Bootloader Specifications
│   ├── CUBIE_A5E_PLATFORM_GUIDE.md            <-- Radxa Cubie A5E (Allwinner A527/T527, Mainline U-Boot, SDIO)
│   └── CUBIE_A7A_PLATFORM_GUIDE.md            <-- Radxa Cubie A7A (Allwinner A733, LPDDR5 boot0, GICv3, USB)
│
├── buildroot/                                 <-- Buildroot Build System & Historical Case Studies
│   ├── BuildRootHowTo.md                      <-- Buildroot Package Management & Build Flow
│   ├── DeviceTreeHowTo.md                     <-- Device Tree & Overlay (DTBO) Guide
│   ├── DebugLog.md                            <-- Chronological Engineering Case Studies & Bug History (Cases 1–7)
│   └── A7A_BRINGUP_AND_KNOWN_ISSUES.md        <-- A7A Bring-Up Post-Mortem & Verified Fixes
│
├── flightcontroller/                          <-- High-Level Avionics, FPGA & Application Stack
│   ├── ArchitectureAndAbstractX.md            <-- Flight Stack Split-Responsibility Model (Linux + FPGA)
│   ├── DevelopmentAndDebugging.md             <-- Cross-Compilation & VS Code Remote GDB Workflows
│   └── LandingAssistML.md                     <-- Vision-Based Landing Guidance & TinyML Sensor Fusion
│
└── articles/                                  <-- Long-Form Engineering Whitepapers & Deep Dives
    ├── README.md                              <-- 4-Part XuanTie Heterogeneous RISC-V Series
    ├── Mainline_Flightstack_Bringup_Article.md<-- Master 6-Blueprint Avionics Architecture Case Study
    └── FOSS_NPU_Migration_Article.md          <-- Transition from Proprietary Blobs to Upstream Mesa Teflon
```

---

## ⚡ Hardware Comparison: Cubie A5E vs. Cubie A7A

| Feature / Subsystem | Radxa Cubie A5E | Radxa Cubie A7A | Documentation |
| :--- | :--- | :--- | :--- |
| **SoC Silicon** | Allwinner A527 / T527 (`sun55iw3`) | Allwinner A733 (`sun60iw2p1`) | [A5E Guide](platforms/CUBIE_A5E_PLATFORM_GUIDE.md) / [A7A Guide](platforms/CUBIE_A7A_PLATFORM_GUIDE.md) |
| **CPU Cluster** | 8× Cortex-A55 (Octa-core) | 2× Cortex-A76 (Big) + 6× Cortex-A55 (LITTLE) | [Real-Time OS Isolation](common/REALTIME_FLIGHT_ARCHITECTURE.md) |
| **RAM Subsystem** | 2 GiB / 4 GiB LPDDR4 / LPDDR4X | 6 GiB LPDDR5 (Dynamic multi-PState training) | [A7A Platform Guide](platforms/CUBIE_A7A_PLATFORM_GUIDE.md) |
| **Interrupt Controller** | ARM GICv3 (`0x03400000`/`0x03460000`) | ARM GICv3 (`0x03400000`/`0x03460000`) | [A7A Bring-Up Issues](buildroot/A7A_BRINGUP_AND_KNOWN_ISSUES.md) |
| **Co-Processor** | XuanTie E906/E907 (RV32IMAFCP) | XuanTie E907 (RV32IMAFCP) | [RemoteProc Guide](common/RISCV_REMOTEPROC_GUIDE.md) |
| **Boot Chain** | Mainline TF-A + Mainline U-Boot 2026.01 | Vendor `boot0` + BL31 + Vendor U-Boot 2018.07 | [A5E Guide](platforms/CUBIE_A5E_PLATFORM_GUIDE.md) / [A7A Guide](platforms/CUBIE_A7A_PLATFORM_GUIDE.md) |
| **Mainline Kernel** | **100% In Mainline Linux** (`sun55i-a523`) | **Out-of-Tree Patch** (`sun60i-a733`) | [A7A Platform Guide](platforms/CUBIE_A7A_PLATFORM_GUIDE.md) |
| **Wi-Fi / BT Bus** | **SDIO 3.0** (`aic8800_bsp.ko` + `_fdrv.ko`) | **USB 2.0 High-Speed** (`aic8800_fdrv.ko` standalone) | [Wi-Fi AIC8800 Guide](common/WIFI_AIC8800_GUIDE.md) |
| **Camera Interface**| 2-Lane MIPI CSI-2 via Media Controller | 2-Lane MIPI CSI-2 via Media Controller | [Camera V4L2 Guide](common/CAMERA_V4L2_GUIDE.md) |
| **Real-Time Core 7**| `isolcpus=7` + `mlockall` + UIO Mailbox | `isolcpus=7` + `mlockall` + UIO Mailbox | [Real-Time OS Isolation](common/REALTIME_FLIGHT_ARCHITECTURE.md) |

---

## 🛠️ Buildroot Quick Start

### Building Radxa Cubie A5E
```bash
make cubie_a5e_defconfig
make
# Output image: images/sdcard.img
```

### Building Radxa Cubie A7A
```bash
make cubie_a7a_defconfig
make
# Output image: images/sdcard.img
```
