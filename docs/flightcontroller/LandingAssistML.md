# Vision-Based Landing Assist (TinyML)

This document describes the high-level guidance application running on the Cubie A5E flight controller using NPU-accelerated TinyML models for autonomous landing assistance.

---

## 1. Landing-Assist Guidance Concept

During the final phase of flight, the flight controller fuses high-frequency sensor readings with vision-based target classification:

```text
  Camera Frame  ──> [ TIM-VX Accelerated NPU Model ] ──> Landing Confidence (0.0 - 1.0)
                                                                 │
  ToF / LiDAR   ───────────────────────────────────────────────> │ ──> [ Speed Controller ] ──> SPI Packet (FPGA)
```

1. **Target Detection:** The camera captures down-facing frames. A quantized TensorFlow Lite model runs on the NPU (via the TIM-VX delegate) to classify the landing pad or locate the landing marker.
2. **Altitude Control:** An altitude sensor (such as a Time-of-Flight rangefinder, LiDAR, or sonar) provides millimetric distance-to-ground measurements.
3. **Decent Rate Decision:** The guidance script adjusts the target vertical speed depending on how confident the NPU model is in the landing zone quality.

---

## 2. Minimal Landing Assist Implementation (Python)

Below is the reference loop for the landing guidance module, running on the A5E Linux userspace:

```python
import time
import numpy as np

# Placeholders for sensor/camera APIs
from flight_sensors import Camera, DistanceSensor
from flight_control import GuidanceController

def main():
    camera = Camera.initialize()
    distance_sensor = DistanceSensor.initialize()
    controller = GuidanceController.initialize()

    # Load Model with TIM-VX NPU delegate
    # (By default libraries are located in /usr/lib/libtim-vx.so)
    model = LoadTFLiteModel("landing_quantized.tflite", use_npu=True)

    print("Landing assist active. Monitoring altitude...")

    while True:
        frame = camera.read_frame()
        altitude_m = distance_sensor.read_meters()

        # Run hardware-accelerated NPU inference
        landing_confidence = model.predict(frame)  # Returns float between 0.0 and 1.0

        if altitude_m < 1.0 and landing_confidence > 0.85:
            # Safe zone identified, slow down descent rate
            print(f"Target found (conf: {landing_confidence:.2f}). Adjusting descent rate.")
            controller.set_vertical_speed(-0.15)  # 15 cm/s
        elif altitude_m < 0.3:
            # Flare/touchdown phase close to the ground
            controller.set_vertical_speed(-0.05)   # 5 cm/s
        else:
            # Nominal descend rate
            controller.set_vertical_speed(-0.30)   # 30 cm/s

        time.sleep(0.1)  # Run loop at ~10 Hz

if __name__ == "__main__":
    main()
```

---

## 3. Integration & Safety Validation Workflow

To ensure system stability, any updates to the landing-assist model or guidance scripts must follow this validation sequence:

### Step 1: Validate Sensor I/O & Timestamps
Confirm that camera frames and distance sensor values are read cleanly without blocking the main event loop, and verify that their timestamps are synchronized.

### Step 2: Establish CPU Baseline
Run the inference script on the CPU first. Verify that the logic behaves correctly and log baseline execution latency (usually high on a CPU).

### Step 3: Enable TIM-VX Delegate Acceleration
Enable the TIM-VX NPU delegate. Verify that:
* The NPU model loads and executes without error.
* Model outputs (class confidences) match the CPU baseline outputs within acceptable float tolerance.
* Inference latency drops significantly compared to the CPU baseline.

### Step 4: Safety-Gated Autonomy
Never allow the autonomous ML guidance to command raw motor signals directly. 
* All ML speed commands must go through the primary Flight Controller supervisor block.
* The system must check limits (e.g. max roll/pitch tilt) and immediately hand control back to the pilot or fallback safety routines if a manual override switch is toggled on the RC transmitter.

---

## 4. Camera Verification & Testing Tools

The Buildroot image includes the **Video4Linux2 (V4L2)** utilities (`v4l2-ctl` from the `v4l-utils` package) to verify MIPI-CSI and USB-UVC cameras.

### A. Command Line Verification (`v4l2-ctl`)

Log in to the board via SSH (Dropbear) or the debug terminal, and run the following commands:

1. **List all connected cameras:**
   ```bash
   v4l2-ctl --list-devices
   ```
   *Expected output:* A list showing video nodes (e.g., `/dev/video0` or `/dev/video1`) and the driver name (e.g., `sunxi-video` or `uvcvideo`).

2. **List supported resolutions and pixel formats:**
   ```bash
   v4l2-ctl --list-formats-ext -d /dev/video0
   ```
   *Note:* Replace `/dev/video0` with the correct node name found in step 1. Look for formats like `MJPG`, `YUYV`, or raw format definitions.

3. **Capture a single test frame (JPEG format):**
   If the camera supports MJPEG (`MJPG`), capture a single test image:
   ```bash
   v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=MJPG --stream-mmap --stream-to=/tmp/test_frame.jpg --stream-count=1
   ```

4. **Capture a single test frame (Raw YUYV format):**
   If the camera does not support MJPEG hardware encoding:
   ```bash
   v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=YUYV --stream-mmap --stream-to=/tmp/test_frame.yuyv --stream-count=1
   ```

You can use `scp` to copy the image file to your PC and verify the lens focus and alignment:
```bash
scp root@cubie-a5e-flight:/tmp/test_frame.jpg .
```

### B. Python Camera Acquisition Script

Below is a lightweight Python script that opens the V4L2 device node directly using Python standard libraries (or `os`/`fcntl` descriptors) to read raw YUYV frames without requiring heavy OpenCV installations:

```python
#!/usr/bin/env python3
import os
import fcntl
import mmap
import ctypes

# Simple V4L2 raw capture structure definitions
class v4l2_buffer(ctypes.Structure):
    _fields_ = [
        ("index", ctypes.c_uint32),
        ("type", ctypes.c_uint32),
        ("bytesused", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("field", ctypes.c_uint32),
        ("timestamp", ctypes.c_int64 * 2),
        ("timecode", ctypes.c_uint32 * 4),
        ("sequence", ctypes.c_uint32),
        ("memory", ctypes.c_uint32),
        ("offset", ctypes.c_uint32),
        ("length", ctypes.c_uint32),
        ("reserved2", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
    ]

def capture_raw_frame(device_path="/dev/video0", width=640, height=480):
    """
    Directly opens the V4L2 camera file descriptor, maps a frame buffer,
    reads a single frame, and writes it to a file.
    """
    try:
        # Open device in read/write mode
        fd = os.open(device_path, os.O_RDWR)
        print(f"Opened V4L2 device: {device_path}")
        
        # In a real pipeline, you would use ioctl() to configure formats (VIDIOC_S_FMT)
        # and request buffers (VIDIOC_REQBUFS).
        # v4l2-ctl can be run prior to set the camera into the correct format:
        # v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=YUYV
        
        # Read raw stream data directly from fd if the camera driver supports read()
        try:
            raw_data = os.read(fd, width * height * 2) # YUYV is 2 bytes per pixel
            with open("/tmp/python_frame.yuyv", "wb") as f:
                f.write(raw_data)
            print("Frame successfully captured to /tmp/python_frame.yuyv")
        except OSError:
            print("Direct read() not supported by driver. Run v4l2-ctl stream commands above instead.")
        
        os.close(fd)
    except Exception as e:
        print(f"Error capturing frame: {e}")

if __name__ == "__main__":
    capture_raw_frame()
```

