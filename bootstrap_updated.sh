#!/bin/bash
# bootstrap_mainline_workspace.sh
# Mainline-First Architecture Blueprint Initialization for Allwinner T527 / Cubie A5E

set -e

echo "Initializing Antigravity AI IDE Project Workspace Profile..."
mkdir -p .antigravity
mkdir -p workspace_prompts

# ==============================================================================
# STEP 1: Upgrading Workspace Profile Configuration (.antigravity/profiles.json)
# ==============================================================================
cat << 'EOF' > .antigravity/profiles.json
{
  "project_name": "t527-mainline-flightstack",
  "architecture_bounds": {
    "host_domain": "ARM Cortex-A55 (Mainline Linux framework, standard POSIX, V4L2 M2M)",
    "realtime_domain": "XuanTie E906/E907 RISC-V Core (Bare-metal / Melis RTOS via local ITCM/DTCM)"
  },
  "engineering_mandate": {
    "workflow": "Strictly mainline Linux syntax. Leverage Bootlin/Paul Kocialkowski upstream baselines.",
    "memory_allocator": "Videobuf2 contiguous standard dma-buf (vb2_dma_contig)",
    "register_reference": "Use Radxa linux-aw2501 tree purely as an open-book TRM for register footprints.",
    "riscv_strategy": "Reverse engineer and pull initialization sequences directly from Allwinner's official sunxi-melis SDK.",
    "npu_strategy": "Completely reject closed vendor Acuity stacks and proprietary /dev/galcore drivers. Enforce mainline Etnaviv and Mesa Teflon delegate."
  },
  "logging_audit_policy": {
    "require_trace_logs": true,
    "trace_log_format": "Markdown (.md) tracking TRM/Bootlin/Melis/Mesa vs Mainline translation logic",
    "required_artifacts": [".patch files formatted cleanly for buildroot out-of-tree trees / upstream RFC"]
  }
}
EOF

echo "Generating Multi-Phase Architecture Blueprints..."

# ==============================================================================
# PROMPT 1: Mainline Camera Ingestion (MIPI-CSI & ISP Mainline Configuration)
# ==============================================================================
cat << 'EOF' > workspace_prompts/prompt1_mainline_camera.md
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
EOF

# ==============================================================================
# PROMPT 2: VEU Stateless Hardware Encoder Port (Bootlin Cedrus Paradigm)
# ==============================================================================
cat << 'EOF' > workspace_prompts/prompt2_mainline_veu_encoder.md
# Blueprint 2: Direct VEU Hardware Encoding Engine via Cedrus Extension

## 1. Mandated Rules
* **STRICTLY MAINLINE:** Replicate the stateless `v4l2_m2m` design framework. No legacy proprietary video engines or Android `ion` dependencies allowed.
* **UPSTREAM PARADIGM:** Extend standard `cedrus` structures natively.
* **ZERO-COPY PIPE:** Memory buffers must feed directly into the hardware using standard user-space `dma-buf` tokens.

## 2. Context & Origins
* **Where this comes from:** This architecture directly mirrors Paul Kocialkowski’s mainline `cedrus/h264-encoding` branch and utilizes his command-line test application (`v4l2-cedrus-enc-test`) as our design pattern. The vendor tree (`drivers/media/video/sunxi-cedar/`) is read strictly to harvest raw register addresses, macroblock configurations, and encoding slices.

## 3. Engineering Goals
* Create a clean, mainline-style `v4l2_m2m` memory-to-memory kernel driver for the T527 Video Encoder Unit (VEU).
* Add a custom Buildroot package compilation entry for the standalone C encoding validation engine.

## 4. Implementation Phases
### Phase 1: Device Tree Block Mapping
* Map the structural VEU `.dtsi` sub-node mapping out base register offsets (`0x07090000`), system clock controls (`CLK_BUS_VEU`), and corresponding hardware GIC interrupts.

### Phase 2: V4L2 M2M Infrastructure Scaffolding
* Construct an empty `v4l2-mem2mem` driver framework (`sun55i-veu.c`) utilizing standard `vb2_dma_contig` ingestion queues.

### Phase 3: Register Surgery Execution
* Transplant raw bitstream generation steps, sequence parameter sets, and hardware command states from the vendor reference files directly into the clean kernel `.device_run` runtime handler loop.

### Phase 4: Buildroot Package Porting
* Construct an out-of-tree Buildroot directory tree (`package/sunxi-veu-enc-test/`) with a standard `.mk` script and `Config.in`. 
* Cross-compile Paul's C test engine to run on the ARM host domain, processing inputs from the camera's exported memory maps.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt2_veu_transplant_audit.md`. It must document exactly how each register was extracted from the vendor tree and re-mapped inside the clean mainline `.device_run` logic blocks.
* **ARTIFACT:** Output `.antigravity/patches/0002-media-allwinner-veu-m2m-driver.patch`.
EOF

# ==============================================================================
# PROMPT 3: RISC-V Bare-Metal Ring-Buffer Ingestion (Reverse-Engineered from SDK)
# ==============================================================================
cat << 'EOF' > workspace_prompts/prompt3_riscv_ingestion.md
# Blueprint 3: XuanTie RISC-V Core Bare-Metal Ring-Buffer & FPGA Dual SPI Loop

## 1. Mandated Rules
* **STRICTLY ISOLATED:** The real-time firmware must run completely decoupled from host Linux memory space.
* **ZERO DDR BUS CONTENTION:** Force all execution parameters inside internal tightly-coupled hardware memory blocks.
* **ALLOCATION FREE:** Absolutely no runtime heap allocations; use rigid, fixed structures.

## 2. Context & Origins
* **Where this comes from:** Low-level peripheral initialization configurations, power configurations, and clock gating routines are reverse-engineered directly from Allwinner’s official `sunxi-melis` SDK examples written for the XuanTie E906/E907 real-time processor complex.

## 3. Engineering Goals
* Establish microsecond-level deterministic ingestion firmware executing inside the auxiliary real-time core.
* Maintain full-duplex Dual SPI loops capturing sensor frames from an external FPGA fabric.

## 4. Implementation Phases
### Phase 1: Interrupt Steering Validation
* Write initialization values into the SoC Security Peripherals Controller (SPC) and CCU block matrices to route Dual SPI hardware interrupts cleanly into the RISC-V PLIC instead of the ARM host GIC.

### Phase 2: TCM Linker Layout Configuration
* Draft a strict, rigid linker script (`.ld`) that forces your high-priority SPI ISR functions directly into the Instruction TCM (ITCM) and builds the circular message buffers inside Data TCM (DTCM) or reclaimed SRAM C (320KB block pool).

### Phase 3: Ingestion Engine Development
* Construct an optimized pointer-exchange data ring handling 32-byte or 64-byte blocks to manage full-duplex SPI payloads without processing stalls.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt3_riscv_tcm_map.md`. This log must chart the precise memory boundaries of the ITCM, DTCM, and SRAM C layers, mapping how the `sunxi-melis` SDK register steps were implemented.
EOF

# ==============================================================================
# PROMPT 4: Mailbox Inter-Processor Communication (IPC) Synchronization
# ==============================================================================
cat << 'EOF' > workspace_prompts/prompt4_mailbox_sync.md
# Blueprint 4: Bidirectional Mailbox Doorbell & Raw Shared Memory Link

## 1. Mandated Rules
* **LIGHTWEIGHT EXECUTION:** Do not include heavy, high-overhead frameworks like OpenAMP or RPMsg.
* **MAINLINE DRIVER FIRST:** Extend baseline driver models natively under standard directory hierarchies (`drivers/mailbox/`).
* **DETERMINISTIC BOUNDS:** Coordinate interactions using fixed memory pointer windows in shared SRAM.

## 2. Context & Origins
* **Where this comes from:** This low-overhead IPC model implements a hardware doorbell mechanism based on a raw ZynqMP OCM-to-R5 structural architecture. The low-level lane routing mechanics are reverse-engineered directly from the vendor's `drivers/mailbox/sunxi-mailbox.c` implementation.

## 3. Engineering Goals
* Build an out-of-tree driver patch enabling ultra-low-latency state-machine handshakes between the ARM Cortex-A55 Linux host and the XuanTie RISC-V real-time core.

## 4. Implementation Phases
### Phase 1: Kernel Mailbox Extension Patch
* Isolate the hardware-specific register management loops from the vendor code. Rewrite them into a clean, independent mainline-compliant extension driver mapped inside `drivers/mailbox/`.

### Phase 2: RISC-V Mailbox Doorbell Handler
* Implement an immediate interrupt service routine (`sunxi_mailbox_isr()`) pinned inside the RISC-V core's ITCM space. This handler must instantly parse incoming pointer address signals from fixed Message SRAM blocks without execution delay.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt4_mailbox_sync_trace.md`. This log must track data latency boundaries, provide a complete memory map of the shared SRAM window offsets, and document the state-machine handshake states.
* **ARTIFACT:** Output `.antigravity/patches/0003-mailbox-sunxi-t527-driver.patch`.
EOF

# ==============================================================================
# PROMPT 5: Local OpenOCD & GDB Debugging Bridge via ARM MMIO
# ==============================================================================
cat << 'EOF' > workspace_prompts/prompt5_riscv_debug_bridge.md
# Blueprint 5: Local OpenOCD & GDB Debugging Bridge via ARM MMIO

## 1. Mandated Rules
* **HARDWARE-LESS TRACING:** Implement debugging routines without relying on a physical hardware JTAG adapter probe or external lines.
* **CLEAN PERMISSION CONTROL:** Expose access parameters securely via U-Boot parameters using standard memory flag configurations.
* **CROSS-COMPILATION ALIGNMENT:** Maintain compilation scripts within out-of-tree Buildroot structures.

## 2. Context & Origins
* **Where this comes from:** This architecture leverages Allwinner's hardware routing design, which maps the XuanTie RISC-V Debug Module Interface (DMI) registers straight into the global system interconnect memory map. This design allows local utilities on the ARM host to trace real-time co-processor states directly over the system bus.

## 3. Engineering Goals
* Provide a production-ready out-of-tree Buildroot script that compiles OpenOCD with native memory-mapped (`sunxi_mmap`) I/O capability.
* Enable local GDB debugging connectivity into the XuanTie E906/E907 real-time core directly from the ARM terminal under Linux.

## 4. Implementation Phases
### Phase 1: Boot Unlocking Parameter Patch
* Add the `iomem=relaxed` system boot configuration to the default target environment. This ensures user-space utilities are granted unrestricted access to the SoC peripheral nodes via `/dev/mem`.

### Phase 2: OpenOCD Target Script Construction
- Construct an advanced OpenOCD hardware script (`openocd_t527_local.cfg`). Map the configuration to interface using the Allwinner physical memory base address (`0x07090000`).
* Incorporate exact register sequences to activate and toggle the `dmactive` bit within the standard RISC-V `dmcontrol` hardware space.

### Phase 3: Buildroot Package Configuration
* Draft an out-of-tree Buildroot recipe addition (`package/openocd/openocd.mk`) to compile OpenOCD with MMIO support flags enabled.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt5_debugger_trm_alignment.md`. This document must provide an educational map illustrating exactly how the physical RISC-V DMI registers map to the system bus, enabling anyone to reconstruct the debugging pipeline.
EOF

# ==============================================================================
# PROMPT 6: Mainline NPU Acceleration Engine (Mesa Teflon + Etnaviv)
# ==============================================================================
cat << 'EOF' > workspace_prompts/prompt6_mainline_npu.md
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
EOF

echo "----------------------------------------------------------------------"
echo " SUCCESS: Updated Single-Source Workspace Initialization Complete!"
echo "----------------------------------------------------------------------"
echo " The following structures have been written to your workspace tree:"
echo "   1. .antigravity/profiles.json  <- Rules, boundaries, and mandates."
echo "   2. workspace_prompts/          <- Multi-phase educational blueprints (1-6)."
echo ""
echo " Mainline camera, VEU encoder, RISC-V, and FOSS NPU paths are now locked."
echo "----------------------------------------------------------------------"
