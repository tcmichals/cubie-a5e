# 🧠 Common Subsystem Guide: 3 TOPs NPU & TinyML Acceleration

This document details the **Neural Processing Unit (NPU)** architecture, memory-mapped registers, driver stacks, and inference pipelines for the **Radxa Cubie A7A** (Allwinner A733) and **Radxa Cubie A5E** (Allwinner A527/T527).

> **Deep-Dive Hackster.io Article & SVG Diagrams**: For a complete visual walkthrough of the live-verified `VIP9000Nano-DI` (`0x9202`) silicon dataflow, MMUv2 & DMA architecture, the 4-layer `etnaviv` + Mesa Teflon software stack, live silicon verification findings, and zero-copy `dma-buf` video processing with MIPI CSI-2, Hardware ISP 602, Cedrus H.264/H.265 Video Encoder, and the E902 RISC-V coprocessor, see **[Freeing the 3.0 TOPS NPU on the Allwinner A733: How an On-Die Neural Accelerator Works, Mainline Linux `etnaviv` + Mesa Teflon Bring-Up, and Zero-Copy Video Processing](../articles/hackster_a733_vip9000_npu_etnaviv_teflon.md)**.

---

## 1. Hardware Architecture & Specifications

| Parameter | Radxa Cubie A7A (Allwinner A733 — Live Silicon Verified) | Radxa Cubie A5E (Allwinner A527 / T527) |
| :--- | :--- | :--- |
| **NPU Silicon IP** | VeriSilicon Vivante **VIP9000Nano-DI (`HW_V83`)** | VeriSilicon Vivante **VIP9000Nano-SI+** |
| **Chip ID / Revision** | Model `0x9000`, Rev **`0x9202`** (date `2023-05-18`) | Model `0x9000`, Rev **`0x9003`** |
| **Product / Customer / ECO ID** | `0x05090009` / **`0x1000003b`** / `0x08000000` | `0x05090009` / **`0x10000016`** / `0x06000000` |
| **Peak Performance** | **3.0 TOPS class (INT8)** (`492–1120 MHz` OPP) | **2.0 TOPS (INT8)** |
| **NN / TP / Shader Cores** | **8 NN cores** (64 MAD/core), **0 TP cores (`TP_ENGINE = 0`)**, **1 shader core** | **6 NN cores** (64 MAD/core), **2 TP cores**, 1 shader core |
| **On-Chip VIP SRAM** | **512 KB** (`0x80000`, 128-byte physical width) | **512 KB** (`0x80000`, 64-byte physical width) |
| **MMU Architecture** | MMUv2 Secure AHB + **Page Descriptor (`0x003b4`)** | MMUv2 Secure AHB (`0x00388..0x003a8`) |
| **Physical MMIO Base** | **`0x03600000`** (`0x1000` byte window) | **`0x07122000`** (`0x2000` byte window) |
| **GIC Interrupt Vector** | **`GIC_SPI 65`** (`IRQ_TYPE_LEVEL_HIGH`) | **`GIC_SPI 211`** (`IRQ_TYPE_LEVEL_HIGH`) |
| **Clocks (`bus`, `core`, `reg`)** | `CLK_MBUS_NPU`, `CLK_NPU` (`pll-npu`, 840–1008 MHz), `CLK_BUS_NPU` (parented to `CLK_AHB_NPU`) | `CLK_BUS_NPU_AXI`, `CLK_NPU`, `CLK_BUS_NPU_AHB` |
| **Resets** | 3 resets in `0x0b04`: `RST_BUS_NPU_CORE` (`BIT(16)`), `RST_BUS_NPU_AXI` (`BIT(17)`), `RST_BUS_NPU_AHB` (`BIT(18)`) | 1 reset: `RST_BUS_NPU` |
| **Power Domain & Supply** | `PD_NPU` (PCK-600 domain 4), AXP8191 `dcdc2` (`vdd-gpu-npu`) | `PD_NPU` (PCK-600 domain 1), `dcdc3` |

---

## 2. NPU Execution Pipeline Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ High-Level AI Model (TensorFlow Lite / ONNX / PyTorch)                      │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Model Quantization & Compilation (Acuity / VIPLite Toolchain)                │
│    • Converts model graph to VIP9000 binary format (.nb / .vpmd)            │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Linux Runtime (Mesa Teflon / Open-Source Etnaviv OR Vendor VIPLite Driver)  │
│    • Allocates contiguous DMA input/output tensor buffers (CMA / dma-buf)   │
│    • Programs hardware command queues at MMIO 0x03600000                    │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Allwinner A733 VIP9000 Silicon Core (3 TOPs @ 0x03600000, IRQ 65)           │
│    • Zero-CPU hardware matrix multiplication & non-linear activation        │
│    • Fires GIC_SPI 65 on completion -> passes output tensors to flight stack│
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Dual Mainline Compute Paths (`etnaviv` on `/dev/dri/renderD128`)

The A733 `VIP9000Nano-DI` (`0x9202`) contains both an **8-core `HW_V83` Neural Network (NN) engine** and a programmable **Vivante PPU (OpenCL/Shader) core**. Mainline Linux 7.1 `etnaviv` exposes both over `/dev/dri/renderD128` with zero proprietary blobs:

### Path A: Neural Network Inference (Mesa 26.1.8 Teflon + `etnaviv`)
1. **Kernel Driver**: Mainline Linux `etnaviv` DRM (`CONFIG_DRM_ETNAVIV=y`) with A733 CCU clock parenting, triple resets (`core`/`axi`/`ahb`), `0x9202` HWDB (`ETNA_SEC_KERNEL`), and MMUv2 **PDMODE** (`0x003B4 = ((mtlb_dma >> 12) << 4) | 0x1`, `0x00388 = 0x21`).
2. **Userspace Runtime**: Mesa **Teflon** (`libteflon.so`, TFLite external delegate) with two Cubie A7A patches in `project-cubie-a5e/board/radxa/cubie_a7a/patches/packages/mesa3d/`:
   - `0001-etnaviv-hwdb-add-allwinner-npu-feature-database.patch`: Allwinner `0x9000` feature database (`0x8303`, `0x9003`, `0x9113`, `0x9202`).
   - `0002-etnaviv-teflon-add-hw-v83-and-no-tp-support.patch`: `TP_ENGINE = 0` CPU fallback (`TRANSPOSE`, `DETRANSPOSE`, `RESHUFFLE`), `NN_CMD_SUPPORT_SLICE = 1` slice strides (`x * y`), `FP_INIMAGE_POST_SCALE = 1` raw IEEE-754 biased exponent (`scale_bits >> 23`, incl. bit 7 in `w15[30]`), and clean CPU fallback for `HW_V83` ext-cmd `ADD`/`SUBTRACT`.
3. **Verification**:
   ```sh
   /usr/bin/npu-smoke-test
   ```

### Path B: Direct PPU / OpenCL Compute (`etnaviv` DRM Shader Core — Verified Live)
For custom non-CNN workloads (image pre/post-processing, DSP math, custom kernels) that vendor VIPLite `.nb` binaries cannot express, the NPU's Vivante PPU shader core runs directly via `DRM_IOCTL_ETNAVIV_GEM_SUBMIT`:
- **Source**: `project-cubie-a5e/board/radxa/cubie_a7a/src/npu_ppu_compute_test.c`
- **Live Silicon Result (Cubie A7A, `840 MHz` `pll-npu`, `800 mV` `dcdc2`, `35.9 °C`)**:
  - 16-DWORD Vivante PPU compute shader (`img_load.u8` × 2 → `dp2x8` → `img_store.u8`) executes in **`0.092–0.220 ms`** per dispatch.
  - **5/5 bit-exact IEEE-754 float32 outputs** (`fill = 1, 2, 3, 5, 7` → `0x3d808000`, `0x3e008000`, `0x3e40c000`, `0x3ea0a000`, `0x3ee0e000`), **0 MMU faults**, **0 hangs**, clean `GIC_SPI 65` completion.

---

## 4. Community Questions, Common A733 NPU Issues & Fixes

Real-world issues reported across Radxa Forums, Armbian, and GitHub (Cubie A7A/A7Z/A7S, Orange Pi 4 Pro, Frigate NVR, YOLOv8/YOLO11) and how our mainline stack addresses them:

| Issue | Root Cause | Mainline Fix / Workaround |
| :--- | :--- | :--- |
| **T527 `*_t527.nb` fails `vpm_run` on A733** | T527 (`0x9003`: 6 NN / 2 TP) vs A733 (`0x9202`: 8 NN / **0 TP**, `HW_V83`, `SLICE=1`, `FP_POST_SCALE=1`) are binary-incompatible. | Mesa Teflon compiles standard `.tflite` files on-device at runtime—no Acuity Docker or per-SoC `.nb` exports. |
| **VIPLite `2.0.3.4-AW` vs `2.0.3.2` mismatch / missing libs on R6 & Trixie** | Proprietary `/dev/vipcore` and `libNBGlinker.so` blobs break across kernel/rootfs versions. | Uses standard Linux DRM UAPI (`/dev/dri/renderD128`, `etnaviv 1.4.0`) + Buildroot-compiled `libteflon.so`. |
| **Scrambled YOLO / Frigate bounding boxes** | `vip_query_output` reports NHWC while VIP9000 NN writes planar NCHW, and `0x9202` has `TP_ENGINE = 0`. | Teflon `0002` patch performs exact `NHWC ↔ NCHW` transpose/detranspose with GPU fence sync. |
| **NMS / TopK / Softmax accuracy loss on NPU** | Quantizing post-processing inside monolithic `.nb` breaks coordinate/score precision. | TFLite external delegate automatically keeps `NMS`, `TopK`, `Softmax`, and unsupported ops on Cortex-A76/A55 CPU. |
| **`aw_nna_vip` fails to compile on mainline 6.6 / 7.1** | Out-of-tree vendor driver depends on BSP 5.15 headers (`sunxi-autogen.h`). | Replaced with mainline `drivers/gpu/drm/etnaviv` + DT/CCU/reset/PDMODE patches. |
| **`0x03600000` register reads hang board** | Missing `PD_NPU`, `CLK_AHB_NPU` parent, or any of the 3 resets in `0x0b04` (bits 16/17/18); or device runtime-suspended. | Parent `bus-npu` to `ahb-npu`, deassert all 3 resets via `reset_control_array`, only read MMIO while `runtime_status=active`. |
| **Mesa 26.1.8 outputs all-`128` (`0x80`) on `0x9202`** | `0x9202` is the first Mesa HWDB chip with `TP_ENGINE=0`, `NN_CMD_SUPPORT_SLICE=1`, and `FP_INIMAGE_POST_SCALE=1`. | Fixed in `0002-etnaviv-teflon-add-hw-v83-and-no-tp-support.patch` + kernel MMUv2 PDMODE. |

*(Full deep-dive analysis, SVG architecture diagrams, and live register proofs: [hackster_a733_vip9000_npu_etnaviv_teflon.md](../articles/hackster_a733_vip9000_npu_etnaviv_teflon.md).)*

---

## 5. Vision-Based Flight Applications

1. **Precision Landing Target Detection**:
   - Quantized INT8/UINT8 MobileNet-YOLO running via Teflon NPU delegate alongside zero-copy CSI → ISP 602 → Cedrus H.264/H.265.
2. **Custom Vision / DSP Kernels**:
   - Low-latency Vivante PPU compute shaders via `etnaviv` (`0.09–0.22 ms`) feeding 14.5 µs MSGBOX setpoints to the on-die E902 RISC-V flight loop.
