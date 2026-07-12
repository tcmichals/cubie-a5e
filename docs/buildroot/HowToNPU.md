# NPU & TinyML Acceleration Guide

This guide explains how the Neural Processing Unit (NPU) and TinyML hardware-accelerated inference stack is configured and validated using the open-source **Etnaviv** and **Mesa Teflon** stack on the Radxa Cubie A5E flight controller.

---

## 1. NPU Software Stack

To run TensorFlow Lite models using NPU hardware acceleration instead of CPU-bound inference, the following open-source components are integrated:

```text
┌─────────────────────────────────────────────────────────┐
│        Python / C++ Application (Landing Assist)        │
├─────────────────────────────────────────────────────────┤
│            TensorFlow Lite Inference Engine             │
├─────────────────────────────────────────────────────────┤
│         Mesa Teflon NPU Delegate (libteflon.so)         │
├─────────────────────────────────────────────────────────┤
│          Mesa 3D Gallium GPU Driver (etnaviv)           │
├─────────────────────────────────────────────────────────┤
│         etnaviv Kernel DRM Driver (/dev/dri/*)          │
└─────────────────────────────────────────────────────────┘
```

In this Buildroot tree:
* `CONFIG_DRM_ETNAVIV=y` is enabled in `linux.config` to build the open-source kernel driver.
* `BR2_PACKAGE_MESA3D=y` and `BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_ETNAVIV=y` are enabled to compile the Mesa Etnaviv driver.
* `BR2_PACKAGE_MESA3D_TEFLON=y` is enabled to build the Teflon TensorFlow Lite delegate library (`libteflon.so`).
* `BR2_PACKAGE_TENSORFLOW_LITE=y` is enabled to build the TensorFlow Lite engine.

---

## 2. On-Target Validation

### A. Kernel Driver Verification
Once booted, verify that the `etnaviv` DRM driver successfully probed the hardware:
```bash
dmesg | grep -i etnaviv
```
Verify that the DRM render node exists (usually `/dev/dri/renderD128`):
```bash
ls -l /dev/dri/renderD128
```

### B. TensorFlow Lite Delegate Inference
To load a quantized TensorFlow Lite model (`.tflite`) using the Teflon NPU delegate, load the delegate in your Python or C++ application:

**Python Example:**
```python
import tflite_runtime.interpreter as tflite

# Load the Teflon delegate from Mesa
teflon_delegate = tflite.load_delegate("/usr/lib/libteflon.so")

# Initialize interpreter with the NPU delegate
interpreter = tflite.Interpreter(
    model_path="landing_quantized.tflite",
    experimental_delegates=[teflon_delegate]
)
interpreter.allocate_tensors()
```
