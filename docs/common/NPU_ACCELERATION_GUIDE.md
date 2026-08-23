# 🧠 Common Subsystem Guide: 3 TOPs NPU & TinyML Acceleration

This document details the **Neural Processing Unit (NPU)** architecture, memory-mapped registers, driver stacks, and inference pipelines for the **Radxa Cubie A7A** (Allwinner A733) and **Radxa Cubie A5E** (Allwinner A527/T527).

---

## 1. Hardware Architecture & Specifications

| Parameter | Specification | Technical Details |
| :--- | :--- | :--- |
| **NPU Silicon IP** | VeriSilicon Vivante VIP9000 Core | High-efficiency Convolutional Neural Network (CNN) inference engine |
| **Peak Performance** | **3.0 TOPs (INT8)** / **1.5 TOPs (FP16)** | Hardware accelerated INT8, INT16, FP16, and BFLOAT16 tensor arithmetic |
| **Physical MMIO Base** | **`0x03600000`** (`0x1000` byte window) | Register map for command parser, DMA descriptor engine, and IRQ status |
| **GIC Interrupt Vector** | **`GIC_SPI 65` (`0x41`)** | Level-high interrupt line for DMA batch execution and error handling |
| **Clocks & Resets** | `clk_npu`, `clk_bus`, `clk_mbus_gate` | Driven by PLL_NPU with dynamic frequency scaling (`opp-492` MHz) |
| **Power Domain** | `pd_npu` (Power Domain 4) | Dynamically gated via PRCM power domain controller |
| **I/O Memory Management**| Direct Physical / IOMMU Master | Zero-copy tensor buffer sharing with Linux CMA (Contiguous Memory Allocator) |

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
