# 🎬 Common Subsystem Guide: Hardware Video Encoding & VPU Acceleration

This document details the **Video Engine (VE / VPU)** architecture, memory-mapped registers, V4L2 stateless Memory-to-Memory (M2M) drivers, and real-time H.264/H.265 video streaming pipelines for the **Radxa Cubie A7A** (Allwinner A733) and **Radxa Cubie A5E** (Allwinner A527/T527).

---

## 1. Hardware Architecture & Encoding Specifications

| Parameter | Specification | Technical Details |
| :--- | :--- | :--- |
| **VPU Silicon IP** | Allwinner VE (Video Engine 5.0 / Cedrus) | Multi-format hardware video encoder & decoder core |
| **Encoding Codecs** | **H.265 (HEVC) & H.264 (AVC) Main/High Profile** | Supports baseline, main, and high profiles up to **4K@30fps** or **1080p@120fps** |
| **Decoding Codecs** | **H.265, VP9, AVS2 (8K@24fps / 4K@60fps)** | Multi-standard hardware entropy decoder (CABAC/CAVLC) |
| **Physical MMIO Base** | **`0x01C0E000`** (`0x3000` bytes) & Syscon **`0x03000000`** | Base register window for macroblock engines, motion estimation & bitstream DMA |
| **GIC Interrupt Vector** | **`GIC_SPI 49` (`0x31`)** | Level-high interrupt fired upon frame compression completion |
| **Clocks & Resets** | `pll_ve`, `clk_bus_ve`, `reset_ve` | Clocked via PLL_VE (typically 400 MHz to 600 MHz) |
| **Memory Architecture** | Direct Physical DMA / Contiguous Memory Allocator (CMA) | Zero-copy frame handoff from Camera (V4L2) -> VPU -> Network Socket |

---

## 2. Real-Time Drone Video Streaming Topology

```
┌─────────────────┐       DMA-BUF       ┌─────────────────┐       DMA-BUF       ┌─────────────────┐       RTP / UDP       ┌─────────────────┐
│ MIPI CSI Sensor │ ──────────────────> │ Allwinner ISP   │ ──────────────────> │ Hardware VPU    │ ────────────────────> │ Ground Station  │
│ (IMX219 / HQ)   │   (Raw Bayer)       │ (NV12 / YUV420) │    (Zero-Copy)      │ (H.265 Encoder) │    (Low Latency)      │ (QGroundControl)│
└─────────────────┘                     └─────────────────┘                     └─────────────────┘                       └─────────────────┘
                                                                                         │
                                                                           MMIO: 0x01C0E000 (IRQ 49)
                                                                           Codec: H.265 1080p @ 60 FPS
                                                                           Bitrate: 4000 Kbps CBR
```

---

## 3. GStreamer Low-Latency Hardware Pipelines

### Pipeline A: Real-Time H.265 RTSP/UDP Stream to Ground Control Station (GCS)
```bash
# Capture 1080p from MIPI CSI-2, encode with hardware VPU, and stream via RTP/UDP
gst-launch-1.0 -v \
    v4l2src device=/dev/video0 ! \
    video/x-raw,format=NV12,width=1920,height=1080,framerate=60/1 ! \
    v4l2h265enc bitrate=4000000 gop-size=30 ! \
    rtph265pay config-interval=1 pt=96 ! \
    udpsink host=192.168.1.100 port=5600 sync=false
```

### Pipeline B: Local NV12 to H.264 Hardware File Recording
```bash
# Record 1080p video directly to SD card / eMMC with minimal CPU load (< 3%)
gst-launch-1.0 -v \
    v4l2src device=/dev/video0 num-buffers=1800 ! \
    video/x-raw,format=NV12,width=1920,height=1080,framerate=60/1 ! \
    v4l2h264enc bitrate=8000000 ! \
    h264parse ! \
    mp4mux ! \
    filesink location=/var/flight_video.mp4
```

---

## 4. Kernel Configuration Checklist (`CONFIG_` Options)

To enable hardware video encoding in the mainline kernel build:

```ini
# Mainline V4L2 Stateless Cedrus VPU Driver
CONFIG_VIDEO_SUNXI_CEDRUS=y
CONFIG_V4L_MEM2MEM_DRIVERS=y
CONFIG_VIDEO_MEM2MEM_DEINTERLACE=y

# DMA Contiguous Memory Allocator (Minimum 128 MB for 4K video)
CONFIG_CMA=y
CONFIG_CMA_SIZE_MBYTES=128
```
