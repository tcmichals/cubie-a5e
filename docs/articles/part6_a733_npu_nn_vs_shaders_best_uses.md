# Inside an Edge AI Chip: NN Cores, Shaders, and Why You Need Both (Best Uses on the Allwinner A733)

**By Tim Michals**  
*Embedded Linux, Edge AI & Robotics Engineering Blog*

---

When a single-board computer spec sheet advertises a **"3.0 TOPS NPU"**, most developers picture a single black box: feed it a camera frame, get bounding boxes out.

When we actually brought up the **Allwinner A733** on the **Radxa Cubie A7A** under mainline Linux `7.1.0 PREEMPT_RT`—probing silicon register by register instead of trusting vendor marketing PDFs—we discovered something far more interesting (and much more useful):

The NPU sitting at `0x03600000` (`VeriSilicon VIP9000Nano-DI`, rev `0x9202`, `HW_V83`) is **not one engine**. It is a compact **two-engine heterogeneous compute cluster** sharing a Front-End DMA command parser, a 512 KB on-chip SRAM, and an MMUv2 virtual memory unit.

Understanding the difference between **NN cores**, **shaders (PPU)**, and the **missing TP engine** is the key to unlocking real performance on modern edge-AI SoCs—and explains why our mainline `etnaviv` + Mesa Teflon stack exposes **both** paths over `/dev/dri/renderD128`.

---

## 1. What Is Actually Inside the A733 NPU?

### Engine 1: The 8× NN Cores (Fixed-Function Convolution Arrays)
Think of the **NN (Neural Network) cores** as an assembly line built for one job: **8-bit integer multiply-accumulate (`INT8` / `UINT8` MAC) math**.

* The A733 packs **8 NN cores × 64 MADs/core** backed by **512 KB of ultra-wide VIP SRAM** (**128 bytes / 1,024 bits per cycle**).
* Hardware weight decompression (Huffman + ZRL) streams compressed filters from LPDDR5 directly into the MAC arrays, while fused inline stages handle 32-bit bias add, `ReLU`/`ReLU6`, pooling, and requantization.
* **What NN cores crush:** `CONV_2D`, `DEPTHWISE_CONV_2D`, and `FULLY_CONNECTED`—which make up **90%+ of the compute** in YOLOv8/YOLO11, MobileNet, ResNet, and PoseNet.
* **What NN cores CANNOT do:** Branches (`if/else`), arbitrary float32 math, FFTs, image warping, optical flow, or bounding-box NMS.

### Engine 2: The 1× PPU / Shader Core (Programmable Vector Processor)
Right next to the 8 NN cores sits a **Programmable Processing Unit (PPU)**—a 1-core, **256-thread SIMD vector processor** (64 temp registers, 512-instruction cache).

If you have never written GPU or OpenCL code, a **"shader"** simply means a **small parallel program that executes the same math across hundreds of pixels or array elements simultaneously**. It is essentially a mini compute GPU bolted directly onto the NPU die:
* It reads/writes normal Linux buffers mapped through MMUv2.
* It natively executes IEEE-754 **float32**, **fp16**, and **integer** vector instructions.
* On our live Cubie A7A (`0x9202`), a 28-DWORD cold-boot-deterministic Vivante PPU compute shader dispatched via mainline `etnaviv` DRM (`npu_ppu_compute_test.c`) completes in **`0.052–0.950 ms`** (10/10 runs PASS) with bit-exact IEEE-754 float32 outputs, and zero-copy V4L2/CMA `dma-buf` PRIME import (`npu_v4l2_dmabuf_test.c`) runs in **`0.206 ms` / `0.220 ms`** with zero CPU involvement.

### What’s *Missing* on the A733: `TP_ENGINE = 0`
Older Vivante NPUs—like the Allwinner T527 (`0x9003`, Cubie A5E) or NXP i.MX8M Plus (`0x8002`)—included a third hardware block called the **TP (Tensor Processor)**. The TP engine automatically handled tensor layout transposes (`NHWC ↔ NCHW`), border padding, and piecewise-linear activation LUTs (`Sigmoid`, `Abs`).

On the A733 (`0x9202`), VeriSilicon **removed the TP block** (`TP_ENGINE = 0`, `TPEngine_CoreCount = 0`) to save silicon area, expecting software to run non-convolution operations on the **PPU shader core** or **Arm CPU**.

---

## 2. Why Real Applications Need *Both* Paths

A real robotics, drone, or security camera pipeline is **never** just a single convolution layer. Follow one 1080p30 video frame from camera sensor to actuator on a Cubie A7A:

1. **Camera Capture & ISP**: Raw Bayer pixels become a clean 1080p `NV12` frame.
2. **Video Recording / RTSP Stream**: That exact same frame is compressed into H.264 or H.265.
3. **Pre-Processing**: Resize, color conversion, normalization, or `NHWC → NCHW` layout conversion.
4. **AI Backbone Inference**: 40–100 quantized `CONV_2D` layers extract features.
5. **Custom Vision / Post-Processing**: Bounding-box decoding, NMS, `TopK`, `Softmax`, or custom optical flow / tracking math.
6. **Hard Real-Time Control**: Steering / PID / servo updates with microsecond determinism.

If you rely on proprietary vendor **VIPLite `.nb` blobs**, you only get step 4—locked to an old BSP kernel, an x86 Acuity Docker container, and a single SoC revision—while the PPU shader core stays hidden behind closed drivers.

With **mainline Linux 7.1 `etnaviv` + Mesa 26.1.8 Teflon** on `/dev/dri/renderD128`, you get **both** Path A (TFLite NN inference) and Path B (programmable PPU compute shaders) on the same standard Linux DRM device, with zero-copy **`dma-buf`** buffer sharing across the camera ISP and hardware video encoder.

---

## 3. Best Uses: Matching Every Workload to the Right A733 Silicon Block

Here is our practical engineering cheat sheet for getting the most out of the Allwinner A733's heterogeneous silicon on mainline Linux:

| Application / Stage | Best Engine on A733 | Verified Performance / Characteristic | Why Not Run It Elsewhere? |
| :--- | :--- | :--- | :--- |
| **Object Detection & Classification (`YOLOv8`, `MobileNet`, `PoseNet`)** | **8× NN Cores** (Mesa Teflon `.tflite` delegate — **WIP**) | `0.105 ms` Conv2D dispatch (`quantized_add` 192/192 exact; NN DRAM-read bug open on `0x9202`); `512 KB` VIP SRAM weight reuse | Running CNN backbones on Arm CPU burns 3–5 W and starves control/networking |
| **Custom Compute, Image Filtering, Optical Flow, Stereo, DSP, Custom AI Layers** | **1× PPU Shader Core** (`etnaviv` DRM compute, `npu_ppu_compute_test.c`, `npu_v4l2_dmabuf_test.c`) | **`0.052–0.950 ms`** per dispatch (`0.206 ms` V4L2 `dma-buf` PRIME), exact IEEE-754 `float32` | NN cores can't run custom math; vendor VIPLite `.nb` doesn't expose shaders |
| **1080p30 Camera Demosaic, 3A, NV12, H.264 & H.265 Video Encoding** | **Hardware ISP 602 (`sun60i-isp`) + Cedrus VE (`ve@1c10000`)** | **30.0 fps** H.264 at **0.89% CPU**, H.265 at **1.55% CPU** (zero-copy `dma-buf`) | Eliminates 94 MB/s of CPU frame copies and software encoding load |
| **NMS, `TopK`, `argmax`, `Softmax`, Graph Logic, MAVLink / RTSP** | **2× Cortex-A76 + 6× Cortex-A55** (automatic TFLite delegate partitioning) | Full `float32` precision, NEON-accelerated | Quantizing NMS/TopK to INT8 on NPU destroys bounding-box coordinate accuracy |
| **Hard Real-Time PID, Motor Control, Sensor Polling** | **On-Die 200 MHz XuanTie E902 RISC-V** (`remoteproc` + MSGBOX) | **14.5 µs** shared-SRAM doorbell, **0 drops** across 20k RPMsg stress test | Zero Linux scheduling jitter; replaces external STM32/RP2040 MCUs |

---

## 4. Why This Matters Beyond One Demo

When every major block on the A733—**MIPI CSI-2**, **Hardware ISP 602**, **Cedrus H.264/H.265**, **8× NN + PPU NPU (`etnaviv` + Teflon)**, and **200 MHz E902 RISC-V**—runs on **mainline Linux 7.1 `PREEMPT_RT`** using standard kernel interfaces (`V4L2`, `DRM`, `dma-buf`, `remoteproc`):

1. **Zero proprietary vendor lock-in**: Upgrade kernels freely without broken 44K-line `vipcore.ko` modules or mismatched `libVIPhal.so` blobs.
2. **Portable AI models**: Drop a standard quantized `.tflite` file onto the board—no x86 Acuity Docker workflow and no T527-vs-A733 `.nb` binary incompatibility.
3. **Full silicon utilization**: Use the 8× NN cores for CNNs, the PPU shader core for custom parallel compute, ISP+VE for zero-copy 1080p video, and the E902 RISC-V core for hard real-time actuation—all at the same time on a single board.

*Next in the series: **[Part 7: Freeing the Allwinner A733 NPU on Mainline Linux 7.1 — The 8 Real-World Problems We Solved on Live Silicon](part7_a733_npu_silicon_debugging_problems_solved.md)**, or dive into the full hardware diagrams and register walkthrough in our **[Hackster.io NPU Deep Dive](hackster_a733_vip9000_npu_etnaviv_teflon.md)**.*
