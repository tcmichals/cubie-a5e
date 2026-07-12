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
