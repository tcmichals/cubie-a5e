# Blueprint 1: Mainline Camera Capture & Media Controller Linkage

## 1. Mandated Rules
* **STRICTLY MAINLINE:** Absolutely no usage of legacy `sunxi-vfe` vendor drivers or proprietary Allwinner wrappers.
* **UPSTREAM PARADIGM:** Implement using standard Linux media-controller topologies.
* **ZERO-COPY ALLOCATION:** Enforce `vb2_dma_contig` allocations to pass `dma-buf` tokens cleanly to user space.

## 2. Context & Origins
* **Where this comes from:** This implementation leverages the hardware-level pin mappings, clock trees, and pipeline routing schemas established upstream for the Allwinner T527 by Paul Kocialkowski. The legacy vendor source (`linux-aw2501`) is treated purely as an open-book Technical Reference Manual (TRM) for physical hardware verification.

## 3. Engineering Goals
* Establish a clean out-of-tree Buildroot patch linking an IMX219 sensor over MIPI-CSI lanes on the Radxa Cubie A5E.
* Expose standard `/dev/videoX` subdevices capable of exporting raw frames directly through memory file descriptors.

## 4. Implementation Phases
### Phase 1: Device Tree Bindings & Sensor Linkage
* Extract the precise base hardware layout configurations from Paul Kocialkowski's upstreamed T527 MIPI-CSI bindings.
* Draft a mainline-compliant Device Tree node patch (`.patch`) adding the IMX219 sensor definitions, clock relationships, and endpoint port routing configurations to `sun55i-a527.dtsi` and the board-specific `.dts`.

### Phase 2: Media Controller Orchestration
* Scaffold a structural setup shell script executing standard `media-ctl` and `v4l2-ctl` statements to map routing links from the physical CSI receiver into the active mainline ISP engine.

## 5. Quadcopter Visual Target Tracking & Video Storage Architecture
* **Target Tracking Sensor Selection:** Sony IMX708 (or IMX335 5MP / OV9281 Global Shutter). High resolution (1080p60/2K) provides high pixel density for target tracking algorithms (YOLOv8-nano / OpenCV KCF), while Hardware HDR prevents white-out glares when flying into direct sunlight or heavy shadows.
* **Dual-Branch Zero-Copy Pipeline:**
  * **Branch A (Target Tracking):** Downscaled frame (e.g. 416x416) passed to Allwinner NPU / ARM CPU for real-time bounding box detection and flight control.
  * **Branch B (Onboard Storage):** Full-resolution 1080p60 NV12 stream passed directly via V4L2 `dma-buf` memory sharing to the Allwinner Hardware Encoder (`cedrus` / `v4l2h264enc`) to write high-bitrate MP4 files to SD storage (`/mnt/sdcard/recordings/`) with **0% CPU memcpy overhead**.

## 6. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Maintain `docs/buildroot/CameraTesting.md` with V4L2 Media Controller link setups, `mmap` streaming examples, and GStreamer hardware encoding pipelines.
* **ARTIFACT:** Output `.antigravity/patches/0001-dts-allwinner-t527-camera-pipeline.patch`.
