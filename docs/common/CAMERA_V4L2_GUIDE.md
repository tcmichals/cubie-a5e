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

This section is the detailed plan: rules, facts, exit criteria and the step checklist (§5.3, steps 0.1–4.6). TODO.md (Part I §2, "A733 Hardware ISP") holds only a summary made from it: Done / Now / Next / Not now, and one box per phase.

### 5.1 Rules

0. **No video processing on the CPU** (owner, 2026-11-11, DESIGN_DECISIONS.md): sensor → CSI → hardware ISP → NV12 dma-buf → hardware VE encoder; the CPU only queues buffers. RAW capture without the ISP is for bring-up only. Target CPU load during capture + encode: < 2 % of one core.
   - If RAW is ever recorded, it is DMA end to end (owner, 2026-11-11): CSI DMA writes the frame into a DRAM dma-buf; userspace passes that buffer to the storage driver (`O_DIRECT` / io_uring, no memcpy, no page cache); the storage controller's DMA (NVMe, or the SMHC IDMAC) reads it. The CPU only queues buffers and submits I/O. Bandwidth: IMX415 full 3864×2192 RAW10 packed at 30 fps ≈ 320 MB/s (16-bit ≈ 510 MB/s), 1080p RAW10 ≈ 78 MB/s: full-resolution RAW needs NVMe over PCIe (A7A PCIe signals exist in the schematic; no mainline A733 PCIe driver yet). ISP → H.264 needs only 2-6 MB/s.
1. Phases run in order. A phase starts only when the previous phase's exit criteria have passed **on the board** and the result is recorded in `docs/platforms/CUBIE_A7A_DEBUG_LOG.md`.
2. Hardware facts come from the vendor BSP (`linux-a733/bsp/drivers/vin/`, `bsp/configs/linux-5.15/sun60iw2p1.dtsi`) or silicon readbacks, cited by file:line. Not from this document alone.
3. Driver work happens in `linux-cubie` on the A7A branch, as a proper V4L2 driver (media controller, subdevs, `videobuf2-dma-contig`), so it can go upstream later. No vendor `vin` module port.
4. Every phase that adds a video or metadata node passes `v4l2-compliance` with no failures.
5. The Software ISP patch stays until Phase 4 passes; then it is removed.
6. **A7A (A733) first, then backport to the A5E (T527)** (owner, 2026-11-09). Both chips use the same ISP 600 block in the vendor build, so the driver is written once. Every difference found between the two chips goes into §6, with its source, at the time it is found, even during A7A-only phases.

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
| Mainline CSI ↔ ISP link | `sun6i-csi` already supports an ISP on a DT graph port (`SUN6I_CSI_PORT_ISP`, `sun6i_csi_isp_detect()`); the ISP then owns the media device. That is the V3s model (ISP with its own output DMA); the A733 ISP has none (ISP → VIPP → CSI DMA, §5.4 0.5/0.9), so this port model does not fit as is | `drivers/media/platform/sunxi/sun6i-csi/sun6i_csi.c:40-63,336-346` |
| Mainline reference | `drivers/staging/media/sunxi/sun6i-isp` (V3s ISP, ~2,400 lines): driver structure only, different hardware, no statistics node | `linux-cubie` |

### 5.3 Phases and steps

Tick steps here as they are done. This checklist is the source; the TODO.md entry is a summary made from it (**Done / Now / Next / Not now** plus one box per phase). Update that summary whenever camera work stops for something else, and tick a phase there only when its exit test passes on the board. Vendor paths are under `linux-a733/bsp/drivers/vin/`.

**Exit criteria per phase:**
- Phase 0: §5.4 (hardware map) written with file:line citations and reviewed by the owner.
- Phase 1: 1920×1080 NV12 at 30 fps, 300 frames, 0 drops; colours correct; CPU load compared with the Software ISP; `v4l2-compliance` passes.
- Phase 2: white-balance gains set from userspace change the image within 2 frames; `v4l2-compliance` passes.
- Phase 3: statistics follow the scene, one buffer per frame at 30 fps; `v4l2-compliance` passes.
- Phase 4: `cam` captures with exposure and white balance converging on their own, with the Software ISP patch removed.

- [ ] **Phase 0 — Hardware map (documentation only, no driver code)**. Output: new §5.4 in the camera guide, every fact cited by file:line.
  - [x] 0.1 Vendor call sequence: `vin.c` probe order, `vin-video/vin_core.c` stream on/off, `vin-isp/sunxi_isp.c` (`s_stream`, load/save buffer allocation, IRQ handler).
  - [x] 0.2 ISP 600 register map from `vin-isp/isp600/isp600_reg.h` and `isp600_reg_cfg.c`: enable/control, input and output format, interrupt enable/status bits, load/save/statistics address registers, version register.
  - [x] 0.3 Load buffer layout: `ISP_LOAD_REG_SIZE` (`0x1000`) register image plus table offsets; which `ISP_LOAD_DRAM_SIZE` (`0xAF40` or `0x7700`) applies to the A733 build and why.
  - [x] 0.4 Statistics buffer layout (AE, AWB, AF, histogram offsets inside `ISP_STAT_TOTAL_SIZE` `0x6a00`).
  - [x] 0.5 Frame routing: CSI_TOP (`0x05800800`, `top_reg.c`) registers that connect CSI parser → ISP → VIPP (`0x05910000`) → DMA (`0x05830000`); is VIPP required between ISP and DMA?
  - [x] 0.6 Clocks, resets, power domain for the ISP (vendor `vind` node `sun60iw2p1.dtsi:2563`); which already exist in our `ccu-sun60i-a733.c` and PCK600 driver, which are missing.
  - [x] 0.7 IOMMU: ISP is IOMMU master 2, CSI master 1 (`platform/sun60iw2_vin_cfg.h:52-53`). Find out how our CSI DMA works today without an IOMMU driver (mainline `sun50i-iommu.c` has no A733 support) and whether ISP DMA works the same way.
  - [x] 0.8 Interrupt `GIC_SPI 129`: which status bits fire per frame (load done, statistics ready, frame end) and what the vendor handler does for each.
  - [x] 0.9 Mainline reference: `sun6i-csi` ISP port (`SUN6I_CSI_PORT_ISP`, `sun6i_csi_isp_detect()`, async registration) and staging `sun6i-isp`: list what is reused and what is new.
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

**Phase 5 — Hardware H.264 encoder.** Separate plan (`docs/common/A733_VE_ENCODER_GUIDE.md`); consumes Phase 1 NV12 by `dma-buf`.

- [ ] **Phase 6 — Backport to the A5E (T527)**. Starts after Phase 4 passes on the A7A.
  - [ ] 6.1 Fill every "not checked" T527 cell in §6 from the vendor `linux-aw2501` tree (`bsp/drivers/vin/`, `bsp/configs/linux-5.15/sun55iw3p1.dtsi`) or silicon readbacks.
  - [ ] 6.2 Driver: add the T527 compatible and per-SoC data only for what §6 shows differs (IRQs, IOMMU master, CSI/MIPI count); no copied code paths.
  - [ ] 6.3 T527 DTS: ISP node and CSI → ISP graph link in the A523 dtsi / A5E board DTS.
  - [ ] 6.4 Bring up the existing CSI capture on the A5E first (sensor → CSI-2 → CSI → `/dev/video0` raw), then repeat the Phase 1–4 board tests on the A5E.
  - [ ] 6.5 Log in `docs/buildroot/DebugLog.md` (A5E); update §6. **Exit Phase 6.**

---

### 5.3a What the A733 camera hardware can do (datasheet, the reference)

Source: `vendor-a733-reference/A733_Datasheet_V0.93.txt` §3.3.1 ISP (line 1173), §3.7.1 MIPI CSI (line 1415), front page (lines 34-42). Owner, 2026-11-11: vendor code, vendor tests and forum reports are not trusted; the datasheet and User Manual set what to build and test against. Every report in §5.5 / `CAMERA_V4L2_GUIDE.md` is checked against these limits.

| Block | Capability |
| :--- | :--- |
| MIPI CSI | 3 ports, 4 + 4 + 2 lanes, CSI-2 v1.1, D-PHY v1.2, 2 Gbit/s per lane; crop, frame-rate down, sensor frame sync |
| Parallel CSI | 2 ports, 8/10/12/16-bit; up to 4×720p30 BT.656, 4×1080p30 BT.1120 |
| ISP input | up to 4 sensors; RAW8/10/12/14/16 |
| ISP max resolution | **online 3264×4224**, offline 5874×4224 (CSI section: 5664×4248 offline) |
| ISP throughput, one sensor | online: 8 MP@60 linear, 8 MP@30 2-frame WDR; offline: 24 MP@25 linear, 8 MP@30 2F-WDR |
| ISP output | up to 16 streams; YUV422/YUV420/RGB888/RGB565; mirror/flip; 1/16-1× downscaler; bounding boxes |
| ISP algorithms (hardware) | crop, 2F-WDR, DRC, local tone mapping, BLC, digital gain, WB, LSC, gamma, DPC, crosstalk, CAC, 2D/3D NR, demosaic, sharpness, 3D-LUT colour; AE/AWB/AF, anti-flicker and histogram **statistics** (the 3A loop itself is software) |
| DMA writers (BK) | 6: BK0/1 up to 20 MP@30 / 24 MP@25, BK2/3 up to 2.5K@60, BK4/5 up to 1080p30; BK0-3 4-lane time-division multiplexing (TDM) |
| CSI format conversion | YUV422→420, 422→400, 420→400; H/V flip for YUV and RAW |
| Encoder (VE) | H.265/H.264 up to 4K@30, MJPEG/JPEG up to 4K@15 |

What this means:
- **4K-wide (3840) capture is beyond online ISP mode** (max width 3264). Radxa PR #33 (ISP1 limited to 3264×2160) is a hardware limit, not a driver bug; 3840×2160 needs offline mode (frame through DRAM) or a sensor mode ≤ 3264 wide.
- **Wider than 3264 online:** the vendor splits one frame into left and right halves (with overlap) for ISP0 and ISP1 (`STITCH_2IN1`, `large_image == 3`: vendor `vin.c:1448-1453`, `vin-csi/sunxi_csi.c:451-460`, `vin-tdm/vin_tdm.c:387-394`, `vin-isp/sunxi_isp.c:1075-1084`). The IMX415 "2-frame stitch with one buffer" report is this split, not 2F-WDR/DOL (corrected 2026-11-11, triage doc). Whether the split is a documented hardware mode: not found.
- The **TDM** in the `tdm0 rx0 frame error` reports is the ISP input stage (`tdm0@5908000`, ISP IOMMU master 2), idle in online normal mode (§5.4 0.5; `vin-tdm/vin_tdm.c:1079-1097`); it is used for offline/WDR/stitch modes. (Corrected 2026-11-14; earlier text called it the BK0-3 time-division input stage.)
- Our target (ov5647, 2-lane, 1080p30 RAW10, ISP online) is well inside every limit.

### 5.4 Hardware map (Phase 0, in progress)

Steps 0.1–0.5 and 0.7–0.9 written 2026-11-14 from offline sources only (no board access). Vendor paths are relative to `oldkernels/linux-a733/bsp/drivers/vin/` unless given in full. "Inferred" = deduced from the code, not stated by it; "not found" = not in the vendor kernel source or the binaries checked. **P1** marks what Phase 1 (1080p30 NV12, fixed settings, ov5647 2-lane `SGBRG10`, ISP online) needs; "not P1" = later phases.

**Build facts used throughout.** The vendor image kernel is built with `CONFIG_ARCH_SUN60IW2=y`, `CONFIG_SUPPORT_ISP_TDM=y`, `CONFIG_VIN_IOMMU=y`, `CONFIG_D3D=y`, `CONFIG_D3D_PKG_MODE=y`, `CONFIG_WDR` not set, `CONFIG_ISP_SERVER_MELIS` not set, `CONFIG_ARCH_SUN55IW6` not set (`/boot/config-5.15.147-21-a733` in `radxa-r6/root.ext4`, lines 21, 303, 310-316, 319, 74). On that arch `platform/platform_cfg.h:58-72` defines `ISP_600`, `VIPP_200`, `TDM_V200`, `CSIC_DMA_VER_140_000`. The A733 userspace ISP library is **ISP 602**: `libisp.so` (`libawispapi-isp-602-arm64`) `isp_get_version` @ `0x21ba4` prints the constant `602` (`mov w2, #0x25a`). The T527 is 601 (§6): same register layout family, revision differs.

#### 0.1 Vendor call sequence (P1)

| Stage | What the vendor does | Source |
| :--- | :--- | :--- |
| Module init | registers drivers in this order: csi, tdm, isp, mipi, flash, scaler (VIPP), vin-core (DMA), then `vin` (media device) | `vin.c:2705-2760` |
| `vin_probe` | maps CSI_TOP (`reg[0]` `0x05800800`), CSI CCU (`reg[1]` `0x05800000`), combo-PHY top (`reg[2]`); registers v4l2 + media device; gets clocks; `pm_runtime_enable`; registers entities and default links | `vin.c:2449-2471`, `:2521-2546`, `:2556-2584` |
| `isp_probe` | reads `device_id`, `work_mode` (0 = online); **enables IOMMU translation for master 2** (`vin_iommu_en(ISP_IOMMU_MASTER, true)`); `of_iomap`, `request_irq(isp_isr, IRQF_SHARED)`; one DMA allocation (layout in 0.3); maps register pointers into MMIO and into the DRAM copy | `vin-isp/sunxi_isp.c:3287-3316`, `:3332-3334`, `:3345-3382`, `:2431-2446` |
| Virtual ISPs | `isp01..03` (`reg` `0x58ffffc`/`8`/`4`) are channels 1-3 of the same block: base + `id*4` gives `0x05900000` again, AHB block at `0x20 + id*0x30`. With ISP0 online they are empty (`is_empty`). `isp10/20/30` have no `reg` | `isp600/isp600_reg_cfg.c:108-128`, `sunxi_isp.c:3306-3314`, dtsi `sun60iw2p1.dtsi:2677-2721` |
| Power on (`vin_md_set_power`) | clocks on, 100 µs, CSI CCU gating (0.5), ISP bridge enable, parser clocks, `MISP0` clock, CSI_TOP enable, MBUS request max `0xf` | `vin.c:819-863`, `:703-721` |
| Open (`core.init` on the ISP) | read the version register (gated by `isp_ver_rd_en`); copy `isp_default_reg[]` (`0x1000` bytes) to the working load image; stats/save address = save buffer; load address = `load_para[0]`; save-load address; `para_ready = 0` | `sunxi_isp.c:1264-1310`; called from `vin-video/vin_video.c:2431` |
| `S_FMT` on the ISP sink | `__isp_try_format()`: bayer order from the mbus code (`SGBRG10` → `infmt` `ISP_GBRG` = 6, `RAW_10`); input = valid = sensor size, start 0; clamp 4224 (ISP0) / 3264 (`id == 1`) | `sunxi_isp.c:71-180` (entry `:97-102`), `:1060-1124`, `isp600/isp600_reg_cfg.h:339-344` |
| Stream on, order | pipeline `seq[2..4]`: TDM_RX, MIPI, **ISP**, SCALER (VIPP), CAPTURE (DMA), CSI parser, SENSOR. Routing (0.5) is written first | `vin.c:1388-1401`, `:1447-1459`, `:1482-1496` |
| ISP `s_stream(1)` | returns at once if the output is RAW (`:764-796`). Otherwise: reload defaults if the size changed; D3D ping-pong buffers (`CONFIG_D3D`); top level on (`isp_enable`, `isp_mode = work_mode`, `isp_top_cap_en`); clear all IRQ status; enable `PARA_LOAD` and `PARA_SAVE` + error IRQs; table-update flags from the image, plus `GAMMA` and `CEM`; write format/size/bit-depth into the image (`sunxi_isp_set_load_ddr`); copy the image into `load_para[0]` **and** `[1]`; `para_ready = 1`; `cap_en = 1` | `sunxi_isp.c:747-975` (ISP 600 branch `:946-975`), `:635-679`, `:681-716` |
| ISP `s_stream(0)` | `cap_en = 0`, IRQs off and cleared; (offline: wait for the core to go idle); top `isp_top_cap_en = 0`, `isp_enable = 0`; free D3D buffers | `sunxi_isp.c:1003-1029`, `:656-661` |
| IRQ | see 0.8 | `sunxi_isp.c:2456-3146` |
| Error recovery | `sunxi_isp_reset()`: stop parser, ISP bridge, ISP, VIPP channel, DMA; restart in reverse order; no register reload | `sunxi_isp.c:1558-1724` |

#### 0.2 ISP 600 register map (P1 unless marked)

One block at `0x05900000`, size `0x1300` (dtsi `:2668-2670`). Register offsets are the same in the MMIO space and in the DRAM load image (0.3). Layout `isp600/isp600_reg.h:26-35`: top `0x000`, AHB `0x020 + id*0x30` (`0x20` stride on T527 only, `:28-32`), load registers `0x100`, save `0x1000`, save-load `0x1200`.

**Written directly (MMIO):**

| Abs. offset (id 0) | Register | Fields used | Source |
| :--- | :--- | :--- | :--- |
| `0x000` | `ISP_TOP_CFG0` | b0 `isp_enable`, b1 `isp_mode` (0 online, 1 offline), b2 `isp_top_cap_en`, b3 `isp_ver_rd_en`, b4 `sram_clear`, b8 `int_mode`, b9/b10 clock back doors, b11/b12 auto-gating | `isp600_reg.h:38,89-105`; `isp600_reg_cfg.c:224-258` |
| `0x010` | `ISP_VER_CFG` | [11:0] small, [23:12] big; read with `isp_ver_rd_en = 1` | `isp600_reg.h:39,107-114`; `sunxi_isp.c:1265-1267`. **Silicon readback (2026-11-15): `0x00602100`** (Big `0x602` = ISP 602, small `0x100` = rev 1.0) |
| `0x014` | `ISP_MAX_SIZE` | [15:0] width, [31:16] height | `isp600_reg.h:40,116-122` |
| `0x018` | `ISP_MODULE_FET` | d3d/wdr/d2d/pltm feature bits | `isp600_reg.h:41,124-138` |
| `0x020` | `ISP_AHB_CFG0` | b0 `cap_en`, b1 `para_ready`, b8 rsc, b9 gamma, b10 drc, b13 d3d, b14 pltm, b15 cem, b16 msc, b17-b20 fe/nr msc `*_update`, b31 `dma_error_mode` | `isp600_reg.h:44,140-161`; `isp600_reg_cfg.h:114-126` |
| `0x024` | `ISP_LOAD_ADDR` | DRAM address `>> 2` of the load image | `isp600_reg.h:45`; `isp600_reg_cfg.c:303-306`; `isp600_reg_cfg.h:28` |
| `0x028` | `ISP_INT_BYPASS` = **IRQ enable** | bits in 0.8 | `isp600_reg.h:46,163-185`; `isp600_reg_cfg.c:308-316` |
| `0x02c` | `ISP_INT_STATUS` | bits in 0.8; cleared by writing the bit (write-1-to-clear, inferred from `isp600_reg_cfg.c:323-326`) | `isp600_reg.h:47,187-209` |
| `0x030` / `0x034` / `0x038` | internal status 0 / 1 / 2 | 0: config errors (width/height/hsync/vsync/btype, `:211-234`); 1: state machine `[15:0]`, current id `[17:16]`, LBC errors (`:239-261`); 2: FIFO flags (`:266-282`) (all in `isp600_reg_cfg.h`) | `isp600_reg.h:48-50` |
| `0x03c` | `ISP_SAVE_ADDR` | DRAM address `>> 2` of the save/statistics buffer. `bsp_isp_set_saved_addr()` and `bsp_isp_set_statistics_addr()` write **the same register** | `isp600_reg.h:51`; `isp600_reg_cfg.c:358-366` |
| `0x040` | `ISP_SAVE_LOAD_ADDR` | DRAM address `>> 2` of the save-load buffer (D3D/PLTM state). On T527 it is a load register at `0x114` instead | `isp600_reg.h:52-54,60-62`; `isp600_reg_cfg.c:126-128,368-371` |

**Load registers** (written into the DRAM image at `0x100 + off`, fetched by the hardware; `isp600_reg.h:57-84`):

| Abs. offset | Register | Fields / Phase 1 value | Source |
| :--- | :--- | :--- | :--- |
| `0x100` | `GLOBAL_CFG0` | [2:0] `input_fmt` (4 BGGR, 5 RGGB, **6 GBRG**, 7 GRBG), [8:4] `byr_act_bit`, [14:12] `byr_max_bit` (only 14/16-bit), [19:16] `rgb_cfg_bit`, b30 `save_reg_dbg_en`, b31 `dbg_out_en` | `isp600_reg.h:223-237`; `isp600_reg_cfg.c:383-397` |
| `0x104` | `GLOBAL_CFG1` | [2:0] `speed_mode` (vendor **3**), [6:4] `last_blank_cycle` (vendor **5**), [28:16] `line_int_num` | `isp600_reg.h:239-252`; `sunxi_isp.c:697-698` |
| `0x120` / `0x124` / `0x128` | input size / valid size / valid start | [13:0] width, [29:16] height; start [12:0] / [28:16] | `isp600_reg.h:262-290`; `isp600_reg_cfg.c:420-428` |
| `0x130` | `MODULE_BYPASS0` (enables) | b0 wdr, b1 wdr split, b8-10 ch dg, b12-14 ch msc | `isp600_reg.h:292-307` |
| `0x134` | `MODULE_BYPASS1` (enables) | b0 wdr_stitch … b5 **D3D**, b6 BLC, b7 WB, … b14 CCM, b17 gamma, b18 CEM, b24 AE, b25 AF, b26 AWB, b27 AFS, b28/29 hist | `isp600_reg.h:309-340`; `isp600_reg_cfg.h:132-156` |
| `0x138` | `MODULE_MODE0` | b20 `d3d_ref_frm_mode` (set with `CONFIG_D3D_PKG_MODE`) | `isp600_reg.h:342-349`; `sunxi_isp.c:688-690` |
| `0x140`-`0x14c` | D3D bayer / K0 / K1 / status addresses | `>> 2`; not P1 (D3D off) | `isp600_reg.h:70-73`; `sunxi_isp.c:477-487` |
| `0x170` | `VIN_CFG0` | [3:0] `input_cfg`, [11:8] `output_cfg`, [18:16]/[22:20]/[26:24] output channel data. **Meaning of the values: not found**; the vendor never writes it (default image `0`) | `isp600_reg.h:74,351-365` |
| `0x198` / `0x1b8` / `0x1d8` | ch0/1/2 `EXPAND_CFG0` | b0 blc_en, b1 inv_blc, [8:4] `input_bit` (P1: **10**), [20:16] `output_bit` (only set for 14/16-bit) | `isp600_reg.h:75-77,367-404`; `sunxi_isp.c:702-711` |
| `0x320`, `0x380`-`0x394` | D3D cfg / LBC cfg | not P1 | `isp600_reg.h:78-84` |
| save-load `+0x030` | `RANDOM_SEED` | [30:28] `input_fmt` copy (written together with `GLOBAL_CFG0`), b31 `d3d_k_state` | `isp600_reg.h:87,407-417`; `isp600_reg_cfg.c:200-203,383-387` |

Output format: the ISP has no output-format or output-DMA register of its own; the image goes out on the bridge to the VIPP (0.5). Which YUV layout the ISP→VIPP bus carries: not found (the VIPP is always told `vipp_in_fmt = 1`, `vin-vipp/vipp200/vipp200_reg.c:408-413`, i.e. YUV422 by `enum vipp_format`, `vipp200_reg.h:161-166`; inferred). Most module registers between `0x170` and `0x1000` (AE/AF/AWB windows at `0x650`/`0x670`/`0x6e0`/`0x710`, `isp600_reg_cfg.h:105-112`) have no names in the kernel; `libisp.so` `bsp_isp_map_load_addr` (@ `0x537f0`, 1988 bytes) maps about 260 offsets in `0x020`-`0xf38` without names — not P1.

#### 0.3 Load buffer layout (P1)

**Size on the A733: `ISP_LOAD_DRAM_SIZE` = `0x7700`.** `0xAF40` is only for `CONFIG_ARCH_SUN55IW6` (`isp600/isp600_reg_cfg.h:31-35`; YUV table `0x4F40` vs `0x1700`, `:40-44`); the vendor image has `CONFIG_ARCH_SUN60IW2=y` and `SUN55IW6` unset (config lines 21, 74). Confirmed by the userspace side: `libisp.so` `isp_set_load_reg` @ `0x58f88` puts `0x7700` in the size field of `VIDIOC_VIN_ISP_LOAD_REG` (`0xc0105706`, `sunxi_isp.c:1462-1463` for the number).

| Image offset | Size | Content | Source |
| :--- | :--- | :--- | :--- |
| `0x0000` | `0x1000` | register image (`ISP_LOAD_REG_SIZE`); same offsets as MMIO; AHB `0x20` word holds the table-update flags the driver reads back (`bsp_isp_load_update_flag`) | `isp600_reg_cfg.h:36`; `isp600_reg_cfg.c:163-195,374-381` |
| `0x1000` | `0x1800` | FE table (`ISP_FE_TBL_SIZE`); content not found | `isp600_reg_cfg.h:37` |
| `0x2800` | `0x2600` | bayer tables: RSC/LSC `0x2800` (`0x800`), MSC `0x3000` (`0x800`), NR-MSC `0x3800` (`0x3c8`); D3D-DK `0x800`, PLTM-TM `0x800`, PLTM-LM `0x200` sizes known, **offsets not found** | `isp600_reg_cfg.h:38,60-65`; offsets from `sunxi_isp.c:1340,1353-1357` |
| `0x4E00` | `0x1200` | RGB tables: DRC `0x200`, gamma `0x1000` (order not found) | `isp600_reg_cfg.h:39,68-69` (offset = sum, inferred) |
| `0x6000` | `0x1700` | YUV tables: CEM0 `0xcc0`, CEM1 `0xa40` (order not found) | `isp600_reg_cfg.h:43,72-73` (offset inferred) |
| `0x7700` | — | end (`0x1000+0x1800+0x2600+0x1200+0x1700`, debug table size 0) | `isp600_reg_cfg.h:34-45` |

Buffers (one `os_mem_alloc`, `sunxi_isp.c:2431-2446`): save/stats `0x6a00` | save-load `0x3400` | `isp_load` (working image, CPU only) `0x7700` | `load_para[0]` `0x7700` | `load_para[1]` `0x7700`; total `0x20300`. The hardware only reads `load_para[0/1]` (ping-pong, 0.8) and writes save / save-load.

Default image `isp_default_reg[]` (`vin-isp/isp_default_tbl.h:32`, ISP 600 part `:302-567`, one A733-only row `:325-326` at image `0x160`): `GLOBAL_CFG0 = 0x000c00c7` (GRBG, 12-bit), `GLOBAL_CFG1 = 0x53` (speed 3, blank 5), input/valid size `0x04380780` (1920×1080), `MODULE_BYPASS0 = 0`, **`MODULE_BYPASS1 = 0x80` (only WB on)**, `VIN_CFG0 = 0`, ch0 expand `0x000c00c0` (`:319-336`). The driver overwrites format, size and bit depth at stream-on (0.1). The tables after `0x1000` stay **zero** unless userspace (libisp) loads them (`load_shadow` is zeroed, `sunxi_isp.c:1271`), although the driver always sets the gamma and CEM update flags (`:963`): with `GAMMA_EN`/`CEM_EN` off in the default image this is harmless (inferred).

#### 0.4 Statistics (save) buffer layout (not P1; Phase 3)

The save buffer (`ISP_SAVE_DRAM_SIZE` = `ISP_STAT_TOTAL_SIZE` = `0x6a00`) is the statistics buffer: `0x100` of saved registers (`ISP_SAVE_REG_SIZE`) then `0x6900` of statistics (`isp600_reg_cfg.h:48-51`). Block sizes are in `isp600_reg_cfg.h:76-87`; packing them in header order after `0x100` ends exactly at `0x6a00` and puts HIST0 at `0x5a80`, which is the one offset the vendor states (`ISP_STAT_HIST0_MEM_OFS`, `:89`; used by `vin-rp/vin_rp.c:116-117`). The other offsets are inferred from that.

| Offset | Size | Block |
| :--- | :--- | :--- |
| `0x0000` | `0x100` | saved registers |
| `0x0100` | `0x360` | AE |
| `0x0460` | `0x020` | reserved (`ISP_STAT_RES_SIZE`) |
| `0x0480` / `0x1080` / `0x1c80` / `0x2880` / `0x3480` | `0xc00` each | AF IIR acc / FIR acc / IIR cnt / FIR cnt / HL cnt |
| `0x4080` | `0x1800` | AWB RGB |
| `0x5880` | `0x200` | AFS (anti-flicker) |
| `0x5a80` | `0x400` | HIST0 (stated) |
| `0x5e80` | `0x400` | HIST1 |
| `0x6280` | `0x780` | PLTM luminance |

Save-load buffer (`0x3400`): `0x100` registers + `0x3300` (PLTM PKX `0x3000`, D3D K `0x300`) (`isp600_reg_cfg.h:55-57,92-93`). Per-window format inside each block (AE windows, AWB zones, histogram bins): **not found** in the kernel; needs `libisp.so` (`__isp_handle_awb_stat` @ `0x4c1e0`, `__isp_handle_af_stat` @ `0x4d0b4`, `isp_handle_stats` @ `0x4f2a0`) in Phase 3. Statistics config windows in the load image: AE `0x650` (8 B), AF `0x670` (12 B), AWB `0x6e0` (8 B), HIST `0x710` (32 B) (`isp600_reg_cfg.h:105-112`). Buffer pipeline: the address is set at PARA_LOAD (`LOAD_SET`), the block is filled during the frame (`SAVE_SET` at the next PARA_SAVE) and is complete one PARA_SAVE later (`READY`) (`vin-stat/vin_h3a.c:386-478`).

#### 0.5 Frame routing and VIPP (P1)

Path in online mode: **MIPI → CSI parser → (TDM, idle) → ISP0 → VIPP0 → DMA0 (BK0)**. TDM, ISP and VIPP each have their own DT node; CSI_TOP selects the inputs.

| Register (CSI_TOP `0x05800800`) | Fields | Source |
| :--- | :--- | :--- |
| `+0x000` `CSIC_TOP_EN` | b0 top enable, b3 **ISP bridge enable**, b8 SRAM power-down, b12+n F2S bridge ch n, b16+n S2F bridge ch n, b20 bridge-ch disable (vendor clears it), b31 version read | `top_reg_i.h:27-43`; `top_reg.c:277-329` |
| `+0x030 + isp*0x10 + in*4` `ISPx_INy` | value = `parser*4 + parser_ch` (A733 table) | `top_reg_i.h:100-118`; `top_reg.c:25,75-107,337-342` |
| `+0x0A0 + vipp*4` `VIPPn_IN` | value = `isp*4 + isp_ch` | `top_reg_i.h:120-131`; `top_reg.c:111,138-154,344-349` |
| `+0x0F0` MBUS request max | [4:0] mcsi, [12:8] mcsi1, [20:16] misp; vendor `0xf` each | `top_reg_i.h:137-143`; `top_reg.c:383-393`; `vin.c:854` |
| `+0x1F0` / `+0x1F4` | feature list / version | `top_reg_i.h:134-146` |

CSI CCU (`0x05800000`): `+0x000` b31 clock-gating disable (A733 sets it, `vin.c:706-707`), b0/b1 mcsi/post clock mode; `+0x004` parser/combo/MIPI clocks; `+0x008` **b0 `MISP0_CLK_EN`, b4 `MISP0_BRIDGE_CLK_EN`**, b12/b16 F2S/S2F bridge ch clocks, b20 bridge gating disable; `+0x00c`/`+0x010` BK0-3 b0-3, **VIPP0-3 b8-11**, post b16 (`top_reg_i.h:420-486`, `top_reg.c:553-653`).

Facts:
- DMA input select is a no-op on the A733 (only `SUN50IW9` writes it, `top_reg.c:351-358`). DMA channel n takes VIPP n (inferred; vinc and scaler share `vipp_sel`, `vin-video/vin_video.c:6030`).
- **Our RAW path already goes through this route**: `sun55i_a523_csi.c:448-452` writes `ISP0_IN0 = 4` (parser 1 ch 0) and `VIPP0_IN = 0` (ISP0 ch 0) and `:467-471` sets TOP b0/b3/b12/b16 and MBUS `0x000f0f0f`, with the ISP and VIPP not programmed. The vendor RAW path also skips ISP and VIPP programming (`sunxi_isp.c:764-796`, `vin-vipp/sunxi_scaler.c:707-734`). So with ISP and VIPP off, RAW passes through both (silicon: Milestone 19; inferred mechanism).
- **TDM is not used in online normal mode**: `sunxi_tdm_subdev_s_stream()` returns before touching hardware when online, not WDR and no speed-down (`vin-tdm/vin_tdm.c:1079-1097`); speed-down is set only by a private ioctl (`vin-video/vin_video.c:3065,5124`). RAW also returns early (`vin_tdm.c:907-921`). Not P1.
- **Is VIPP required between ISP and DMA? Yes for YUV, by the vendor code**: every YUV stream programs VIPP (crop, scaler size, `sc_en = 1`, output format) and sets `para_ready` + channel capture (`sunxi_scaler.c:744-880`). For NV12: `sc_fmt = YUV420`, `out_fmt = YUV420`, no chroma down-sample because `MAX_OSD_NUM` is 0 on VIPP 200 (`sunxi_scaler.c:805-819`, `vipp200/vipp200_reg.h:28`). Whether YUV passes VIPP unprogrammed (like RAW) or through the channel `BYPASS_MODE` bit (`vipp200_reg_i.h:146`, never used by the vendor): **not found**, board test. VIPP has its own DRAM load buffer (`VIPP_REG_SIZE` `0x400`, load regs at `+0x200`, channel ctrl `+0x40` b0 cap_en / b2 para_ready / b4 bypass, load address `+0x50`; `vipp200_reg_i.h:26-36,138-174`, `vipp200_reg.c:213-273`) and uses IOMMU master 1 (dtsi `:2728`).
- DMA (BK) for NV12 progressive: `CFG` `+0x204` [19:16] = `FIELD_UV_CB_YUV420` (**5**; `6` = frame variant is only for interlaced/field sources), `FIELD_EITHER`; Y address `+0x220`, UV address `+0x228` (`CSI_BUF_1_A`), `>> 2`; `BUF_LEN` `+0x238` Y [13:0] = C [29:16] = width (`vin_video.c:5864-5911`, `dma140/dma140_reg.h:70-110,166-173`, `dma140/dma140_reg_i.h:122-172`, `dma140/dma140_reg.c:537-547`).

#### 0.6 Clocks, resets, power domain (2026-11-11)

Vendor DT `oldkernels/linux-a733/bsp/configs/linux-5.15/sun60iw2p1.dtsi:2563` (`vind0`): `csi_top = <600000000>`, `csi_isp = <540000000>`; clocks `CLK_CSI` (src `PLL_VIDEO0_4X`), `CLK_CSI_MASTER0..2` (src `sys24M` / `PLL_VIDEO0_3X`, sensor MCLK), `CLK_ISP` (src `PLL_VIDEO0_4X`), `CLK_BUS_CSI`, `CLK_CSI_MBUS`, `CLK_ISP_MBUS`; resets `RST_BUS_CSI` ("csi_ret") and an empty "isp_ret"; `power-domains = <&pd SUN60IW2_PCK_VI>`. ISP `isp00@5900000` SPI 129, `isp01@58ffffc` (`work_mode 0xff`), TDM (ISP input stage) `tdm0@5908000` SPI 134, all `iommus = <&mmu_aw 2 0>`.

Vendor CCU `bsp/drivers/clk/sunxi-ng/ccu-sun60iw2.c`:

| Clock | Register | Fields | Parents (mux index order) |
| :--- | :--- | :--- | :--- |
| `csi` | `0x1840` | M [4:0], mux [26:24], gate 31 | video2-4x, de-4x, peri0-480m, peri0-400m, peri0-600m, video0-4x, video1-4x (`:1763`) |
| `isp` | `0x1860` | M [4:0], mux [26:24], gate 31 | video2-4x, peri0-480m, peri0-400m, peri0-600m, video0-4x, video1-4x (`:1776`) |
| `csi-bus` | `0x1844` bit 0; reset bit 16 | | |
| `csi-mclk` / `isp-mclk` (MBUS) | `0x05E4` bit 8 / bit 9, keyed (`MBUS_GATE_KEY_VALUE`) | | (`:680-688`) |
| `csi-master0..2` | `0x1800` / `0x1804` / `0x1808` | MP + mux + gate | sys24M, video0-4x, video0-3x, video1-4x, video1-3x, video2-4x, video2-3x |

Ours (`linux-cubie` `ccu-sun60i-a733.c`): `csi-top` is a gate only (`:117`); the mux is a raw `writel((4 << 24), 0x1840)` = peri0-600m (`:449`); **the ISP clock is a raw `writel(BIT(31) | (3 << 24), 0x1860)` = peri0-600m, 600 MHz, always on (`:452`)**, not a clock. Missing: `CLK_ISP` (M + mux + gate), `CLK_ISP_MBUS` (bit 9), `CLK_CSI_MASTERx`. How the vendor sets these clocks (`oldkernels/linux-a733/bsp/drivers/vin/vin.c`): probe reparents both `csi_top` and `isp` to `PLL_VIDEO0_4X` (`:262`, `:275`); stream-on sets **the video PLL itself** to the DT rate when it is ≥ 300 MHz, then the CSI/ISP divider to 1 (`:335-392`). CSI and ISP share that PLL, so the last `clk_set_rate()` wins (base DT 600/540 → both end at 540; Radxa overlays 704/704 → whole camera block at 704 MHz; the reported 360/300 "fix" leaves the PLL at 300). The vendor CCU (`ccu-sun60iw2.c`) has the correct A733 map (PLL_PERI0 at `0x00A0`, `:99-117`). Mainline plan: CSI and ISP each from a fixed PERI0 output (600M/400M) with their own M divider, rates from the DT, read back on the board; video PLLs stay free for the display. Phase 1.2: add `CLK_ISP` and `CLK_ISP_MBUS` as real clocks, remove the raw writes, set the ISP rate from the DT (`assigned-clock-rates`, vendor 540 MHz; Radxa issue #39 reports frame errors at 704 MHz, fixed at 300 MHz, see §5.5), and confirm the `PD_VI` power domain covers the ISP.

Addition (2026-11-14): the ISP also needs the CSI-internal gates in the CSI CCU block (`0x05800000 + 0x008` b0 `MISP0_CLK_EN`, b4 `MISP0_BRIDGE_CLK_EN`; VIPP0 clock `+0x00c` b8), set by the vendor at power-on (`vin.c:846`, `top_reg.c:591-597,645-653`). Our `sun55i_a523_csi.c` runtime resume already sets them (`:220-262`). The empty `isp_ret` reset matches the code: `isp_probe` requests no reset (`sunxi_isp.c:3267-3431`).

#### 0.7 IOMMU (P1)

- Vendor: `vin_iommu_en(master, true)` → `sunxi_enable_device_iommu()` (`utility/vin_os.c:282-288`, only with `CONFIG_AW_IOMMU` + `CONFIG_VIN_IOMMU`, both `y` in the vendor config lines 178/310). It clears the master's bypass bit: register `0x03900000 + 0x400 + 0x200*master + 0x4` (`IOMMU_MIC_BYP_REG`), write 1 = bypass, 0 = translate (`bsp/drivers/iommu/sunxi-iommu-v2.c:96-102,482-499`; IOMMU node `iommu@3900000`, `allwinner,iommu-v20`, dtsi `:418-420`). Driver default mask `0x3ff` = all bypassed until a master is enabled (`sunxi-iommu-v2.c:181,667-672,1907`).
- Who enables what: **ISP master 2** at `isp_probe` (`sunxi_isp.c:3332-3334`) and `tdm_probe` (`vin-tdm/vin_tdm.c:2528`); **CSI master 1** at `vin_core` probe/open (`vin-video/vin_core.c:1679,1784,2239`). VIPP and DMA nodes are master 1, ISP and TDM nodes master 2 (dtsi `:2665-2720`, `:2728`, `:3055`).
- Why our CSI DMA works: silicon readback, master 1 `BYP` (`0x03900604`) = 1, bypass on (`logs/a7a-ve-enc-20261114/RESULTS.md:127-128`), so DMA uses physical addresses; `videobuf2-dma-contig` without an IOMMU gives exactly that.
- ISP: register `0x03900804` (master 2 `BYP`). **Silicon readback (2026-11-15): `0x00000001`** (bypass active, identical to master 1 CSI and master 3 VE_ENC; `0x03900800/08/0c/14` all read `0x1`). This confirms ISP DMA operates directly with physical memory (`videobuf2-dma-contig`) without requiring an IOMMU page table driver.

#### 0.8 Interrupt `GIC_SPI 129` (P1)

`isp00` only (`isp01..03` = SPI 130-132, dtsi `:2680-2698`). Bits are the same in `ISP_INT_BYPASS` (enable, `0x028`) and `ISP_INT_STATUS` (`0x02c`) (`isp600_reg_cfg.h:169-206`).

| Bit | Name | Enabled by vendor | Vendor handler action | Source |
| :--- | :--- | :--- | :--- | :--- |
| 0 | `FINISH` | no | handler commented out | `sunxi_isp.c:3133-3143` |
| 1 | `START` | no | commented out | same |
| 2 | **`PARA_SAVE`** (per frame: save/stats written, inferred) | yes | `isp_frame_number++`; frame-sync event + stats ISR for libisp | `:951`, `:3055-3072`, `:1927-1968` |
| 3 | **`PARA_LOAD`** (per frame: load image fetched, inferred) | yes | stats address for the next frame; copy libisp's shadow if new; re-apply format/size (`sunxi_isp_set_load_ddr`); D3D ping-pong; copy the working image into the **other** `load_para[]` and point `ISP_LOAD_ADDR` at it; `para_ready = 1` | `:951`, `:3074-3130`; `vin-stat/vin_h3a.c:409-433` |
| 4 | `N_LINE_START` | no | commented out | `:3137-3139` |
| 9 | `HB_SHORT` ("hblank need more than 128 cycles") | yes | log, `sunxi_isp_reset()` | `:2852-2856` |
| 10 | `CFG_ERROR` | yes | log internal status 0 cause (two-byte, fmt change, btype, dvld, hsync, vsync, width, height), reset | `:2863-2907` |
| 11 | `FRAME_LOST` | yes | log, reset (online) | `:2857-2862` |
| 16 | `INTER_FIFO_FULL` | yes | log status 2, reset | `:2908-2980` |
| 17 | `WDMA_FIFO_FULL` | yes | log status 0, reset | `:2981-3017` |
| 18 | `WDMA_OVER_BND` | yes | log, reset | `:3018-3022` |
| 19 | `RDMA_FIFO_FULL` | yes | log, reset | `:3023-3052` |
| 20 | `LBC_ERROR` | yes | log status 1, `d3d_rec_reset` | `:2837-2847` |
| 21 | `DDR_RW_ERROR` | yes | log only | `:2833-2836` |
| 22 | `AHB_MBUS_W` (AHB/MBUS write conflict) | yes | log only | `:2848-2851` |

Phase 1 minimum: enable `PARA_LOAD`, `PARA_SAVE` and the error bits; on `PARA_LOAD` set `para_ready` again (and swap the image if parameters changed, Phase 2); count frames on `PARA_SAVE`. Per-frame image completion is signalled by the CSI DMA interrupt (SPI 117), not by the ISP (inferred: the vendor ISP handler never touches video buffers).

#### 0.9 Mainline reuse

| Item | Reuse | New |
| :--- | :--- | :--- |
| `sun6i-csi` ISP port (`SUN6I_CSI_PORT_ISP` = 2, `sun6i_csi.h:24`) | `sun6i_csi_isp_detect()` (graph endpoint on port 2, `sun6i_csi.c:40-64`); with an ISP the CSI does not register its own v4l2/media device and binds to the ISP's v4l2 device on async bound (`sun6i_csi.c:370-393`, `sun6i_csi_bridge.c:695-703`) | That model is the V3s one: the ISP has its own capture DMA, and the CSI capture link is created disabled (`sun6i_csi_capture.c:1055-1060`). On the A733 the ISP has **no** output DMA: the CSI capture node (BK0 via VIPP0) must stay the video node, fed by the ISP source pad. Needed: link ISP source → CSI capture, CSI capture/DMA programming for NV12 (0.5), CSI_TOP routing per mode, VIPP programming. Requires changes in `sun6i-csi`, not only a new ISP driver |
| A733 CSI glue `sun55i_a523_csi.c` | CSI CCU gates incl. MISP0/VIPP0 clocks (`:220-262`), routing parser1→ISP0→VIPP0 (`:448-452`), TOP/bridge enable (`:467-471`) | NV12 DMA format, UV address, VIPP0 block (new `reg`), ISP enable sequencing |
| staging `sun6i-isp` (V3s, 3042 lines) | structure: proc subdev + params node + load/save buffers set up at start and re-armed on `PARA_LOAD` (`sun6i_isp.c:112-130,254-267`), uAPI pattern | Register map (V3s: FE regs at `0x0`, separate LUT/DRC/stats address registers `0x28`-`0x30`, `sun6i_isp_reg.h:19-68`; ISP 600: one contiguous `0x7700` image, AHB block at `0x20`), no capture DMA, statistics node (staging has none, `TODO.txt`), virtual channels |

#### 0.10 Options for the owner (not decided)

| Option | For | Against |
| :--- | :--- | :--- |
| A. New driver `drivers/media/platform/sunxi/sun60i-isp/` (compatible e.g. `allwinner,sun60i-a733-isp`, T527 `allwinner,sun55i-a523-isp`) | ISP 600 register layout, load image and DMA-less output differ completely from V3s; clean upstream review; no staging baggage | more new code; must still change `sun6i-csi` for the ISP → CSI-capture link |
| B. Extend staging `sun6i-isp` with an ISP 600 variant | reuses proc/params skeleton and async binding | almost no register code shared; staging driver is itself blocked on uAPI/stats (`TODO.txt`); its capture node model does not fit |
| C. ISP as a subdev inside `sun6i-csi` (A733 backend) | CSI already owns DMA, VIPP routing and the media device on A733 | mixes two hardware blocks/IRQs in one driver; harder to backport per-SoC data; unusual upstream |

Names: any option needs a binding name for the ISP node and a VIPP node (or VIPP inside the CSI binding). Owner decision.

#### Phase 1 minimal bring-up sequence (draft)

Assumes our RAW pipeline (Milestone 19) as the base; "done" = already in `linux-cubie`. Items marked "board check" are not proven by the source.

1. Power/clocks: `PD_VI` on; bus/MBUS/CSI clocks (done); ISP module clock + `CLK_ISP_MBUS` as real clocks, ~540 MHz (0.6; vendor DT `:2570`); CSI CCU `+0x008` b0/b4 and `+0x00c` b8 (done, `sun55i_a523_csi.c:220-262`).
2. IOMMU: confirm master 2 bypass (`0x03900804` = 1, board check, 0.7); no IOMMU programming.
3. Allocate (`dma_alloc_coherent`, below 4 GiB): save `0x6a00`, save-load `0x3400`, two load images `0x7700` each (`sunxi_isp.c:2431-2446`).
4. Build the load image: copy `isp_default_reg[]` (`0x1000`, `isp_default_tbl.h:302-567`), zero `0x1000`-`0x7700`; set `GLOBAL_CFG0.input_fmt = 6` (GBRG), `byr_max_bit` untouched; `RANDOM_SEED.input_fmt = 6` in save-load `+0x30`; input/valid size `0x04380780`, start 0; `GLOBAL_CFG1` speed 3, blank 5; ch0 `input_bit = 10`; clear `D3D_EN` (b5 of `0x134`) (`sunxi_isp.c:681-716,957-959`; `isp600_reg_cfg.c:383-447`). Copy it into both load buffers (`:969-971`).
5. Version: `TOP_CFG0.isp_ver_rd_en = 1`, read `0x010`, clear (`sunxi_isp.c:1265-1267`). Record the value.
6. Addresses (`>> 2`): `ISP_SAVE_ADDR 0x03c` = save, `ISP_LOAD_ADDR 0x024` = load[0], `ISP_SAVE_LOAD_ADDR 0x040` = save-load; `para_ready = 0` (`sunxi_isp.c:1293-1309`).
7. Routing (done for RAW): `ISP0_IN0 = 4`, `VIPP0_IN = 0`, TOP b0/b3/b12/b16, MBUS max (`sun55i_a523_csi.c:448-471`; vendor `vin.c:1456-1458`).
8. ISP top on: `TOP_CFG0` `isp_enable = 1`, `isp_mode = 0` (online), `isp_top_cap_en = 1` (`sunxi_isp.c:650-655`).
9. IRQ: clear `0x02c` (write `0xffffffff`), enable `PARA_LOAD | PARA_SAVE` + bits 9-11, 16-22 in `0x028` (`sunxi_isp.c:950-954`).
10. Table flags: `AHB_CFG0` update bits = flags from image `0x20` with D3D cleared `| GAMMA | CEM` (`sunxi_isp.c:956-964`; `isp600_reg_cfg.c:291-301`).
11. `AHB_CFG0.para_ready = 1`, then `cap_en = 1` (`sunxi_isp.c:973-974`).
12. VIPP0 (`0x05910000`, master 1): load buffer `0x400`; crop 0,0 1920×1080; scaler size 1920×1080, ratios for 1:1 (value: `__scaler_w_shift`/ratio code, not yet read); `sc_en = 1`; `sc_out_fmt = vipp_out_fmt = YUV420 (0)`, `vipp_in_fmt = 1`; top clock + cap enable; channel `para_ready`, `chn_cap_en` (`sunxi_scaler.c:631-685,744-880`; `vipp200_reg.c:264-418`). Or test whether VIPP passes YUV unprogrammed (0.5, board check).
13. DMA0 (done for RAW, change format): `CFG [19:16] = 5`, field either; `BUF_LEN` Y = C = 1920; H/V size; Y `+0x220`, UV `+0x228`; enable (`vin_video.c:5864-5911,6027-6119`).
14. Parser and sensor on (done), in the vendor order after ISP/VIPP/DMA (`vin.c:1395-1396`).
15. Per frame: ISP `PARA_LOAD` → `para_ready = 1` (and optional image swap); `PARA_SAVE` → count; DMA frame-done (SPI 117) → return the vb2 buffer (done).
16. Stop: `cap_en = 0`, IRQs off/clear, `isp_top_cap_en = 0`, `isp_enable = 0` (`sunxi_isp.c:1004-1006,657-660`), then VIPP/DMA/parser as today.

Open questions for the board:
- [x] **ISP version value**: Solved on silicon (2026-11-15): `0x00602100` (`ISP_VER_CFG` at `0x05900010` with `isp_ver_rd_en = 1`). Big version `0x602` (ISP 602), small revision `0x100`.
- [x] **Master 2 BYP**: Solved on silicon (2026-11-15): `0x03900804` = `0x00000001` (bypass active).
- [ ] Whether a frame comes out with zero tables (gamma/CEM disabled) and `VIN_CFG0 = 0`.
- [ ] VIPP needed or bypassable for YUV.
- [ ] Colour order check with `GBRG` (ov5647 at our mode).

### 5.5 Vendor and community reports (inputs for Phase 0, 2026-11-11)

Root-cause triage of each report (code / config / tuning / hardware limit / unknown, checked against the datasheet): [`CAMERA_V4L2_GUIDE.md`](CAMERA_V4L2_GUIDE.md).

Anecdotal; check each against the vendor source before relying on it.

- CSI/ISP clock: `tdm0 rx0 frame error! width is 0` on IMX219 (A7Z), reported fixed by lowering the DT CSI/ISP clocks 704/704 → 360/300 MHz: https://github.com/radxa-build/radxa-a733/issues/39 . Our `ccu-sun60i-a733.c` probe sets the ISP clock to 600 MHz: check the vendor rate in Phase 0.6.
- Colour depends on closed per-sensor tuning: IMX415 red/magenta cast, `libisp_ini.so` falls back to an ov13850 profile: https://github.com/radxa-build/radxa-a733/issues/19 . Radxa's userspace ISP package `allwinner-isp6xx` (tuning, e.g. GC2093): https://github.com/radxa-pkg/allwinner-isp6xx/pull/3
- IMX415 2-ISP split ("2-frame stitch", see 5.3a; not DOL) with one buffer makes continuous capture unusable: https://forum.radxa.com/t/cubie-a7z-camera-4k-imx415-continuous-capture-is-unusable-isp-runs-a-2-frame-dol-stitch-with-only-one-buffer/31224
- ISP1 frame size limit 3264×2160 (not 3840×2160): https://github.com/radxa/allwinner-bsp/pull/33
- A7A IMX214: wrong colours, `ISP_ERR` in `isp_dev_start` (TDM event subscribe failed): https://forum.radxa.com/t/radxa-cubie-a7a-radxa-13m-214-camera/29717 ; IMX415 `sensor_detect=0x0`: https://forum.radxa.com/t/cubie-a7a-radxa-camera-4k-imx415-detected-but-no-video-output-sensor-detect-0x0/30357
- VE (not camera, for the encoder work): vendor Cedar H.264 encoder IRQ never fires (`cedar_dev_ve2`, "wait interrupt overtime"), decode works: https://github.com/radxa-build/radxa-a733/issues/35
- Armbian A7Z community support (no camera found): https://github.com/armbian/build/pull/10036 ; forum: https://forum.armbian.com/topic/56130-radxa-cubie-a7aa7z-allwinner-a733/page/2/

### 5.6 Vendor sensor and ISP tuning data we can reuse (2026-11-14)

Found on the Radxa r6 image (`radxa-a733_bullseye_kde_r6`) and in the vendor BSP. Not yet extracted.

| Data | Where | Form | Use |
| :--- | :--- | :--- | :--- |
| Sensor I²C init tables (modes, timing) | `oldkernels/linux-a733/bsp/drivers/vin/modules/sensor/imx415_mipi.c` (1879 lines, ~986 `{reg, val}` entries), `imx219.c`, `imx214.c` | GPL source | Cross-check the mainline `imx415`/`imx219` drivers (both in `linux-cubie` `drivers/media/i2c/`) for the vendor-tested modes; no port. **No OV5647** in the vendor tree or image. |
| ISP tuning per sensor and mode | `libisp_ini.so` in `libawispapi-isp-602-arm64` (closed, arm64): exported data objects, e.g. `imx415_mipi_1080p_isp_tuning_settings` (0x18470 B), `_iso_settings` (0x36ac B), `_3a_settings` (0xaa4 B), `_test_settings` (0x7c B), `_cfg` (0x20 B); same set for `imx219_1080p`, `imx219_3280_2464`, `imx415_mipi_3840_2160`, `imx214_*` | binary blobs with symbol and size | Decode with the GPL struct definitions in `vin-isp/isp_tuning_priv.h` (`struct isp_param_config` at `:388`: `isp_test_param`, `isp_3a_param`, `isp_tunning_param`, `isp_dynamic_param`); check `sizeof` against the blob sizes first. Gives black level, WB, CCM, gamma, NR, sharpness, LSC tables for Phase 1-2 defaults. |
| Vendor ISP output as a quality reference | run `v4l2src ... en-awisp=1` on the vendor image with an IMX415/IMX219 module | NV12 frames + H.264 (`omxh264videoenc`, needs `io-mode=2`) | Same-scene comparison with our ISP. Needs a vendor-supported sensor (not OV5647). |
| ISP registers and load buffer while streaming | `/dev/mem` on the vendor image (`STRICT_DEVMEM` off), ISP `0x05900000`, only while streaming (`PD_VI` on) | snapshot | Programmed register image to compare with `isp_default_reg[]` and our Phase 1 values. |

**Size check 2026-11-14** (struct sizes from `isp_tuning_priv.h` compiled natively, x86-64, with or without `USE_ENCPP`; vs blob sizes from `objdump -T libisp_ini.so`):

| Struct | Header `sizeof` | Blob size | Result |
| :--- | :--- | :--- | :--- |
| `isp_test_param` | `0x7c` | `0x7c` | **matches**: decodable now |
| `isp_tunning_param` | `0x18438` | `0x18470` | blob 56 bytes larger |
| `isp_dynamic_param` (iso) | `0x3670` | `0x36ac` | blob 60 bytes larger |
| `isp_3a_param` | `0x1680` | `0xaa4` | blob much smaller: different layout |

So the BSP kernel header (ISP 600) is close to, but not the same as, the library's layout (ISP 602). Next: find the added fields (compare the T527 `isp-601` headers in tina-source `rtos-hal/.../vin_isp/isp_server/`, which are newer; use known table contents such as gamma ramps and LSC tables as anchors inside the blobs), then decode. The 3A struct needs the userspace header, not the kernel one. Blob offsets in `libisp_ini.so`: `imx415_mipi_1080p_isp_tuning_settings` `0x22108`, `_iso_settings` `0x3a578`, `_3a_settings` `0x3dc28`, `_test_settings` `0x3e6d0` (symbol values; map to file offsets through the section headers).

**Better source found 2026-11-14: no blob decoding needed.** Radxa publishes the userspace ISP source: https://github.com/radxa/allwinner-isp6xx (packaging: `radxa-pkg/allwinner-isp6xx`; cloned to `~/projects/cubie/downloads/allwinner-isp6xx/src`, commit `516fd74`, 2026-11-05, 169 MB; no licence file found in the repo: treat as reference only, do not copy code or tables into kernel or libcamera patches without the owner checking the licence). Contents of `isp6xx/`:
- **Per-sensor tuning as readable C initialisers** (`libisp/isp_cfg/SENSOR_H/*_a733.h`, generated by Allwinner's Hawkview tool, named fields): `imx415_mipi_1080p_a733.h` (8546 lines), `imx415_mipi_3840_2160_a733.h`, `imx219_mipi_1080p_a733.h`, `imx219_mipi_3280_2464_a733.h`, `imx214_*`, `imx386_*`, `ov13850_*`, `ov13b10_*`, `gc2093`, `gc05a2`, `gc030a`, `s5k5e8`; `_t527.h` variants for the A5E. **No OV5647.**
- **The matching struct definitions**: `libisp/isp_tuning/isp_tuning_priv.h` (419 lines; the kernel copy has 396, which is why the blob sizes above differ), `libisp/include/isp_ini_parse.h`.
- **Open C source** for the ISP server glue (29 `.c` files: `isp_manage`, `isp_dev`, `isp_events`, `isp_tuning`, `isp_cfg`, `isp_math`, `tuning_app`): how parameters become the load-buffer contents and how statistics are read. Direct input for Phase 0.3/0.4 open items and Phases 2-3.
- **Closed**: the 3A/algorithm core only (`libisp/out/isp602*/libisp_algo.{a,so}`). Our AE/AWB is libcamera's own, so this is not needed.

Steps (add to Phase 1/2 when started): read `isp_tuning.c`/`isp_manage` for the parameter → load-buffer mapping and update §5.4 0.3/0.4; take the IMX415/IMX219 `_a733.h` values as the reference tuning. Superseded for these sensors: extract the blobs with `objdump -T` offsets → `dd`; write a
decoder from `isp_tuning_priv.h`; dump to readable text per sensor; record in §5.4.

## 6. T527 (A5E) vs A733 (A7A) Camera Differences

Write-up of what differs between the two chips for the camera path, kept up to date as the work moves forward (rule §5.1.6). Each row cites its source; a T527 value is "not checked" until it is. Vendor trees: A733 `linux-a733/bsp/`, T527 `linux-aw2501/bsp/`.

| Item | A733 (Cubie A7A) | T527 (Cubie A5E) | Same? | Source |
| :--- | :--- | :--- | :--- | :--- |
| ISP block in vendor build | `isp600` | `isp600` | Same | vin `Makefile`: A733 `:66` (`SUN60IW2`), T527 `:60` (`SUN55IW3`) |
| ISP version (libisp) | **602** (`libawispapi-isp-602-arm64` `libisp.so` `isp_get_version` @ `0x21ba4`, constant `602`; §5.4 build facts). Hardware version register value still to read on the board (vendor `vin_v4l2.ko` prints `ISP_VERSION: ISP%x_%x`) | **601** (`ISP_SMALL_VERSION` 100, tag `isp-601-600-v1.00`, "AI985") | Close, **revision differs** | T527: tina-source `rtos-hal/hal/source/vin/vin_isp/isp_server/isp_version.h:38-40`; T527-only code under `#if defined CONFIG_ARCH_SUN55IW3`: `isp600/isp600_reg_cfg.h:313`, `isp_default_tbl.h:95`, `isp_server/isp_server.h:136`, `vin.c:308,777` (2026-11-11) |
| ISP registers | `0x05900000`, `0x1300` | `0x05900000`, `0x1300` | Same | dtsi `isp00`: A733 `sun60iw2p1.dtsi:2668`, T527 `sun55iw3p1.dtsi:4235` |
| ISP interrupt | `GIC_SPI 129` | `GIC_SPI 133` | **Differs** | same nodes |
| ISP IOMMU master | 2 | 0 | **Differs** | `platform/sun60iw2_vin_cfg.h:52`, `platform/sun55iw3_vin_cfg.h:54`; dtsi `iommus` |
| CSI IOMMU master | 1 | 1 | Same | `sun60iw2_vin_cfg.h:53`, `sun55iw3_vin_cfg.h:53` |
| CSI_TOP (`vind`) | `0x05800800`, `GIC_SPI 135` | `0x05800800`, `GIC_SPI 139` | Address same, **IRQ differs** | dtsi `vind0`: A733 `:2563`, T527 `:4106` |
| CSI0 parser | `0x05820000`, `GIC_SPI 125` | `0x05820000`, `GIC_SPI 130` | Address same, **IRQ differs** | dtsi `csi0`: A733 `:2600`, T527 `:4148` |
| MIPI0 | `0x05810100`, `GIC_SPI 133` | `0x05810100`, `GIC_SPI 137` | Address same, **IRQ differs** | dtsi `mipi0`: A733 `:2628`, T527 `:4179` |
| DMA writer `vinc00` | `0x05830000`, `GIC_SPI 117` | `0x05830000`, `GIC_SPI 122` | Address same, **IRQ differs** | dtsi `vinc00`: A733 `:3040`, T527 `:4636`; A733 value also in the A733 User Manual Table 12-1 (TODO camera audit) |
| VIPP0 (scaler) | `0x05910000` | `0x05910000` | Same | `sun60iw2_vin_cfg.h:27`, `sun55iw3_vin_cfg.h:28` |
| Number of CSI / MIPI receivers | 3 / 3 | 4 / 4 | **Differs** | `VIN_MAX_CSI`, `VIN_MAX_MIPI` in the two cfg headers |
| ISPs, DMA channels, scalers | 7 / 18 / 18 | 7 / 18 / 18 | Same | `VIN_MAX_ISP`, `VIN_MAX_DEV`, `VIN_MAX_SCALER` |
| ISP 600 register layout, load/statistics buffer sizes | Phase 0 | not checked | — | to fill in Phase 0 (A733) and 6.1 (T527) |
| Clocks, resets, power domain | Phase 0 | not checked | — | to fill in Phase 0 and 6.1 |
| Mainline CSI / MIPI drivers working | Yes (Milestone 19) | not checked | — | `CUBIE_A7A_DEBUG_LOG.md` Milestone 19 |

**Pattern so far:** the register map is the same; interrupt numbers move (T527 is 4–5 higher), the ISP's IOMMU master differs, and the T527 has one more CSI/MIPI receiver. The backport should mostly be per-SoC data, not new code; Phase 0 and 6.1 confirm or break that.

---

## 7. Driver Sources, Our Changes and Open Fixes (A733 first)

Where each piece of the camera path comes from, what we changed, and what still has to be fixed before it can go upstream. Kernel tree: `linux-cubie`, branch `a7a_remoteproc_camera_usb`. History of each fix: `docs/platforms/CUBIE_A7A_DEBUG_LOG.md` Milestones 16, 17, 19 and the Nov 1 handoff. The T527 column of §6 says which of these are expected to apply to the A5E.

| Piece | File(s) | Source | Our changes (commit) | Board status | Still to fix |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Sensor OV5647 | `drivers/media/i2c/ov5647.c` | Mainline | `ov5647_set_pad_fmt()` now writes `hblank` (`928a9c657b3f`, fixes 24 fps cap); 1080p default mode, VTS `0x487` (`eb680fc2b40b`) | 1080p30 verified | Send the `hblank` fix upstream on its own (real mainline bug). Changing the driver's default mode is board policy, not upstreamable. The driver insists on a 25 MHz clock but the module crystal is 24 MHz, so the reported pixel rate is 4% high; needs a proper fix (24 MHz support or a correct clock description) |
| MIPI D-PHY | `drivers/phy/allwinner/phy-sun55i-a523-mipi-dphy.c` | **New, written by us** (`bf888f8962eb`) | Programs combo-top `0x05810000`: LDO, VREF, termination calibration | Works (dphy1) | DT binding; upstream review; not tried on the T527 |
| MIPI CSI-2 receiver | `drivers/media/platform/sunxi/sun6i-mipi-csi2/` | Mainline + A523/A733 support added by us (`bf888f8962eb`, +282 lines) | OUT_NUM only for >2 lanes (`928a9c657b3f`); format defaults (`eb680fc2b40b`) | Works (mipi1, 2 lanes) | Split into upstreamable patches; binding compatible |
| CSI controller / DMA | `drivers/media/platform/sunxi/sun6i-csi/sun55i_a523_csi.c` (new, 471 lines) + `sun6i_csi.c` | **New file by us** on the mainline `sun6i-csi` driver (`bf888f8962eb`) | Ungates CSI-internal clocks at `0x05800000`, CSI_TOP enable, parser 1 routing, parser reset toggle (`928a9c657b3f`) | Works | Parser 1 routing is fixed in code; must come from the DT graph. Review against the vendor `top_reg.c` in ISP Phase 0.5 |
| CSI bridge | `sun6i_csi_bridge.c` | Mainline | Default format at setup, SINK/SOURCE pads mirrored, NULL-state guard (`bf888f8962eb`, `928a9c657b3f`) | Works | The NULL-state guard hides a caller passing no state; find the real cause before upstream |
| Capture node | `sun6i_csi_capture.c` | Mainline | Default 1920×1080 SGBRG10 (`eb680fc2b40b`) | Works | Default-format change is board policy; drop for upstream |
| Clocks | `drivers/clk/sunxi-ng/ccu-sun60i-a733.c` | Our A733 CCU driver | CSI clocks added (`bf888f8962eb`); CSI_TOP `0x1840` and ISP `0x1860` set to 600 MHz in code (`928a9c657b3f`); blind CSI gate unmask removed (`ce747fed491f`) | Works | 600 MHz should be set from DT (`assigned-clock-rates`), not hard-coded |
| Power domain | DTS `power-domains = <&pck600 PD_VI>` on dphy1/mipi1/csi | Our PCK600 driver | Added after genpd powered VI off (Milestone 17/18 log) | Works | — |
| Device tree | `sun60i-a733-cubie-a7a.dts`; overlay `project-cubie-a5e/dts-overlay/allwinner/cubie-a7a-ov5647.dtso` | Ours | i2c3 + pins (`9d77ea5d3106`); dphy1, mipi1, csi nodes (`bf888f8962eb`); overlay with 25 MHz `camera-clk` | Works | Bindings for the new compatibles; clock value (see OV5647 row) |
| ISP | — | **None yet**; vendor reference `vin-isp/isp600/` | — | — | §5 plan (Phases 0–4) |
| IOMMU | — | None; mainline `sun50i-iommu.c` has no A733 support | — | CSI DMA works without it | ISP Phase 0.7 |
| Userspace | libcamera simple pipeline + `0001-pipeline-simple-enable-Software-ISP-for-sun6i-csi.patch`; `/usr/bin/a7a-cam-capture` | libcamera (Buildroot) + our patch and tool | Software ISP enabled for `sun6i-csi` | Works | Replace the Software ISP (§5 Phase 4) |
| H.264 encoder | — | None yet | — | — | §5 Phase 5 |

**Branch state:** `928a9c657b3f` is a WIP commit that also contains a watchdog change (`sunxi_wdt.c`); split it before any of this is prepared for upstream. Commits carry no `Signed-off-by` (the owner adds his own).

---

## 8. Camera module choice (advice 2026-11-14, not verified on hardware)

- Recording/FPV: **IMX415** (Radxa Camera 4K): mainline `imx415`, vendor driver, and readable A733
  tuning (`imx415_mipi_1080p_a733.h`, `_3840_2160_a733.h`); overlay `cubie-a7a-imx415.dtso` exists.
  Rolling shutter: soft-mount, short exposure.
- Vision/NPU: global shutter, **IMX296** (colour, mainline `imx296`) or **OV9281** (mono, mainline
  `ov9282`). No vendor tuning. Connector/pinout for Radxa's 31-pin port not verified.
- OV5647: works with the ISP (raw Bayer in) and is fine for ISP Phase 1; no vendor tuning or
  reference, so tuning is ours.
- A7Z v2.00 has two 4-lane CSI ports: one of each is possible.


## 9. ISP tuning procedure (to offer openly)

Per sensor + lens; output is a libcamera tuning file (YAML) plus a script that regenerates it.
| Step | Capture (owner) | Computed |
| :--- | :--- | :--- |
| Black level | raw frames, lens covered | offset |
| Lens shading | flat evenly lit white surface | correction tables |
| White balance | grey card under daylight / indoor / shade | gain vs colour temperature |
| Colour matrix | 24-patch colour chart under 2-3 lights | matrices, colour error |
| Noise / sharpness / gamma | bright and dim scenes | strengths per gain |
Needs ISP Phases 1-3 first. IMX415/IMX219: vendor values available as a starting point (camera
guide §5.6). Needed hardware: a ColorChecker-type chart.


---

## Appendix A. Vendor and community issue triage


Date: 2026-11-11. Scope: Radxa A733 boards (Cubie A7A / A7S / A7Z), vendor 5.15 / 6.6 BSP images.
Research only: no board was touched for this note.

**Trust levels** (owner, 2026-11-11, see `CAMERA_V4L2_GUIDE.md` §5.3a):

- **Reference:** A733 Datasheet V0.93 (`vendor-a733-reference/A733_Datasheet_V0.93.txt`, §3.3.1 ISP line 1173,
  §3.5 VE line 1336, §3.7.1 MIPI CSI line 1415) and the A733 User Manual V1.00
  (`tina-source/manuals-text/A733_User_Manual_V1.00.txt`, "UM" below).
- **Read as evidence of what the vendor code does, not of what the hardware needs:** vendor BSP source
  (`old-kernels/linux-a733/bsp`, `radxa/allwinner-bsp` branch `cubie-aiot-v1.4.6`, plus newer branches through the GitHub API).
- **Unverified:** every claim that something works or fails in a vendor, Radxa or forum test. These are
  marked *(unverified)* below. None were reproduced here.

Categories: CODE (driver, kernel or closed userspace bug), CONFIG (DT/overlay values, clock rates),
TUNING/DATA (missing ISP sensor profile), HARDWARE/WIRING, **HARDWARE LIMIT** (the request is beyond what the
datasheet allows), UNKNOWN.

### Summary table

| # | Report | Symptom | Category | vs datasheet | Fixed? | Mainline takeaway |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | [radxa-a733#39](https://github.com/radxa-build/radxa-a733/issues/39) IMX219, A7Z | `tdm0 rx0 frame error! width is 0`, 0-byte capture | **CONFIG** (overlay `csi_top`/`csi_isp` = 704 MHz). It may also be a vendor-code clock-tree conflict (hypothesis) | UM §16.1.6.3 sets only a **minimum** CSI_CLK = lane rate / 8 (1-pixel mode): at most 250 MHz at the 2 Gbit/s datasheet maximum, so 300/360 MHz is enough for any lane rate. No **maximum** CSI/ISP clock found in the datasheet or UM. 704 MHz is not an integer division of any default parent in UM §16.1.3 (1188/1044/600/480/400 MHz) | No. Issue open, no reply. All A733 overlays in `radxa-pkg/radxa-overlays` still have 704/704 | Use a fixed peri0 parent, not a retuned video PLL. Start at the vendor base-DT rates (600 / 540 MHz) or lower and check them against silicon readback |
| 2 | [radxa-a733#19](https://github.com/radxa-build/radxa-a733/issues/19) IMX415, A7S | Red/magenta cast; libisp falls back to the `ov13850` profile | **TUNING/DATA** | n/a. Colour tuning is closed userspace data, not a hardware limit | Partly. `imx415_mipi_{1080p,3840_2160}_a733.h` are in `radxa-pkg/allwinner-isp6xx` from release v1.4.10 (2026-05-14), retuned in PR #1 (2026-06-23). The reporter's later "colours correct after cedarc update" is *(unverified)*. Issue still open | Mainline (libcamera) needs its own IMX415/ov5647 tuning. The vendor `.h` tables are the only starting data |
| 3 | [forum 31224](https://forum.radxa.com/t/cubie-a7z-camera-4k-imx415-continuous-capture-is-unusable-isp-runs-a-2-frame-dol-stitch-with-only-one-buffer/31224) IMX415, A7Z | Split frame, magenta fringes, `STITCH_2IN1`, `buffer1 = (nil)`. 1080p fixed by the correct node/`en-largemode` pairing. 4K still broken, about 2.5 fps | 1080p: **user error** (wrong node/mode pairing). 4K: **HARDWARE LIMIT** plus **CODE** (vendor 2-in-1 path / libisp) | 3840 > **3264 online max width** (DS §3.3.1, §3.7.1). 4K needs offline mode (DS: 5874×4224, 24 MP@25 linear) or the vendor "2-in-1" split over two ISPs. "STITCH_2IN1" is the **left/right split across ISP0+ISP1, not DOL/2F-WDR** (see notes) | No | Cap the online path at 3264 wide. 4K = offline (DRAM) mode or a 2-ISP split, a later phase. ov5647 1080p is well inside the online limits |
| 4 | [allwinner-bsp PR #33](https://github.com/radxa/allwinner-bsp/pull/33) | `ENUM_FRAMESIZES` advertises 3840×2160 on ISP1, `S_FMT` clamps it to 3264 | **HARDWARE LIMIT** (3264) plus a **CODE** inconsistency (enum vs try_fmt) | 3264 width on ISP1 matches DS online 3264×4224. The PR raises the ISP1 height clamp 3264 → 4224, which matches DS. The vendor ISP0 normal-mode clamp is **4224 wide** (`sunxi_isp.c:1097`), **above** the DS online width: see notes | Merged 2026-10-29 into `cubie-aiot-v1.4.8` only. `cubie-aiot-v1.5.0` still has the old clamps | `enum_framesize` and `try_fmt` must use one limit table taken from the datasheet: online 3264×4224 |
| 5 | [forum 29717](https://forum.radxa.com/t/radxa-cubie-a7a-radxa-13m-214-camera/29717) IMX214, A7A/A7Z | Near-B/W NV12, vertical cut, `unable to subscribe to tdm event: Inappropriate ioctl (25)` | **HARDWARE LIMIT** (4208 wide) plus **CODE** (TDM subdev has no event support; 2-in-1 sync fails) plus **TUNING** (default profile) | 4208 > 3264 online width, so the vendor uses 2-in-1 (log `STITCH_2IN1_LINNER`, two ISPs). 13 MP is within DS offline 24 MP@25 | No. Last reply (2026-07-13) only points to the 1080p command | The ENOTTY is real in the vendor code: `sunxi_tdm_subdev_ops` has no `.core` / `subscribe_event` (`vin_tdm.c:1436-1439`). A mainline design should not depend on TDM events |
| 6 | [forum 30357](https://forum.radxa.com/t/cubie-a7a-radxa-camera-4k-imx415-detected-but-no-video-output-sensor-detect-0x0/30357) IMX415, A7A | `read retry 3`, `sensor_detect read value is 0x0`, STREAMON EPERM | **UNKNOWN**, most likely **HARDWARE/WIRING** (no chip ID over I²C) or a connector/overlay mismatch | n/a (I²C/power, not an ISP limit) | No. The reporter never answered after the Radxa reply | Probe must fail on a chip-ID mismatch. The vendor goes on and registers the entity (topology shown) |
| 7 | [forum 30626](https://forum.radxa.com/t/radxa-camera-8m-219-not-working/30626) IMX219, A7Z | r4 image has an empty `/boot/dtbo`; a hand-built overlay detects the sensor but capture fails; a 2nd user reports "ISP error" | **UNKNOWN** (no logs). Possibly the same as #1 (704 MHz in the same overlay) | n/a | Radxa reply 2026-10-28: "use the latest release" *(unverified)* | None beyond #1 |
| 8 | [radxa-a733#35](https://github.com/radxa-build/radxa-a733/issues/35) VE H.264 encoder, A7Z | `h264 encoder wait interrupt overtime`; `cedar_dev_ve2` (hwirq 48) count stays 0; decode IRQ 49 works | **UNKNOWN**. DT IRQ/address/power-domain wiring matches the UM (not a DT IRQ-number bug). Encoder never asserts its line: start, reset, power or userspace cause not found | 1080p30 H.264 is well inside DS §3.5.2 (H.264 up to 4K@30) | No. Encode reported working on A7S *(unverified)*, failing on A7Z (2 reports) and on one board of unknown model | Encoder = separate block: 0x01C10000 (4 KiB), SPI 48, own clocks, reset and PD (see notes) |

### Correction to `CAMERA_V4L2_GUIDE.md` §5.3a

§5.3a says: "2F-WDR / DOL needs two exposures per frame; the IMX415 '2-frame stitch with one buffer'
report is a configuration of that mode". The vendor source does not support this. `STITCH_2IN1` is the
`large_image == 3` path, which splits one frame into a left half and a right half (each with an overlap)
and sends them to ISP0 and ISP1:

- `bsp/drivers/vin/vin.c:1448-1453`: "parser0 ch0--isp0 ch0, parser0 ch1-- isp1 ch0"
- `vin-csi/sunxi_csi.c:451-460`: parser output `hor_len = mf->width/2 + overlayer`, `ch_total_num = 2`
- `vin-tdm/vin_tdm.c:387-394`: each TDM rx gets `width / 2 + overlayer`
- `vin-isp/sunxi_isp.c:1075-1084`: ISP0 and ISP1 each get `mf->width / 2 + overlayer`
- `vin-isp/sunxi_isp.c:3080-3084`: ISP0 copies the shared load table to ISP1 ("2in1")

The IMX415 driver runs in linear mode (forum post, `0x302C = 0x00`, which agrees). The symptoms reported
(a hard split down the centre, each half processed differently, `buffer1 = (nil)`) fit a 2-ISP split whose
second half never syncs. They do not fit a DOL merge. The vendor uses this path to go past the
3264-pixel online width. Suggested correction for §5.3a: "the IMX415/IMX214 `STITCH_2IN1` reports are the vendor 2-ISP
left/right split used above the 3264 online width, not 2F-WDR". (Not edited here: the guide is outside this task.)

### Per-issue notes

#### 1. radxa-a733#39: IMX219 `tdm0 rx0 frame error`, CSI/ISP clock 704 → 360/300 MHz

- **Report** (jupiter9595, 2026-10-08, A7Z, Debian 13, kernel `6.6.98-5-aw2511`): the sensor streams
  (`sensor_s_stream on = 1, 1920*1080`). TDM reports `width is 0, height is 17` and resets, and ISP frame count is 0.
  Changing only the overlay `csi_top`/`csi_isp` 704/704 → 360/300 MHz gives 150 good 1080p frames
  *(unverified)*. The reporter says the 360/300 values come from the working Android DT *(unverified: no
  Android DT available here)*. No replies, no linked commit.
- **Overlay values, checked:** `radxa-pkg/radxa-overlays`
  `arch/arm64/boot/dts/allwinner/overlays/cubie-a7a-radxa-camera-8m-219.dtso:19-20` = `704000000`/`704000000`.
  The same in `cubie-a7a-radxa-camera-4k-415`, `-13m-214`, `cubie-a7z-...-cam2` and `cubie-a7a-radxa-hdmi-in-4k-rk628`.
  The r6 image overlays have the same values (`vendor-a733-reference/dtbo/cubie-a7a-radxa-camera-8m-219.dtbo.disabled.dts:17-18`, `0x29f63000` = 704 MHz).
  For comparison, the A5E overlay `cubie-a5e-radxa-camera-8m-219.dtso:19-20` uses 500/300 MHz.
  Base DT `bsp/configs/linux-5.15/sun60iw2p1.dtsi:2569-2570`: `csi_top = 600000000`, `csi_isp = 540000000`
  (the r6 A7S/A7Z dtbs agree: `0x23c34600`/`0x202fbf00`).
- **How the vendor applies the rate** (`bsp/drivers/vin/vin.c:333-389`): for rate ≥ 300 MHz it first sets the
  source clock (`csi_top_src`, `csi_isp_src`, both `CLK_PLL_VIDEO0_4X` in `sun60iw2p1.dtsi:2575-2579`) to the
  rate itself, then the module clock. In the vendor CCU (`ccu-sun60iw2.c:1763-1782`) both `csi` and `isp`
  are `CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT`. **Hypothesis, not verified:** both clocks retune
  the shared video0 PLL, so the rate each one ends up with depends on order, and 704 MHz retunes a PLL that
  other blocks also use (`pll-video0-4x` is a parent of DE0, DI, G2D and eink, `ccu-sun60iw2.c:880-928`). The
  reporter checked the 704 MHz rates at runtime but did not report the rates after the fix.
- **vs datasheet/UM:** UM §16.1.6.3: minimum `CSI_CLK = lanespeed/8` (1-pixel mode), `/16` in 2-pixel mode
  (4-lane only). At the DS maximum of 2 Gbit/s/lane that is 250 MHz, so 300 MHz meets the minimum for any
  lane rate. A **maximum** CSI or ISP clock: not found in the DS or UM. UM §16.1.3 default parents:
  VIDEO0/1/2PLL4X 1188 MHz, DEPLL4X 1044, PERI0 600/480/400 MHz. ISP_CLK_REG `0x1860` mux: video2-4x,
  peri0-480m, peri0-400m, peri0-600m, video0-4x, video1-4x (UM 4.1.6.246).
- **Category:** CONFIG (the value that changed was a DT value). The mechanism (too fast for the
  TDM/ISP, or a PLL conflict) is not proven.
- **Mainline:** our `ccu-sun60i-a733.c` writes ISP = peri0-600m (600 MHz). Model `CLK_CSI`/`CLK_ISP` as real
  M+mux+gate clocks. Assign a **fixed peri0 parent** (no `CLK_SET_RATE_PARENT` onto a shared video PLL).
  Take the rate from `assigned-clock-rates`. Test on silicon at a rate between the UM minimum and the vendor
  540 MHz. Read `0x1840`/`0x1860` back on the board.

#### 2. radxa-a733#19: IMX415 colour cast, `libisp_ini.so` missing the profile

- **Report** (developakr, 2026-04-30, A7S, `5.15.147-18-a733`, libawispapi ISP602 1.0.1): log
  `cannot find imx415_mipi_1920_1080_30_0_0 isp config, use ov13850_mipi_4224_3136_30_0_0`.
  `strings libisp_ini.so | grep imx415` is empty. Later comment: colours correct on the r6 image after
  installing libcedarc-dev 1.0.7 *(unverified; libcedarc is the VE library, not the ISP library, so how it
  could fix colour is not explained)*. Second user (krumpex): HW encode fails with `wait interrupt overtime`.
- **Source check:** `radxa-pkg/allwinner-isp6xx` v1.4.9 tree (commit `75f9be6a09`, 2026-04-01) has only
  `imx415_mipi_default_ini_capture_a733.h`. `imx415_mipi_1080p_a733.h` and `imx415_mipi_3840_2160_a733.h`
  first appear in the v1.4.10 release (`69617beca6`, 2026-05-14), are included in `isp_ini_parse.c:61-62`, and were
  retuned in PR #1 (2026-06-23). Forum thread 31224 (July) then logs `find imx415_mipi_1920_1080_30_0
  [imx415_mipi_1080p_isp_cfg]`, so the profile is found by then *(that log is unverified)*.
- **Category:** TUNING/DATA. **Fixed:** profile added upstream in the package (v1.4.10). The issue is still open.
- **Mainline:** colour depends on a per-sensor tuning file (libcamera IPA). The vendor headers are tables to start from, not a reference.

#### 3. Forum 31224: IMX415 continuous capture, `STITCH_2IN1` with one buffer

- **Report** (Imran_ahmed, 2026-07-23, A7Z, `5.15.147-21-a733`, libisp `665dc2e9` 2024-12-18): split frame,
  magenta fringes, sometimes monochrome. Log: `STITCH_2IN1_LINNER`, `ISP Set Sync Mode = 0x3`,
  `can not use sync mode.(buffer0 = ..., buffer1 = (nil))`.
- **Replies:** avaf: `/dev/video0` + `en-largemode=0` is for 1080p, `/dev/video1` + `en-largemode=1` for
  3840×2160 *(works on avaf's A7S, unverified)*. Follow-up by the reporter: 1080p correctly paired → no stitch,
  only isp0 opens. A faint seam remains at the exact centre, with the left half slightly darker
  *(unverified; that a 1080p frame shows a centre seam is unexplained)*. 4K correctly paired → both ISPs open,
  `buffer1 = (nil)` again, 90 frames in 36 s (about 2.5 fps).
- **Reporter's DOL theory is not supported:** see "Correction" above. `STITCH_2IN1` is the 2-ISP left/right split.
- **vs datasheet:** 3840 > 3264 online max width (DS §3.3.1, §3.7.1). DS offline: 5874×4224 (ISP) /
  5664×4248 (CSI), 24 MP@25 linear, 8 MP@30 2F-WDR. The DS does not mention the 2-ISP split; whether it is a
  supported hardware mode: not found.
- **Category:** 1080p symptom = user error (fixed by correct use). 4K = HARDWARE LIMIT for the online path
  plus CODE (vendor 2-in-1 sync in closed libisp, with the second buffer never delivered).
- **Mainline:** online pipeline limited to ≤ 3264 wide. 4K = offline mode (frame through DRAM), designed as
  its own phase. Do not copy the 2-in-1 path. Also from this thread: RAW is not reachable because every video node is
  routed through the ISP (`sunxi_isp.0` self-link `[ENABLED,IMMUTABLE]`). A mainline design should keep a
  CSI-only RAW capture node.

#### 4. allwinner-bsp PR #33: ISP1 frame size limits

- **Change** (wangyongsen11, merged by Radxa 2026-10-29 into `cubie-aiot-v1.4.8`):
  `__isp_try_format()` ISP1 normal-mode height clamp 3264 → 4224 on `CONFIG_ARCH_SUN60IW2`. `vidioc_enum_framesizes()`
  clamps max width to 3264 (ISP1) / 4224 (ISP0) and height to 4224 when not in large-image mode.
- **Review:** RadxaYuntian: "current code does not match the spec sheet". Author: the spec's 3264×4224 and
  5874×4224 are online/offline, not ISP1/ISP0. The PR only makes enum match the existing try_format clamp.
- **vs datasheet:** ISP1 3264 width = DS online max, so the 3840 → 3264 clamp is a **HARDWARE LIMIT**, not
  a bug. Height 4224 = DS. **Discrepancy:** the vendor ISP0 normal-mode width clamp is 4224
  (`bsp/drivers/vin/vin-isp/sunxi_isp.c:1097-1098`, the same on 1.4.8 and 1.5.0), which is above the DS online width
  of 3264. Whether ISP0 can really go past 3264 online: not found in the DS or UM. Check on silicon before relying on it.
  (Radxa docs suggest IMX219 3280×2464 on video0 `en-largemode=0`: 3280 > 3264 *(unverified)*.)
- **Where fixed:** `cubie-aiot-v1.4.8` only. `cubie-aiot-v1.5.0` still has the old clamps (`sunxi_isp.c:1108-1109`, height 3264).
- **Mainline:** one format-limit table (min, max 3264×4224 online) used by both `enum_frame_size` and `set_fmt`.

#### 5. Forum 29717: IMX214 colours, `ISP_ERR` TDM event subscribe

- **Report** (BetNem 2025-11-12; detail from chris178 2026-01-06, A7Z, image `rsdk-t2`): 4208×3120 NV12 is
  nearly B/W with ghosting, and part of the picture is cut vertically. Log shows `STITCH_2IN1_LINNER`, two ISPs
  open, `cannot find imx214_4208_3120_2_0_0 isp config, use imx214_mipi_4208_3120_30_0_0 → [imx214_mipi_default_ini_a733]`,
  `isp_dev_start, line: 717, unable to subscribe to tdm event: Inappropriate ioctl for device (25)`,
  `can not use sync mode.(buffer1 = (nil))`, and `/mnt/...ctx_saved.bin` permission denied.
  Brook_Tang: Radxa store said IMX214 has not passed A7A test *(unverified)*. Radxa (feng, 2026-07-13) points to a
  1080p `video0`/`en-largemode=0` command. No confirmed fix. shpinog reports the same on an A5E *(unverified)*.
- **Source check (ENOTTY):** `bsp/drivers/vin/vin-tdm/vin_tdm.c:1436-1439`: `sunxi_tdm_subdev_ops` has only
  `.video` and `.pad`, with no `.core` / `.subscribe_event`. A `VIDIOC_SUBSCRIBE_EVENT` on the TDM subdev
  node therefore returns ENOTTY. The ISP subdev does implement it (`sunxi_isp.c:1971-1990`). So libisp
  expects a TDM event that this kernel does not provide. Whether that causes the image fault: not proven
  (it is logged as an error, and the stream continues).
- **vs datasheet:** 4208 > 3264 online width, so this is the 2-in-1 path again (HARDWARE LIMIT for online). At 13 MP it is within DS offline 24 MP@25.
- **Category:** HARDWARE LIMIT + CODE (closed libisp vs kernel mismatch, 2-in-1 sync) + TUNING (`default_ini` fallback).
- **Mainline:** no dependence on private vendor events. For this sensor, use 1080p or a mode ≤ 3264 wide online.

#### 6. Forum 30357: IMX415 `sensor_detect read value is 0x0`

- **Report** (Sumanth, 2026-02-14, A7A, `5.15.147-14-a733`, bottom CSI port next to the micro-SD slot, overlay
  `cubie-a7a-radxa-camera-4k-415.dtbo`): `[VIN_DEV_I2C]imx415_mipi read retry 3`, `sensor_detect read value is 0x0`,
  `cannot get sensor0_{cameravdd,iovdd,avdd,dvdd} supply`, STREAMON → EPERM.
  Julien_Meekers: "driver/kernel related" *(unverified)*. Radxa (feng): use the documented gst commands. No reply from the reporter.
- **Source check:** the overlay `&sensor0` (`cubie-a7a-radxa-camera-4k-415.dtso:82-97`) defines no `sensor0_*vdd`
  supplies, only reset PE9 and pwdn PE8, I²C `twi_cci_id = 3`, addr 0x34. So the four supply warnings
  (`sensor_helper.c:1099-1120`) are expected with this overlay and are not the cause. The overlay enables
  `csi1`/`mipi1` and lists A7A, A7Z and A7S as compatible. Which physical A7A connector maps to `mipi1`/TWI3: not resolved here (schematic not traced).
- **Category:** UNKNOWN. A chip-ID read of 0 after retries means no valid I²C answer (cable, connector choice,
  sensor power or reset GPIO). Not an ISP or clock limit. The vendor stack still registers the entity
  (`media-ctl` shows `imx415_mipi`) after detect fails.
- **Mainline:** the sensor probe must return an error on a chip-ID mismatch. Trace the A7A connector to TWI/MIPI/GPIO mapping from the schematic before choosing a port for ov5647.

#### 7. Forum 30626: Radxa Camera 8M 219 not working

- **Report** (Xawery, 2026-04-07, A7Z?, image r4): `/boot/dtbo` empty, so the user compiled
  `cubie-a7a-radxa-camera-8m-219.dtso`. `/dev/video*` appears, but the Radxa docs command fails. Qinxiong_Tang (2026-07-27):
  "ISP error" with the official preview command. Radxa (Winston, 2026-10-28): use the latest release, pick the right overlay.
- **Category:** UNKNOWN (no logs). The same overlay has the 704 MHz CSI/ISP clocks of #1, so #1 is a
  candidate cause *(not verified)*. The empty dtbo directory in r4 is a packaging problem (krumpex says the same in
  radxa-a733#3).

#### 8. radxa-a733#35: Cedar VE H.264 encoder IRQ never fires

- **Report** (koolkav1, 2026-07-16, A7Z, `5.15.147-21-a733`, r6): `vencoderdemo` (no GStreamer) gives
  `h264 encoder wait interrupt overtime` on every frame, and the output is 29 bytes. `/proc/interrupts`:
  `cedar_dev_ve2` (wakeupgen 48) = 0, `cedar_dev` (49) = 29 after 29 decoded frames. Clocks are on
  (`pll-ve1`/`ve-enc0` 400 MHz). libcedarc 1.0.1 also gives VE_ENC IOMMU faults (id 0x8), which are gone with 1.0.7.
  The IRQ is still 0. One comment (2026-07-27) asks if anyone got A7Z encode working. No Radxa reply.
- **Other reports:** encode works on A7S: radxa-a733#19/#3 developakr (r6 + libcedarc 1.0.7) and forum 31224
  avaf *(unverified)*. Fails on A7Z: forum 31224 Imran_ahmed (`VeSetSpeed 624 MHz`, same timeout); unknown board: krumpex.
  The boot log line `pd_ve_enc_test@0: runtime_suspend disable clock` also appears on avaf's A7S *(unverified)*.
  radxa-a733#3 skamagedon (Orange Pi 4 Pro) says the problem is the OMX layer and links
  `github.com/skamagedon/a733-zero-copy`, which now returns 404. That does not explain the zero IRQ count with `vencoderdemo`.
- **Wiring checked against the UM (all match the vendor DT):**
  - Address: UM line 1313 `VE_ENC 0x01C10000-0x01C10FFF 4K` (VE_DEC `0x01C0E000`, 8K). DT
    `sun60iw2p1.dtsi:877-899` `ve2@1c10000`, reg size `0x1000`.
  - IRQ: UM Table 12-1: `80 VE_ENC`, `81 VE_DEC` → GIC SPI 48 / 49. DT: `interrupts = <GIC_SPI 48 IRQ_TYPE_LEVEL_HIGH>`
    (decoder `ve@1c0e000`, SPI 49, `:848-870`). **The DT IRQ number is correct.**
  - Power domain: `power-domains = <&pd SUN60IW2_PCK_VE_ENC>` (= 3, `include/dt-bindings/power/sun60iw2-power.h:8`).
    The decoder uses `PCK_VE_DEC` (2). A vendor test node `pd_ve_enc_test@0` also binds PD 3
    (`vendor-a733-reference/dtbs/sun60i-a733-cubie-a7z.dts:7107-7111`).
  - Clocks (`ccu-sun60iw2.c`): `pll_ve` = `CLK_PLL_VE1`; `ve` = `CLK_VE_ENC0` `0x0A80` M[4:0] mux[26:24] gate 31,
    parents pll-ve0, pll-ve1, peri0-800m, peri0-600m, peri0-480m, de-3x, npu (`:941-948`; UM 4.1.6.114); bus
    `CLK_BUS_VE_ENC` `0x0A8C` bit 0 (`:963`); reset `RST_BUS_VE_ENC0` `0x0A8C` bit 16 (`:2075`); AHB
    `CLK_VE_ENC_AHB_GATE` `0x05C0` bit 1, keyed (`:593`); MBUS `CLK_MBUS_VE_GATE` `0x05E0` bit 12 and
    `CLK_MBUS_VE` `0x05E4` bit 1, keyed (`:644`, `:705`).
  - IOMMU master 3 (`iommus = <&mmu_aw 3 1>`), NSI port 9, `ve_top_reg_offset = <0x800>`.
  - The VE2 node in the r6 A7S and A7Z dtbs is identical apart from phandle values (diffed here), so an A7Z-only DT difference is ruled out for those images.
- **Driver path:** `cedar_ve.c:1669` `request_irq(..., VideoEngineInterupt, 0, ...)`. On every clock enable
  `set_vcu_en_regmode_to1_to0()` (`cedar_ve.c:379-397`) pulses bits 0 and 4 of `regs_ve + 0x804`.
  `ve_plat_sun60iw2.c:153-247` handles the encoder: VCU mode reads `+0x800+0x5c` / `+0x928`, otherwise AVC enc
  status `+0xb00+0x1c`. A count of 0 in `/proc/interrupts` means the GIC never saw the line asserted.
  The cause is upstream of the handler: the encoder never ran, never enabled its interrupt, or was held in reset/power-off. Which one: not found.
- **Category:** UNKNOWN. Not a DT IRQ-number or address error (both match the UM). Not a datasheet limit (1080p ≤ 4K@30).
- **Mainline:** describe the encoder as its own device: 0x01C10000/4 KiB, SPI 48 level-high, clocks
  ve-enc0 + bus + AHB + MBUS gate + MBUS clock, reset 0x0A8C bit 16, PD VE_ENC (3), IOMMU master 3. Its first bring-up test
  should read the encoder interrupt status register after one frame with the IRQ masked (polling), so that "never
  started" can be told apart from "IRQ not routed".

### Open items (to be checked by readback or the UM, not taken from vendor or forum)

1. Real CSI/ISP clock maximum: not in DS or UM. Try rates on silicon (frame error counters at 300 / 400 / 540 / 600 MHz).
2. Is ISP0 normal mode really limited to 3264 wide (DS) or 4224 (vendor clamp)?
3. Is the vendor 2-ISP left/right split a documented hardware mode? Not found in the DS. UM CSIC sections not yet searched for it.
4. VE_ENC: read back the power-domain state, `0x0A8C`/`0x0A80`, and the encoder status after a polled frame.
