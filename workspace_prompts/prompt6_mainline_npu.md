# Blueprint 6: Mainline NPU Acceleration Engine via Mesa Teflon & Etnaviv

## 1. Mandated Rules
* **STRICTLY MAINLINE & FOSS:** Zero proprietary vendor binary tools (`Acuity` toolchains) or vendor out-of-tree drivers (`/dev/galcore`). 
* **DRM FRAMEWORK COMPLIANCE:** Force hardware communication inside the kernel through standard Direct Rendering Manager channels via `CONFIG_DRM_ETNAVIV`.
* **ZERO-COPY IMAGE INGESTION:** The NPU processing layout must consume frames dynamically exported from the camera using standard user-space `dma-buf` file descriptors.

## 2. Context & Origins
* **Where this comes from:** The Allwinner T527 features an integrated VeriSilicon Vivante VIP9000-series NPU IP block (recognized as a Vivante GC9000 compute accelerator). This blueprint implements user-space acceleration using Tomeu Vizoso’s open-source **Teflon TensorFlow Lite delegate** merged natively inside the **Mesa 3D Graphics Library**, completely bypassing all vendor lock-ins.

## 3. Engineering Goals
* Construct a Buildroot out-of-tree patch that wires up the T527 NPU device tree sub-node and compiles Mesa 3D with the Teflon compute tracker enabled.
* Expose an operational `libteflon.so` library in target rootfs space to handle quantized `.tflite` flight stack models.

## 4. Implementation Phases
### Phase 1: NPU Device Tree Binding Patch
* Draft a mainline-compliant device tree sub-node targeting the NPU hardware space within `sun55i-a527.dtsi`.
* Explicitly map out the NPU MMIO register bounds (`0x07000000`), connect the system interrupt controller lanes, and configure the mandatory target CCU clock properties (`CLK_BUS_NPU` and `CLK_NPU`).

### Phase 2: Buildroot Mesa3D Recipe Overrides
* Construct an out-of-tree Buildroot recipe extension modifying the target `mesa3d.mk` parameters.
* Apply mandatory cross-compilation compiler flags to build Mesa with the `etnaviv` Gallium engine enabled and `teflon=true` activated to generate the runtime library.

### Phase 3: Application Integration Verification
* Write a clean python/C++ validation execution wrapper script demonstrating how the flight control logic requests model compute offloading over the `/usr/lib/libteflon.so` boundary, validating operational logging tracking records.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt6_npu_mainline_audit.md`. This log must document the complete memory mapping offsets of the GC9000 engine, confirm operational operator fallbacks, and track model execution latencies.
* **ARTIFACT:** Output `.antigravity/patches/0004-dts-mesa-allwinner-t527-npu-acceleration.patch`.
