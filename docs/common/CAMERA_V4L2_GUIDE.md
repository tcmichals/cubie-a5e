# 📷 Common Subsystem Guide: Camera & V4L2 Video Pipeline

This document details the **Camera Capture Subsystem**, Video4Linux2 (V4L2) topology, MIPI CSI receiver interfaces, and Media Controller pipeline configuration for the **Radxa Cubie A7A** (Allwinner A733) and **Radxa Cubie A5E** (Allwinner A527/T527).

---

## 1. Hardware Architecture & Specifications

| Parameter | Allwinner A733 Specification | Notes |
| :--- | :--- | :--- |
| **MIPI CSI-2 Receivers** | 3× MIPI CSI Controllers (4 + 4 + 2-lane, up to 2.0 Gbps/lane) | CSI0 (`0x05820000`), CSI1 (`0x05821000`), CSI2 (`0x05822000`) |
| **CSI Interrupt Vectors**| **`GIC_SPI 125` (`0x7d`), `126` (`0x7e`), `127` (`0x7f`)** | Hardware DMA frame end / packet sync interrupts |
| **Image Signal Processor**| **Hardware ISP at `0x05900000` (`0x1300` bytes), behind the IOMMU (`iommus = <&mmu_aw 2 0>`)**. Vendor driver for A733: `vin-isp/isp600/` (not ISP 5.0 / 5.22) | Hardware statistics for 3A; the 3A control loop itself is software (§5) |
| **ISP Interrupt Vector** | **`GIC_SPI 129` (`0x81`)** | ISP statistic calculation and processing complete |
| **Video Input Capture** | 18× Video Input DMA Channels (`vinc0` to `vinc17` at `0x05830000`–`0x05835000`) | Multi-stream parallel video DMA capture engines |
| **Supported Sensors** | **Sony IMX219 (8MP)**, **Sony IMX477 (12MP HQ)**, **OmniVision OV5647 (5MP)** | Connected via 15-pin / 22-pin FPC MIPI CSI-2 connector |

---

## 2. Hardware Topology & Zero-Copy Pipeline

```
┌──────────────┐     MIPI CSI-2     ┌──────────────┐     Internal Bus     ┌──────────────┐     DMA-BUF     ┌───────────────────────────┐
│ CMOS Sensor  │ ─────────────────> │ CSI Receiver │ ───────────────────> │ Allwinner    │ ──────────────> │ • VPU Hardware Encoder    │
│ (IMX219 8MP) │   (2/4-Lane D-PHY) │ (0x05820000) │                      │ ISP 5.0      │   (Zero-Copy)   │ • 3 TOPs NPU (AI Vision)  │
└──────────────┘                    └──────────────┘                      └──────────────┘                 │ • V4L2 Node (/dev/video0) │
                                                                                                           └───────────────────────────┘
```

---

## 3. Media Controller Pipeline Configuration

In modern Linux (Linux 7.1), camera pipelines use the **Media Controller** framework to link sensor subdevices, CSI receivers, and capture endpoints:

### Enumerating Pipeline Entities
```bash
media-ctl -p -d /dev/media0
```

### Linking Sensor to CSI Capture
```bash
# Link IMX219 sensor pad to CSI video input pad
media-ctl -d /dev/media0 -l '"imx219 1-0010":0 -> "sunxi-csi":0[1]'

# Configure pixel format, resolution, and framerate on the pipeline
media-ctl -d /dev/media0 -V '"imx219 1-0010":0 [fmt:SRGGB10_1X10/1920x1080 field:none]'
media-ctl -d /dev/media0 -V '"sunxi-csi":0 [fmt:SRGGB10_1X10/1920x1080 field:none]'
```

---

## 4. Video Capture Verification & Stream Testing

### 1. Identify Video Capture Node
```bash
v4l2-ctl --list-devices
```

### 2. Query Formats Supported by Sensor
```bash
v4l2-ctl -d /dev/video0 --list-formats-ext
```

### 3. Capture Raw Test Frame
```bash
v4l2-ctl -d /dev/video0 \
    --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
    --stream-mmap --stream-count=1 --stream-to=/tmp/test_frame.raw
```

### 4. Continuous Real-time Stream Verification (FPS Testing)
```bash
v4l2-ctl -d /dev/video0 \
    --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
    --stream-mmap --stream-count=300
```
- Observe console output for steady 30 FPS / 60 FPS delivery without DMA buffer underruns.

---

## 5. A733 Hardware ISP Plan (Strict)

**Goal:** camera frames processed by the A733 hardware ISP, with **no libcamera Software ISP**. Owner decision, 2026-11-09.

**Status (2026-11-09):** not started. The verified pipeline is OV5647 → MIPI CSI-2 → CSI bridge → `/dev/video0` raw `SGBRG10` 1080p @ 30.00 fps (CUBIE_A7A_DEBUG_LOG.md Milestone 19). Debayering is done by the libcamera Software ISP (`0001-pipeline-simple-enable-Software-ISP-for-sun6i-csi.patch`).

This section is the detailed plan: rules, facts, exit criteria and the step checklist (§5.3, steps 0.1–4.6). TODO.md (Part I §2, "A733 Hardware ISP") holds only the phase status and a "Where we are" line, updated whenever camera work stops for something else.

### 5.1 Rules
1. Phases run in order. A phase starts only when the previous phase's exit criteria have passed **on the board** and the result is recorded in `docs/platforms/CUBIE_A7A_DEBUG_LOG.md`.
2. Hardware facts come from the vendor BSP (`linux-a733/bsp/drivers/vin/`, `bsp/configs/linux-5.15/sun60iw2p1.dtsi`) or silicon readbacks, cited by file:line. Not from this document alone.
3. Driver work happens in `linux-cubie` on the A7A branch, as a proper V4L2 driver (media controller, subdevs, `videobuf2-dma-contig`), so it can go upstream later. No vendor `vin` module port.
4. Every phase that adds a video or metadata node passes `v4l2-compliance` with no failures.
5. The Software ISP patch stays until Phase 4 passes; then it is removed.

### 5.2 Known facts (vendor BSP)
| Item | Value | Source |
| :--- | :--- | :--- |
| ISP block used on A733 | `isp600` | `bsp/drivers/vin/Makefile:66` (`CONFIG_ARCH_SUN60IW2`) |
| Registers / IRQ | `isp00` at `0x05900000`, size `0x1300`, `GIC_SPI 129` | `bsp/configs/linux-5.15/sun60iw2p1.dtsi:2668` |
| IOMMU | `iommus = <&mmu_aw 2 0>` | same node |
| Settings path | Driver writes register values and tables into a DRAM "load" buffer; hardware fetches it (`bsp_isp_set_load_addr()`, `PARA_LOAD_INT_EN`). `ISP_LOAD_REG_SIZE` `0x1000`; `ISP_LOAD_DRAM_SIZE` `0xAF40` or `0x7700` depending on config | `vin-isp/isp600/isp600_reg_cfg.h:32-36,172,377` |
| Statistics path | Hardware writes 3A statistics to DRAM (`bsp_isp_set_statistics_addr()`), `ISP_STAT_TOTAL_SIZE` `0x6a00` | `isp600_reg_cfg.h:48-51,389` |
| Default settings | `isp_default_reg[]` and tables | `vin-isp/isp_default_tbl.h:32` |
| VIN top / routing | CSI_TOP `0x05800800`; VIPP (scalers) `0x05910000`–`0x05910C00`; CSI DMA `0x05830000`–`0x05835000` | `platform/sun60iw2_vin_cfg.h:18-37` |
| IOMMU masters | ISP = master 2, CSI = master 1; mainline `sun50i-iommu.c` has no A733 support | `platform/sun60iw2_vin_cfg.h:52-53` |
| Mainline CSI ↔ ISP link | `sun6i-csi` already supports an ISP on a DT graph port (`SUN6I_CSI_PORT_ISP`, `sun6i_csi_isp_detect()`); the ISP then owns the media device | `drivers/media/platform/sunxi/sun6i-csi/sun6i_csi.c:40-63,336-346` |
| Mainline reference | `drivers/staging/media/sunxi/sun6i-isp` (V3s ISP, ~2,400 lines): driver structure only, different hardware, no statistics node | `linux-cubie` |

### 5.3 Phases and steps

Tick steps here as they are done. When a phase's exit test passes on the board, also tick that phase in TODO.md and update its "Where we are" line. Vendor paths are under `linux-a733/bsp/drivers/vin/`.

**Exit criteria per phase:**
- Phase 0: §5.4 (hardware map) written with file:line citations and reviewed by the owner.
- Phase 1: 1920×1080 NV12 at 30 fps, 300 frames, 0 drops; colours correct; CPU load compared with the Software ISP; `v4l2-compliance` passes.
- Phase 2: white-balance gains set from userspace change the image within 2 frames; `v4l2-compliance` passes.
- Phase 3: statistics follow the scene, one buffer per frame at 30 fps; `v4l2-compliance` passes.
- Phase 4: `cam` captures with exposure and white balance converging on their own, with the Software ISP patch removed.

- [ ] **Phase 0 — Hardware map (documentation only, no driver code)**. Output: new §5.4 in the camera guide, every fact cited by file:line.
  - [ ] 0.1 Vendor call sequence: `vin.c` probe order, `vin-video/vin_core.c` stream on/off, `vin-isp/sunxi_isp.c` (`s_stream`, load/save buffer allocation, IRQ handler).
  - [ ] 0.2 ISP 600 register map from `vin-isp/isp600/isp600_reg.h` and `isp600_reg_cfg.c`: enable/control, input and output format, interrupt enable/status bits, load/save/statistics address registers, version register.
  - [ ] 0.3 Load buffer layout: `ISP_LOAD_REG_SIZE` (`0x1000`) register image plus table offsets; which `ISP_LOAD_DRAM_SIZE` (`0xAF40` or `0x7700`) applies to the A733 build and why.
  - [ ] 0.4 Statistics buffer layout (AE, AWB, AF, histogram offsets inside `ISP_STAT_TOTAL_SIZE` `0x6a00`).
  - [ ] 0.5 Frame routing: CSI_TOP (`0x05800800`, `top_reg.c`) registers that connect CSI parser → ISP → VIPP (`0x05910000`) → DMA (`0x05830000`); is VIPP required between ISP and DMA?
  - [ ] 0.6 Clocks, resets, power domain for the ISP (vendor `vind` node `sun60iw2p1.dtsi:2563`); which already exist in our `ccu-sun60i-a733.c` and PCK600 driver, which are missing.
  - [ ] 0.7 IOMMU: ISP is IOMMU master 2, CSI master 1 (`platform/sun60iw2_vin_cfg.h:52-53`). Find out how our CSI DMA works today without an IOMMU driver (mainline `sun50i-iommu.c` has no A733 support) and whether ISP DMA works the same way.
  - [ ] 0.8 Interrupt `GIC_SPI 129`: which status bits fire per frame (load done, statistics ready, frame end) and what the vendor handler does for each.
  - [ ] 0.9 Mainline reference: `sun6i-csi` ISP port (`SUN6I_CSI_PORT_ISP`, `sun6i_csi_isp_detect()`, async registration) and staging `sun6i-isp`: list what is reused and what is new.
  - [ ] 0.10 Owner decision, recorded in the camera guide: new driver (e.g. `sun60i-isp`) or extending `sun6i-isp`; driver and compatible names.
  - [ ] 0.11 Owner reviews §5.4. **Exit Phase 0.**
- [ ] **Phase 1 — ISP outputs an image (fixed settings)**. Work in `linux-cubie`, A7A branch.
  - [ ] 1.1 DT: ISP node (reg, IRQ, clocks, resets, power domain) and graph link CSI → ISP in the A733 dtsi / board DTS; binding YAML draft.
  - [ ] 1.2 Missing clocks/resets/power-domain entries added (from 0.6).
  - [ ] 1.3 Driver skeleton: probe, clocks, reset, runtime PM, IRQ request. Board check: probe OK, version register matches the vendor value.
  - [ ] 1.4 V4L2 subdev: Bayer sink pad, YUV source pad, media links; async registration with the `sun6i-csi` bridge. Board check: `media-ctl -p` shows sensor → CSI-2 → CSI → ISP → capture.
  - [ ] 1.5 Load buffer: `dma-contig` allocation, filled from vendor `isp_default_reg[]` and tables; program load address, trigger load. Board check: load-done interrupt seen.
  - [ ] 1.6 Formats: 1920×1080 `SGBRG10` in, NV12 out; routing CSI → ISP → (VIPP) → DMA per 0.5.
  - [ ] 1.7 Board test: 300 frames 1080p30 NV12, 0 drops (`v4l2-ctl --stream-count=300`).
  - [ ] 1.8 Board test: save a frame, colours correct (compare with the Software ISP image of the same scene).
  - [ ] 1.9 CPU load during 1080p30 capture, ISP vs Software ISP, recorded.
  - [ ] 1.10 `v4l2-compliance -m /dev/media0`: no failures.
  - [ ] 1.11 Logged in CUBIE_A7A_DEBUG_LOG.md. **Exit Phase 1.**
- [ ] **Phase 2 — Parameters from userspace**.
  - [ ] 2.1 First subset chosen and located in the load buffer (from 0.3): black level, white-balance gains, colour matrix, gamma.
  - [ ] 2.2 uAPI header with a versioned parameters struct (pattern: `include/uapi/linux/rkisp1-config.h`).
  - [ ] 2.3 Metadata output node (`V4L2_BUF_TYPE_META_OUTPUT`) and new meta format.
  - [ ] 2.4 Parameters applied at a frame boundary (double-buffered load buffer), never mid-frame.
  - [ ] 2.5 Board test: changing white-balance gains changes the image within 2 frames.
  - [ ] 2.6 `v4l2-compliance`: no failures. Logged. **Exit Phase 2.**
- [ ] **Phase 3 — Statistics to userspace**.
  - [ ] 3.1 uAPI statistics struct from the 0.4 layout (AE, AWB first; AF optional).
  - [ ] 3.2 Metadata capture node (`V4L2_BUF_TYPE_META_CAPTURE`), one buffer per frame from the statistics DMA.
  - [ ] 3.3 Board test: covering the lens lowers the AE statistics; a coloured light shifts the AWB sums.
  - [ ] 3.4 Board test: 30 statistics buffers/s at 1080p30, none dropped.
  - [ ] 3.5 `v4l2-compliance`: no failures. Logged. **Exit Phase 3.**
- [ ] **Phase 4 — libcamera auto exposure and white balance; Software ISP removed**.
  - [ ] 4.1 Owner decision: extend libcamera's simple pipeline or add a new pipeline handler (pattern: `rkisp1`).
  - [ ] 4.2 IPA AE: sensor exposure/gain from AE statistics.
  - [ ] 4.3 IPA AWB: white-balance gains through the parameters node.
  - [ ] 4.4 Buildroot: libcamera patches/package for the above.
  - [ ] 4.5 Remove `0001-pipeline-simple-enable-Software-ISP-for-sun6i-csi.patch`; `cam` still captures.
  - [ ] 4.6 Board test: exposure and white balance converge on their own (bright/dark scene, indoor/outdoor light). Logged. **Exit Phase 4: goal met.**

**Phase 5 — Hardware H.264 encoder.** Separate plan (`docs/common/VIDEO_ENCODING_VPU_GUIDE.md`); consumes Phase 1 NV12 by `dma-buf`.

