# 📷 Common Subsystem Guide: Camera & V4L2 Video Pipeline

This document details the **Camera Capture Subsystem**, Video4Linux2 (V4L2) topology, MIPI CSI receiver interfaces, and Media Controller pipeline configuration across the Radxa Cubie family running Linux 7.1.

---

## 1. Hardware Architecture & Pipeline Overview

The camera subsystem uses a standard hardware topology connecting CMOS image sensors to the Allwinner CSI (Camera Serial Interface) receiver and Image Signal Processor (ISP):

```
┌──────────────┐     MIPI CSI-2     ┌──────────────┐     Internal Bus     ┌──────────────┐     DMA Engine     ┌──────────────┐
│ CMOS Sensor  │ ─────────────────> │ CSI Receiver │ ───────────────────> │ Allwinner    │ ─────────────────> │ V4L2 Nodes   │
│ (e.g. GC2145)│   (2-Lane D-PHY)   │ (sunxi-csi)  │                      │ ISP / VIN    │                    │ (/dev/videoX)│
└──────────────┘                    └──────────────┘                      └──────────────┘                    └──────────────┘
```

---

## 2. Media Controller Pipeline Configuration

In modern Linux (Linux 7.1), camera pipelines do not expose single monolithic video nodes; they use the **Media Controller** framework to link sensor subdevices, CSI receivers, and capture endpoints:

### Enumerating Pipeline Entities
```bash
media-ctl -p -d /dev/media0
```

### Linking Sensor to CSI Capture
```bash
# Link GC2145 sensor pad to CSI video input pad
media-ctl -d /dev/media0 -l '"gc2145 1-003c":0 -> "sunxi-csi":0[1]'

# Configure pixel format, resolution, and framerate on the pipeline
media-ctl -d /dev/media0 -V '"gc2145 1-003c":0 [fmt:UYVY8_2X8/1280x720 field:none]'
media-ctl -d /dev/media0 -V '"sunxi-csi":0 [fmt:UYVY8_2X8/1280x720 field:none]'
```

---

## 3. Video Capture Verification & Stream Testing

### 1. Identify Video Capture Node
```bash
v4l2-ctl --list-devices
```

### 2. Query Formats Supported by Sensor
```bash
v4l2-ctl -d /dev/video0 --list-formats-ext
```

### 3. Capture Raw Test Frame
```bash
v4l2-ctl -d /dev/video0 \
    --set-fmt-video=width=1280,height=720,pixelformat=UYVY \
    --stream-mmap --stream-count=1 --stream-to=/tmp/test_frame.raw
```

### 4. Continuous Real-time Stream Verification (FPS Testing)
```bash
v4l2-ctl -d /dev/video0 \
    --set-fmt-video=width=1280,height=720,pixelformat=UYVY \
    --stream-mmap --stream-count=300
```
- Observe console output for steady 30 FPS / 60 FPS delivery without DMA buffer underruns.
