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
