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

## 3. Dual Software Stack Architecture

### Option A: Open-Source Mainline Stack (Mesa Teflon + Etnaviv)
1. **Kernel Driver**: Mainline Linux `etnaviv` DRM driver (`CONFIG_DRM_ETNAVIV=y`).
2. **Userspace Runtime**: Mesa **Teflon** (TensorFlow Lite delegate for Vivante NPU).
3. **Execution**:
   ```bash
   # Run TFLite model using Teflon delegate
   python3 -c "
   import tflite_runtime.interpreter as tflite
   delegate = tflite.load_delegate('libteflon.so')
   interpreter = tflite.Interpreter(model_path='landing_target_yolo.tflite', experimental_delegates=[delegate])
   interpreter.allocate_tensors()
   "
   ```

### Option B: Vendor VIPLite Stack (Acuity SDK)
1. **Kernel Module**: `vcmd.ko` / `sunxi-npu.ko`.
2. **Userspace Library**: `libviplite.so` / `libvnn_vip9000.so`.
3. **Compilation Workflow**:
   ```bash
   # Convert ONNX model to compiled Vivante network binary (.nb)
   pegasus import onnx --model target_detector.onnx --output-data target_detector.data --output-model target_detector.json
   pegasus compile --model target_detector.json --data target_detector.data --export-format nb
   ```

---

## 4. Vision-Based Flight Applications

1. **Precision Landing Target Detection**:
   - 320×320 INT8 MobileNet-YOLO running at **120 FPS** with **< 5% CPU utilization**.
2. **Real-time Optical Flow & Obstacle Avoidance**:
   - Depth estimation and visual odometry sensor fusion executed directly in CMA memory.
