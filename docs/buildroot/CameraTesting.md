# Camera Verification & Testing Tools

This guide outlines how to verify the Video4Linux2 (V4L2) camera interfaces (MIPI-CSI or USB-UVC webcams) on the Cubie A5E Buildroot operating system.

---

## 1) Included V4L2 Packages

The Buildroot image includes the **Video4Linux2 (V4L2)** core and utilities from `cubie_a5e_defconfig`:
* `BR2_PACKAGE_LIBV4L=y` (V4L2 userspace libraries)
* `BR2_PACKAGE_V4L_UTILS=y` (Command-line diagnostic tools, including `v4l2-ctl`)

---

## 2) Command Line Verification (`v4l2-ctl`)

Log in to the board via SSH (Dropbear) or the debug console terminal and run the following diagnostic sequence:

### A. List connected video devices
```bash
v4l2-ctl --list-devices
```
*Expected output:*
```text
sunxi-video (platform:sunxi-video):
    /dev/video0
    /dev/video1
```

### B. List supported resolutions and formats
Inspect the capabilities of your camera (replace `/dev/video0` with the correct node name):
```bash
v4l2-ctl --list-formats-ext -d /dev/video0
```
Look for available stream compressions:
* **`MJPG`** (Motion JPEG - hardware compressed)
* **`YUYV`** (Raw uncompressed 4:2:2)

### C. Capture a test frame (JPEG format)
If the camera supports hardware JPEG encoding (`MJPG`):
```bash
v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=MJPG --stream-mmap --stream-to=/tmp/test_frame.jpg --stream-count=1
```

### D. Capture a test frame (Raw YUYV format)
If the camera only supports raw formats:
```bash
v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=YUYV --stream-mmap --stream-to=/tmp/test_frame.yuyv --stream-count=1
```

### E. Copy and view the image on your host PC
Use `scp` from your local terminal to transfer the file and verify the focus and lens alignment:
```bash
scp root@cubie-a5e-flight:/tmp/test_frame.jpg .
```

---

## 3) Python 3 V4L2 Camera Capture Script

Below is a lightweight Python script that opens the V4L2 device node directly using Python's standard libraries to read raw YUYV frames without needing external OpenCV installations:

```python
#!/usr/bin/env python3
import os
import ctypes

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
    try:
        # Open V4L2 device file
        fd = os.open(device_path, os.O_RDWR)
        print(f"Opened V4L2 device: {device_path}")
        
        # Read raw stream data directly from fd if the camera driver supports read()
        try:
            raw_data = os.read(fd, width * height * 2) # YUYV is 2 bytes per pixel
            with open("/tmp/python_frame.yuyv", "wb") as f:
                f.write(raw_data)
            print("Frame successfully captured to /tmp/python_frame.yuyv")
        except OSError:
            print("Direct read() not supported by driver. Use v4l2-ctl to stream via mmap.")
        
        os.close(fd)
    except Exception as e:
        print(f"Error capturing frame: {e}")

if __name__ == "__main__":
    capture_raw_frame()
```
