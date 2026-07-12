# Building a Mainline-First Flight Stack: T527 / Cubie A5E Architecture & Bring-up

This case study documents the complete architecture and mainline Linux bring-up of the Radxa Cubie A5E flight controller stack. It explores a modern, open-source approach to combining high-level machine learning and telemetry (ARM Cortex-A55 Linux host) with hard real-time flight control (XuanTie RISC-V co-processor) and custom FPGA sensor ingestion.

---

## 1. System Architecture: The Tri-Domain Paradigm

A modern intelligent drone requires both microsecond-level control loops and compute-heavy intelligence (TinyML, computer vision, and high-bandwidth networking). To satisfy these conflicting constraints, the Cubie A5E system is split into three isolated processing domains:

```text
 ┌────────────────────────┐       ┌────────────────────────┐       ┌────────────────────────┐
 │     Linux OS Domain    │       │   Real-Time Co-Core    │       │     Hardware Domain    │
 │ (ARM Cortex-A55 Cores) │       │ (XuanTie E906/907 RV)  │       │      (FPGA Fabric)     │
 ├────────────────────────┤       ├────────────────────────┤       ├────────────────────────┤
 │ High-level logic, ML,  │◄─────►│ Low-latency flight     │◄─────►│ Fast physical I/O,     │
 │ camera capturing, wifi │  IPC  │ loops (iNAV/Betaflight)│  SPI  │ IMU timestamps, DSHOT  │
 └────────────────────────┘       └────────────────────────┘       └────────────────────────┘
```

1. **Host OS Domain (ARM Cortex-A55):** Runs a mainline Linux kernel with `PREEMPT_RT` real-time patches. Pinned CPU cores run camera capture, networking, and TinyML workloads.
2. **Real-Time Domain (XuanTie E906/E907 RISC-V):** Runs bare-metal firmware or Melis RTOS. It executes the critical flight attitude estimation and control loop thread, completely decoupled from the Linux host.
3. **Deterministic Hardware Domain (FPGA):** Directly interfaces with hardware sensors (IMU, Baro, GPS) and motors (DSHOT/PWM). It applies microsecond-level hardware timestamps to sensor data before transferring it to the RISC-V core.

---

## 2. Blueprint 1: Mainline Camera Ingestion (MIPI-CSI)

To stream high-rate camera frames to flight-assist vision models without vendor-specific overhead:
* **The Mainline Way:** We completely reject legacy Allwinner `sunxi-vfe` wrappers, opting for standard Video4Linux2 (V4L2) **Media Controller** topologies.
* **Pipeline Orchestration:** A `media-ctl` script links the physical IMX219 sensor subdevice directly to the SoC's CSI receiver.
* **Zero-Copy Ingestion:** The pipeline allocates memory queues using `vb2_dma_contig` (Videobuf2 contiguous DMA memory allocator). This exposes direct userspace DMA buffer file descriptors (`dma-buf`), allowing camera frames to feed straight into userspace processing queues with zero copy operations.

---

## 3. Blueprint 2: VEU Stateless Video Encoder (Paul Kocialkowski Paradigm)

To stream live H.264 video telemetry over the network:
* **Stateless Design:** We extend the standard Linux `cedrus` V4L2 stateless memory-to-memory (`v4l2_m2m`) framework to control the SoC's Video Encoder Unit (VEU).
* **Bypassing Blobs:** Instead of using closed vendor middleware or custom allocations, memory buffers feed natively into the VEU hardware registers using standard DMA tokens, enabling high-efficiency, low-power telemetry streams.

---

## 4. Blueprint 3: XuanTie RISC-V Bare-Metal Ingestion

To achieve microsecond-level deterministic attitude estimation:
* **Memory Isolation:** The co-processor firmware runs fully isolated inside the processor's **Instruction TCM (ITCM)** and **Data TCM (DTCM)** blocks. This ensures zero bus contention with the ARM host's DDR memory access.
* **Dual SPI Ring-Buffer:** The RISC-V core runs a dedicated SPI interrupt handler that ingests hardware-timestamped IMU sensor blocks from the FPGA. Data is queued into fixed-size circular buffers in TCM without any heap allocation or lock stalls.

---

## 5. Blueprint 4: Mailbox Inter-Processor Communication (IPC)

Instead of introducing heavy, unpredictable frameworks like RPMsg or OpenAMP:
* **Mailbox Doorbell:** We write a clean mainline `drivers/mailbox/` driver extension. It uses hardware mailbox registers to trigger instant doorbells between the ARM and RISC-V domains.
* **Shared SRAM Window:** Payload data is exchanged via a small, fixed shared memory window in System SRAM C. Handshakes are coordinated using immediate, allocation-free pointer exchange states.

---

## 6. Blueprint 5: Local Debugging Bridge via ARM MMIO

To allow debugging of the co-processor without connecting external JTAG hardware probes:
* **System Bus Debugging:** The SoC maps the XuanTie RISC-V Debug Module Interface (DMI) registers directly into the global ARM memory-mapped I/O (MMIO) bus space.
* **MMIO-Mapped OpenOCD:** We cross-compile OpenOCD in Buildroot with a memory-mapped driver (`sunxi_mmap`). The host ARM core can read/write the co-processor's run-control register addresses directly over the system bus, exposing a local GDB server target port (`localhost:3333`) on the flight computer terminal.
* **On-Board Multi-arch GDB:** We compile the target GDB debugger in Buildroot with `--enable-targets=all` enabled. This generates a native GDB debugger running on the ARM64 flight controller that supports full debugging of guest RISC-V binary structures, allowing developers to inspect variables, backtrace stacks, and step through co-processor execution directly from the target shell.

---

## 7. Blueprint 6: Mainline NPU Acceleration (Etnaviv + Mesa Teflon)

To accelerate computer vision and TinyML landing models:
* **No Closed-Source Blobs:** We completely reject the closed vendor Vivante driver (`/dev/galcore`) and closed `timvx-delegate` libraries.
* **FOSS Compute Stack:** We configure the built-in **Etnaviv** kernel DRM driver (`CONFIG_DRM_ETNAVIV=y`) and patch Mesa 3D to compile the **Teflon** TensorFlow Lite delegate (`libteflon.so`).
* **Under the Hood Pipeline:**
  ```text
  TFLite Application -> libteflon.so -> Gallium3D Shaders -> Etnaviv DRM (ioctl) -> VIP9000 NPU
  ```
  Teflon translates the neural network graph into Gallium compute shaders. The userspace Mesa compiler lowers these to NPU machine code, submitting them as standard Graphics Execution Manager (GEM) command buffers to the kernel. Camera frames are shared via `dma-buf` file descriptors, enabling zero-copy hardware inference.

---

## 8. Mainline Linux Wi-Fi Integration

To establish reliable ground control telemetry links over Wi-Fi:
* **Standard Mainline Interfaces:** The AIC8800 Wi-Fi module driver compiles cleanly against the mainline kernel's `cfg80211` / `mac80211` wireless frameworks.
* **Patching API Drift:** We maintain clean Buildroot out-of-tree patches to resolve kernel API changes (such as the removal of `ieee80211_ptr` in modern kernel versions).
* **Firmware Packaging:** Buildroot packages the required SDIO firmware files directly into `/lib/firmware/aic8800/` ensuring automated target-side interface bring-up.

---

## 9. Key Architectural Takeaways

By designing the system from the ground up to utilize standard, upstreamed Linux kernel APIs (DRM, V4L2 M2M, Media Controller, and mailbox drivers), the Radxa Cubie A5E flight stack remains:
1. **Fully Auditable:** Code contains no proprietary binary blobs, preventing security or safety-critical blind spots.
2. **Maintainable:** The OS can be updated to future kernel versions without breaking out-of-tree hardware drivers.
3. **Performant:** Zero-copy memory interfaces and hardware domain isolation achieve flight controller determinism alongside advanced TinyML capabilities.
