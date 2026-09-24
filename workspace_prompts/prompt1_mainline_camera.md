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

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt1_camera_mainline_setup.md`. This must map out every hardware pin, register, and media endpoint link configured during development to provide an educational reference trail.
* **ARTIFACT:** Output `.antigravity/patches/0001-dts-allwinner-t527-camera-pipeline.patch`.
