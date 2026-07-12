# Camera Verification & Testing Tools

This guide outlines how to verify and test the Video4Linux2 (V4L2) camera interfaces using the mainline Linux **Media Controller** topology on the Radxa Cubie A5E flight controller.

---

## 1) Included V4L2 & Media Controller Packages

The Buildroot image includes the standard upstream **Video4Linux2 (V4L2)** and **Media Controller** utilities:
* `BR2_PACKAGE_LIBV4L=y` (V4L2 userspace libraries)
* `BR2_PACKAGE_V4L_UTILS=y` (Command-line diagnostic tools, including `v4l2-ctl` and `media-ctl`)

---

## 2) Mainline Media Controller Architecture

Under mainline Linux, camera sensors (such as the IMX219) are not represented as monolithic video devices. Instead, they exist as individual **media entities** inside a media graph that connects the camera sensor, the CSI receiver (CSI-RX), the Image Signal Processor (ISP), and the DMA memory writer.

Before you can capture frames, you must orchestrate and link these entities using `media-ctl`.

### A. List Media Devices and Graphs
Inspect the media topology of the system:
```bash
media-ctl -d /dev/media0 -p
```
This prints the complete topological map, showing each entity, its pads, and current link states.

### B. Configuring the Media Pipeline
Before streaming, reset the links and establish the active data path from the sensor to the capture interface.

Example pipeline setup for an IMX219 sensor connected to the CSI-RX:
```bash
# 1. Reset all links to a clean state
media-ctl -d /dev/media0 -r

# 2. Enable the link between the camera sensor pad and the CSI receiver pad
media-ctl -d /dev/media0 -l '"imx219 0-0010":0 -> "sun6i-csi":0[1]'

# 3. Configure the format and resolution at each step of the pipeline
media-ctl -d /dev/media0 -V '"imx219 0-0010":0 [fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 -V '"sun6i-csi":0 [fmt:SRGGB10_1X10/1920x1080]'
```

---

## 3) Command Line Capturing (`v4l2-ctl`)

Once the media controller pipeline is linked and configured, you can stream frames from the DMA output video node (usually `/dev/video0`).

### A. List Formats on the Video Node
```bash
v4l2-ctl -d /dev/video0 --list-formats-ext
```

### B. Capture a Raw Frame using DMA-Mapped Buffers
Since the driver uses the `vb2_dma_contig` memory allocator, you must capture using memory-mapped streaming (`--stream-mmap`):
```bash
v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=RG10 \
    --stream-mmap \
    --stream-to=/tmp/raw_frame.raw \
    --stream-count=1
```

### C. Copy and View on Host
Copy the raw frame back to your development machine:
```bash
scp root@cubie-a5e-flight:/tmp/raw_frame.raw .
```
You can convert or inspect raw Bayer frames on your host using tools like `rawtran` or ImageMagick.

---

## 4) Python 3 V4L2 Memory-Mapped Streamer

Because mainline driver pipelines use memory-mapped streaming buffers, a standard Python `read()` on the `/dev/videoX` node will fail. You must use `mmap` to stream frames.

Below is a lightweight Python script that opens the device node, requests memory-mapped buffers, maps them into Python's address space, and captures a frame:

```python
#!/usr/bin/env python3
import os
import mmap
import fcntl
import ctypes

# V4L2 IOC IOCTL commands and structures
VIDIOC_REQBUFS = 0xc0145608
VIDIOC_QUERYBUF = 0xc0445609
VIDIOC_QBUF = 0xc044560f
VIDIOC_DQBUF = 0xc0445611
VIDIOC_STREAMON = 0x40045653

V4L2_BUF_TYPE_VIDEO_CAPTURE = 1
V4L2_MEMORY_MMAP = 1

class v4l2_requestbuffers(ctypes.Structure):
    _fields_ = [
        ("count", ctypes.c_uint32),
        ("type", ctypes.c_uint32),
        ("memory", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 2),
    ]

class v4l2_timeval(ctypes.Structure):
    _fields_ = [("tv_sec", ctypes.c_long), ("tv_usec", ctypes.c_long)]

class v4l2_buffer(ctypes.Structure):
    _fields_ = [
        ("index", ctypes.c_uint32),
        ("type", ctypes.c_uint32),
        ("bytesused", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("field", ctypes.c_uint32),
        ("timestamp", v4l2_timeval),
        ("timecode", ctypes.c_uint32 * 4),
        ("sequence", ctypes.c_uint32),
        ("memory", ctypes.c_uint32),
        ("offset", ctypes.c_uint32),
        ("length", ctypes.c_uint32),
        ("reserved2", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
    ]

def capture_mmap_frame(device_path="/dev/video0", width=1920, height=1080):
    fd = os.open(device_path, os.O_RDWR | os.O_NONBLOCK)
    
    # 1. Request 1 buffer
    req = v4l2_requestbuffers(count=1, type=V4L2_BUF_TYPE_VIDEO_CAPTURE, memory=V4L2_MEMORY_MMAP)
    fcntl.ioctl(fd, VIDIOC_REQBUFS, req)
    
    # 2. Query buffer and mmap
    buf = v4l2_buffer(index=0, type=V4L2_BUF_TYPE_VIDEO_CAPTURE, memory=V4L2_MEMORY_MMAP)
    fcntl.ioctl(fd, VIDIOC_QUERYBUF, buf)
    
    # Map buffer memory into user address space
    mm = mmap.mmap(fd, buf.length, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=buf.offset)
    
    # 3. Queue the buffer
    fcntl.ioctl(fd, VIDIOC_QBUF, buf)
    
    # 4. Stream ON
    buf_type = ctypes.c_uint32(V4L2_BUF_TYPE_VIDEO_CAPTURE)
    fcntl.ioctl(fd, VIDIOC_STREAMON, buf_type)
    
    # 5. Wait for frame using select
    import select
    select.select([fd], [], [])
    
    # 6. Dequeue buffer
    fcntl.ioctl(fd, VIDIOC_DQBUF, buf)
    
    # Save raw frame content
    frame_data = mm.read(buf.bytesused)
    with open("/tmp/python_mmap_frame.raw", "wb") as f:
        f.write(frame_data)
        
    print(f"Captured {len(frame_data)} bytes to /tmp/python_mmap_frame.raw")
    
    mm.close()
    os.close(fd)

if __name__ == "__main__":
    capture_mmap_frame()
```
