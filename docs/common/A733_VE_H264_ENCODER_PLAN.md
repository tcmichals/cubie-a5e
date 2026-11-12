# A733 VE H.264 encoder: bring-up plan (Cubie A7A)

Status: plan only, written 2026-11-11. Nothing here has been run on the board yet. Tracked in TODO.md
§2, "Zero-Copy Hardware Video Encoding (VE / Cedar H.264 Encoder)".

Owner rule (DESIGN_DECISIONS.md, "Camera: no video processing on the CPU"): sensor → CSI → hardware
ISP → NV12 dma-buf → hardware VE encoder → storage or radio. No CPU processing and no memcpy of
frames. Target: less than 2 % of one core for 1080p30 capture + encode. Until the ISP exists, the
encoder input is a synthetic NV12 buffer or a CSI RAW buffer. Both are **test-only** and are marked
that way in each step below.

Citation conventions:
- `branch:` = `linux-cubie`, branch `a7a_remoteproc_camera_usb`, head `1b0179cd1a18`
  (read with `git show`; the cedrus files have not changed since the WIP commit `27f04580fa76`).
- `vendor:` = `old-kernels/linux-a733/bsp/` (A733 BSP, Linux 5.15).
- `UM` = `tina-source/manuals-text/A733_User_Manual_V1.00.txt`; `DS` =
  `vendor-a733-reference/A733_Datasheet_V0.93.txt`.
- `triage #8` = `docs/common/A733_CAMERA_VE_ISSUES_TRIAGE.md` §8 (radxa-a733#35).

---

## 0. Summary

- The encoder is a separate VE instance ("VE2", "VE_ENC") at `0x01C10000`, 4 KiB, GIC SPI 48,
  power domain VE_ENC (3), IOMMU0 master 3. Its top-level registers are at `+0x800`, the encoder
  pre-processor ("ISP"/ENCPP) at `+0xA00`, the AVC encoder at `+0xB00`. The WIP driver uses this
  layout, and it matches the vendor interrupt handler's register offsets.
- The WIP is Bootlin's 2023 cedrus H.264 encoder (Paul Kocialkowski, written for the V3/V3s/S3) on a
  reworked cedrus (platform / proc / context / engine), plus an A733 variant (`top_reg_offset =
  0x800`) and a DT node. It writes SPS/PPS/slice headers through the hardware put-bits unit, uses a
  fixed QP (no rate control), one reference frame (P only), and V4L2 M2M with stateful-style controls.
  It has not been compiled or run.
- Three gaps can stop the engine on the first board run before any codec question comes up:
  (1) the DT node does not list the AHB master gate (`0x5C0` bit 1) or the MBUS master gate
  (`0x5E0` bit 12); they are on at reset and survive only because the A7A boots with
  `clk_ignore_unused`; (2) the vendor driver clears two bits in the system-control block
  (`0x03000000` bit 0, `0x03000004` bit 24, "SRAM to MACC" / "boot mode") and our kernel does not;
  (3) whether the A733 encoder core is register-compatible with the V3s-era AVC encoder is not
  known: the vendor library has a separate "H264 Ver2" encoder (`h264encVersion2.o`).
- The board warning `sunxi-pck-600 7060000.power-controller: sync_state() pending due to
  1c10000.video-codec` means no driver has bound to the encoder node yet: either the cedrus module
  was not loaded or its probe failed or deferred. It is a symptom to check, not a fault in the
  power-domain driver.
- No A733 IOMMU driver is needed for bring-up: mainline `sun50i-iommu` does not support the A733
  IOMMU (a different "iommu-v20" design), and CSI DMA already works on the A7A without it. The cost:
  every encoder buffer must sit below 4 GiB physical (32-bit address registers), so in CMA.
- API: keep a stateful encoder (`dev-encoder.rst`) for now, so that GStreamer `v4l2h264enc` and
  `v4l2-ctl` can drive it. The stateless encoder uAPI is not merged (Kocialkowski's series of
  2026-05-22 is a v1 without approved documentation). This is a decision for the owner (§2.4).
- **Recommended first code step:** in `linux-cubie`, once the other agent's build is finished: add
  the two master gates to the `ve_enc` node, remove the per-frame `dev_info()`, and add a probe-time
  readback (power-domain status, `CM_VE_ENC_CFG_REG`, VE top version registers `+0x8F0`/`+0x8E4`)
  behind `pm_runtime_resume_and_get()`. Then board step (a) in §4.

---

## 1. Hardware

### 1.1 Address, interrupt, power domain

| Item | Value | Source |
| --- | --- | --- |
| Register window | `0x01C10000`–`0x01C10FFF` (4 KiB) | UM:1313; vendor `configs/linux-5.15/sun60iw2p1.dtsi:876-878` |
| Decoder (separate) | `0x01C0E000`, 8 KiB, SPI 49 | UM:1312; vendor dtsi:847-851 |
| Interrupt | UM table: interrupt 80 = VE_ENC → GIC SPI 48, level high | UM:33279; vendor dtsi:880 |
| Power domain | PCK-600 domain 3 (`PD_VE_ENC`) | `branch:include/dt-bindings/power/allwinner,sun60i-a733-pck-600.h:9`; vendor dtsi:898 |
| PD status readback | `CM_VE_ENC_CFG_REG` CCU `0x1B14`: bits 17:16 status (01 off, 10 on, reset 0x2), bit 0 "module mode" | UM:12332-12345 |
| IOMMU | IOMMU0 master 3 (masters 0-3: USB0/1, CSI, ISP, VE_ENC) | UM:23266; vendor dtsi:445, 465-469 (`iommus = <&mmu_aw 3 1>`, dtsi:896) |
| NSI (interconnect QoS) port | `nsi = <&nsi0 9>` | vendor dtsi:897; no mainline driver, not needed to run |
| Sub-blocks | ENC_TOP `+0x800`, VCUENC `+0x900`, ENCPP `+0xA00`, AVC ENC `+0xB00` | vendor `drivers/ve/cedar-ve/platforms/ve_plat_sun60iw2.c:22-29`; vendor dtsi:899 `ve_top_reg_offset = <0x800>` |

The vendor interrupt handler reads ENC_TOP `+0x0` bits 7:6 to see whether the AVC encoder or the
pre-processor ran, then the AVC status at `+0xB00+0x1C` and its enable at `+0xB00+0x14`
(`ve_plat_sun60iw2.c:219-260`). The WIP uses the same offsets (`branch:drivers/staging/media/sunxi/cedrus/cedrus_regs.h:40-41, 824-849`).

### 1.2 Clocks and resets (current v6 CCU IDs on the branch)

IDs from `branch:include/dt-bindings/clock/allwinner,sun60i-a733-ccu.h` and
`.../reset/allwinner,sun60i-a733-ccu.h`; definitions in `branch:drivers/clk/sunxi-ng/ccu-sun60i-a733.c`.

| Function | v6 ID | Register / bit | Branch CCU line | Vendor name (`ccu-sun60iw2.c`) | In WIP DT node? |
| --- | --- | --- | --- | --- | --- |
| Module clock | `CLK_VE_ENC0` (124) | `0xA80`: M[4:0], mux[26:24], gate 31; parents pll-ve0, pll-ve1, peri0-800M, peri0-600M, peri0-480M, de-3x, npu | 792-805 | `ve_enc0_clk` `:941-948` | yes, "mod" |
| Bus gate | `CLK_BUS_VE_ENC0` (126) | `0xA8C` bit 0 | 822 | `ve_enc_bus_clk` `:963` | yes, "ahb" |
| MBUS clock | `CLK_MBUS_VE_ENC0` (86) | `0x5E4` bit 1 (reset 0) | 610 | `ve_mbus_clk` "ve-mclk" `:705` | yes, "ram" |
| AHB master gate | `CLK_AHB_VE_ENC` (61) | `0x5C0` bit 1 (reset 1) | 549 | `ve_enc_ahb_gate_clk` `:593`, CLK_IS_CRITICAL | **no** |
| MBUS master gate | `CLK_MBUS_VE_ENC0_GATE` (76) | `0x5E0` bit 12 (reset 1) | 585 | `ve_mbus_gate_clk` `:644`, CLK_IS_CRITICAL | **no** |
| Parent PLL | `CLK_PLL_VE1` (34) | `0x240` | 335-349 | vendor sets `ve` parent to `pll_ve` = PLL_VE1 (`cedar_ve.c:1743-1761`) | no |
| Reset | `RST_BUS_VE_ENC0` (23) | `0xA8C` bit 16 | 2245 | `:2075` | yes |

Register reset values: UM:6585 (`0x5C0` = `0x1101_03FF`), UM:6689 (`0x5E0` = `0xF105_5803`), UM:6771
(`0x5E4` = 0), UM:7729 (`0xA80` = 0), UM:7792 (`0xA8C` = 0).

Notes:
- The WIP DT node (`branch:arch/arm64/boot/dts/allwinner/sun60i-a733-cubie-a7a.dts:849-860`) names
  three clocks ("ahb" = bus gate, "mod", "ram" = MBUS clock). The two master gates are left out. They
  are on from reset; v6 registers them as ordinary gates without a consumer, so
  `clk_disable_unused` would switch them off at late init. The A7A boots with `clk_ignore_unused`
  (`project-cubie-a5e/board/radxa/cubie_a7a/boot.cmd:8`), which hides this. Before upstream, or
  before `clk_ignore_unused` is dropped, the node must take them. The mainline cedrus binding allows
  only "ahb", "mod", "ram", so an A733 encoder binding needs two more clock names (or the encoder
  gets its own binding; §5).
- The commit `c9d1a8f3fb9c` says the AHB/MBUS master gates are "handled by the key field gate ops".
  Those ops were reverted in `1b0179cd1a18` (key bits disproved on silicon), so nothing in Linux
  enables them now.
- `CM_VE_ENC_CFG_REG` bit 0 ("module mode") is written by our CCU probe
  (`ccu-sun60i-a733.c:2426-2427`, from `c9d1a8f3fb9c`). The vendor kernel never writes `0x1B14`
  (no hit in `vendor:drivers` or `vendor:configs`); whoever sets it on the vendor image (boot
  firmware or the PCK-600 hardware) is not found. Read it back in step (a).
- The VE parents "now come from the DT" (commit message of `c9d1a8f3fb9c`), but the `ve_enc` node
  has no `assigned-clocks`. cedrus calls `clk_set_rate(mod, 400 MHz)` (`cedrus.c:369`,
  `cedrus_variant_sun60i_a733_enc` `cedrus.c:601-606`); the mux may pick any parent. Add
  `assigned-clocks = <&ccu CLK_VE_ENC0>; assigned-clock-parents = <&ccu CLK_PLL_VE1>;` to match the
  vendor, and check `clk_summary` on the board.
- Vendor operating points: 624 MHz at 0.80 V for every speed bin
  (`vendor:configs/linux-5.15/sun60iw2p1-cpu-vf.dtsi:2106-2131`); vendor load table (decoder
  formats, used as a guide) asks 200 MHz up to 1080p30, 360 MHz up to 1080p60, 624 MHz above
  (`ve_plat_sun60iw2.c:66-101`). 400 MHz is enough for 1080p30.

### 1.3 System-control bits (vendor writes, Linux does not)

The vendor maps a second region `0x03000000` (16 bytes) and at probe clears bit 0 of `0x03000000`
("remapping SRAM to MACC") and bit 24 of `0x03000004` ("clear bootmode bit for give sram to ve")
(`vendor:drivers/ve/cedar-ve/cedar_ve.c:1616-1650`, called at `:1813`; DT reg at dtsi:878-879). In
Linux, `0x03000000` is the `syscon` node (`sun60i-a733-cubie-a7a.dts:475-481`, compatible
`allwinner,sun55i-a523-system-control`); it has no SRAM sub-node and the `ve_enc` node has no
`allwinner,sram` property, so `sunxi_sram_claim()` returns `-ENOENT` and cedrus goes on
(`cedrus.c:408-411`) without touching these bits. Whether the A733 encoder needs them is not found
in the UM. Read both registers in step (a); if bit 0 or bit 24 is set, that is a "never started"
candidate. Fix path: an SRAM section in the system-control binding, the same way the A64 SRAM C is
described (`drivers/soc/sunxi/sunxi_sram.c:99`).

### 1.4 IOMMU

- The A733 IOMMU at `0x03900000` (UM:1385) is a different design from the H6 one: per-master
  "MIC" registers at `0x400 + 0x200 * master` with a per-master bypass register
  (`vendor:drivers/iommu/sunxi-iommu-v2.c:97-125, 482-497`; compatible `allwinner,iommu-v20`,
  `:2229`). Mainline `drivers/iommu/sun50i-iommu.c` matches only `allwinner,sun50i-h6-iommu` and
  `allwinner,sun50i-h616-iommu` (`branch:drivers/iommu/sun50i-iommu.c:1074-1075`). No A733 support
  upstream; none found on the lists.
- Not needed for bring-up. The A7A CSI (IOMMU0 master 1) already DMAs to physical addresses with no
  IOMMU driver (`docs/common/CAMERA_V4L2_GUIDE.md:280`), so IOMMU0 passes physical addresses after
  reset at least for master 1. VE_ENC is master 3 of the same IOMMU; step (b) confirms it.
- Constraint without IOMMU: the AVC address registers are 32-bit (`cedrus_write()` of a `dma_addr_t`,
  `cedrus_enc_h264.c:1126-1185`). The A7A has 6 GiB from `0x40000000`
  (`sun60i-a733-cubie-a7a.dts:35-38`), so DRAM above `0x1_0000_0000` is not reachable. cedrus sets no
  DMA mask, so the platform default (32-bit coherent) applies, and arm64 places the default CMA
  (128 MiB, `linux.config:61-66`) below 4 GiB. Check: the `STM addr=` / `rec=` addresses printed in
  step (b) must be below `0x1_0000_0000`. Imported dma-bufs (CSI, dma-heap) must also come from CMA
  (`/dev/dma_heap/linux,cma`), not the system heap.
- The vendor library 1.0.1 gave VE_ENC IOMMU faults (id 0x8) on the A7Z, gone with 1.0.7
  (triage #8). That is a vendor IOMMU-mode issue; with the IOMMU in bypass it does not apply.

### 1.5 Memory and alignment

| Buffer | WIP size / alignment | Source |
| --- | --- | --- |
| NV12 input (OUTPUT queue) | `bytesperline` aligned to 16; width/height rounded to 16 by the frame-size step; chroma at `luma + bytesperline * height` | `cedrus_enc.c:96-130`; `cedrus.h:140-153`; frmsize 16..4096 step 16 `cedrus_enc_h264.c:1566-1574` |
| Bitstream (CAPTURE) | `sizeimage` from user, minimum 1 KiB | `cedrus_enc.c:44-45`; `STM_START/END/BIT_MAX` `cedrus_enc_h264.c:1126-1130` |
| Reconstruction | `ALIGN(w_mb,2)*16 * ALIGN(h_mb+1,4)*16` luma + chroma, 4 KiB aligned, one per coded buffer | `cedrus_enc_h264.c:557-565` |
| Sub-pixel | `(ALIGN_DOWN((w_mb+47)*2/3,32) + ALIGN(w_mb,32)*2) * (h_mb*16+72)/8` | `cedrus_enc_h264.c:539-548` |
| MB info | `DIV_ROUND_UP(w_mb,32) * 4 KiB` per context | `cedrus_enc_h264.c:468-470` |

All allocations are `dma_alloc_attrs(..., DMA_ATTR_NO_KERNEL_MAPPING)` and the queues use
`vb2_dma_contig_memops` with `VB2_MMAP | VB2_DMABUF` (`cedrus_context.c:639-667`). 1080p30 needs
roughly 3 MiB per NV12 frame plus about 3.3 MiB reconstruction per coded buffer; 128 MiB CMA is
enough for 1080p, 4K needs about 256 MiB (check against the CSI buffer count).

### 1.6 Performance limits

DS:1352-1356: H.265 MP@L5.0 and H.264 BP/MP/HP@L4.2 up to 4K@30fps; MJPEG/JPEG baseline up to
4K@15fps; "supports encoder pre-processing". DS:13-16 has the same in the feature list. The vendor
comment "h264 -- 4k30fps" (`ve_plat_sun60iw2.c:79-82`) agrees. The WIP frame-size limit is 4096 ×
4096 (`cedrus_enc_h264.c:1566-1574`); level L4.2 allows 1080p60 but not 4K at that level, so a 4K
stream would have to signal level 5.1 although the DS says L4.2: open question, not needed for 1080p30.

---

## 2. WIP vs vendor vs what is missing

### 2.1 Where the WIP comes from

The file headers say "Copyright 2023 Bootlin, Paul Kocialkowski" (`cedrus_enc_h264.c:1-7`,
`cedrus_enc.c:1-7`). This is Bootlin's `cedrus/h264-encoding` branch (GitHub `bootlin/linux`, test
tool `bootlin/v4l2-cedrus-enc-test`), announced 2023-11-15 for the V3/V3s/S3: CABAC and P frames,
QP controls only, no rate control, no pre-processing (scaling, colour conversion)
(https://bootlin.com/blog/open-source-linux-kernel-support-for-the-allwinner-v3-v3s-s3-h-264-video-encoder/;
https://www.cnx-software.com/2023/11/17/allwinner-vpu-open-source-h264-hardware-video-encoder-linux-driver/;
https://www.phoronix.com/news/Allwinner-V3-S3-H264-Open). The cedrus `TODO` file
(`branch:drivers/staging/media/sunxi/cedrus/TODO`) states the split that the WIP implements. The
FOSDEM 2024 talk describes kernel-side bitstream generation and basic rate control as the plan
(https://archive.fosdem.org/2024/events/attachments/fosdem-2024-3090-v4l2-stateless-video-encoding-hardware-support-and-uapi/slides/22199/v4l2-stateless-video-encoding_rGq1vIt.pdf).

A733-specific additions in `27f04580fa76`: compatible `allwinner,sun60i-a733-video-engine-enc` with
`top_reg_offset = 0x800` (`cedrus.c:601-606, 661-663`); top-level reset/mode programming at
resume (`cedrus.c:287-301`); DT node; CCU writes (since replaced by v6 + `c9d1a8f3fb9c`).

### 2.2 Register programming compared

The vendor kernel driver is only a register-access shim: the encode sequence lives in the closed
`libvencoder` (user space on the vendor image; the in-kernel copy under
`vendor:drivers/ve/cedar-ve/codec/lib/` is an `ar` archive with `h264encLib.o`,
`h264encVersion2.o`, `h264encRateCtrl.o`, `vcu.o`, ...). So the comparison below is with what the
vendor kernel does, plus what the library's symbol names show. Register-level comparison with the
library: not possible without disassembly; not done.

| Step | WIP | Vendor | Gap |
| --- | --- | --- | --- |
| Power/clock on | runtime resume: reset pulse, clocks ahb/mod/ram (`cedrus.c:258-286`) | `pm_runtime_get_sync`, deassert reset, ahb_gate, mbus_gate, mbus, bus, ve (`cedar_ve.c:505-531, 405-470`) | AHB/MBUS master gates (§1.2) |
| Top reset | `+0x804` bits 0 and 4 set then cleared (`cedrus.c:287-296`) | same, `set_vcu_en_regmode_to1_to0()` on every clock enable and `IOCTL_RESET_VE` (`cedar_ve.c:380-397, 523-527, 957-961`) | none |
| Mode | `+0x800`: clear bits 31:30, set ENC (bit 7), ENC ISP (bit 6), decoder disabled (`cedrus.c:297-301`; again per stream `cedrus_enc.c:56-92`) | user library | bits 31:30 meaning (RAMPD?) on A733 not in the UM excerpt read |
| Version | not read | library reads top `+0xF0` and `+0xE4` (encoder IP version, strings in `libve`: "enc_ip_version", "both 0xf0 and 0xe4 is 0") | read `+0x8F0`/`+0x8E4` in step (a) |
| Input (ENCPP) | `+0xA00`: pic info, stride in MBs, YUV420SP, BT.601, luma/chroma address (`cedrus_enc.c:135-180`) | library | colour range/matrix fixed to BT.601 |
| Headers | SPS/PPS/slice header bit by bit through PUTBITSDATA + STARTTRIG put-bits, EPTB off during headers (`cedrus_enc_h264.c:208-316, 1062-1099`) | library (`H264EncSliceHeader`, `H264EncSei`) | none functionally; CPU cost, see §2.5 |
| Bitstream out | STM start/end/bit max, bit length read back at finish (`cedrus_enc_h264.c:1118-1134, 1275-1305`) | library | none |
| References | one reference (REF0) = previous reconstruction; REF1 unused; MV buffer 0; deblock buffer 0 (`:1136-1186`) | library | B frames not needed |
| Rate control | fixed QP (`PARA1_RC_MODE_FIXED`, `:1223-1227`), RC/MAD registers zero (`:1237-1241`) | software RC in the library (`h264encRateCtrl.o`, `venc_movelevel_vbr_opt.o`) | **no bitrate control**; needed for a radio link |
| Interrupt | enable STALL + FINISH (`:1258-1273`); status `+0xB1C`, clear by writing the mask (`:1310-1330`) | enable mask `& 0x7`, status `& 0xF`, disable on IRQ (`ve_plat_sun60iw2.c:225-260`) | WIP leaves MB_OVERTIME (bit 2) masked: fine |
| Watchdog | delayed work resets the block and fails the job (`cedrus.c:152-168`) | `wait_event_timeout` → "wait interrupt overtime" | the WIP has the right tool to tell "no IRQ" from "no finish" |
| VCU mode | not used | `+0x900` VCU (register lists, "online" mode) handled in the IRQ path (`ve_plat_sun60iw2.c:153-173, 209-212`); CSI online mode reported unsupported on sun60iw2 (`ve_plat_sun60iw2.c:591-598`) | not needed: input comes from DRAM |

### 2.3 Unknown: is the A733 AVC encoder the same core?

The WIP register map was written for the V3s-era encoder. The A733 vendor library carries a
second encoder generation ("H264 Ver2": `H264EncOpenVer2`, `H264EncEncodeVer2_new`,
`VENC_CODEC_H264_VER2`, `init_temporalfilter_ver2`; strings in
`vendor:drivers/ve/cedar-ve/codec/lib/libvencoder`). Which one the A733 uses, and whether "Ver2"
changes the `+0xB00` layout, is not found. The interrupt/status offsets match (§1.1), which is a good
sign but not proof. Step (b) answers it: if the put-bits path produces the expected header bytes and
FINISH comes with a sane bit length, the map is compatible enough to continue.

### 2.4 API choice

- Mainline has no stateless encoder uAPI: `Documentation/userspace-api/media/v4l/` has
  `dev-encoder.rst` (stateful) and `dev-stateless-decoder.rst` only (branch tree). The hantro driver
  has only JPEG encoding (`hantro_h1_jpeg_enc.c`, `rockchip_vpu2_hw_jpeg_enc.c`); Bootlin's H1
  H.264 encoder was never merged
  (https://www.cnx-software.com/2020/11/24/hantro-h1-hardware-accelerated-video-encoding-support-in-mainline-linux/).
- Stateless work upstream: RFC VC8000E H.264 stateless encoder, Marco Felsch, 2025-05
  (https://lists.infradead.org/pipermail/linux-rockchip/2025-May/057921.html); Kocialkowski,
  "[PATCH 00/14] media: Add V4L2 H.264 stateless encode and VC8000E support", 2026-05-22, with an
  in-kernel CBR helper and no approved uAPI documentation yet; he plans Allwinner support later
  (https://patchew.org/linux/20260522101653.2565125-1-paulk@sys-base.io/); Media Summit slides
  (https://hverkuil.home.xs4all.nl/summit2025/Paul%20Kocialkowski%20-%20Stateless%20Video%20Encoding%20uAPI.pdf,
  https://paulk.fr/talks/2026-linux-media-summit/2026-linux-media-summit-v4l2-stateless-video-encoding-uapi-progress-update.pdf);
  July 2026 RK3576 VEPU510 RFC thread: the agreed direction is a stateless encoder spec with the
  request API and compound controls (https://lkml.iu.edu/2607.2/13803.html).
- Mainline precedent for a stateful encoder that writes SPS/PPS in the kernel: `allegro-dvt`
  (`drivers/media/platform/allegro-dvt/nal-h264.c`).
- The WIP is stateful-shaped: the kernel picks frame types, GOP, headers, QP from standard
  `V4L2_CID_MPEG_VIDEO_*` controls (`cedrus_enc_h264.c:1364-1563`). Missing for `dev-encoder.rst`
  and GStreamer: `VIDIOC_ENCODER_CMD` / `V4L2_ENC_CMD_STOP` drain and `V4L2_EVENT_EOS` (the proc
  ioctl table has only the stateless *decoder* command, `cedrus_proc.c:648-649`);
  `V4L2_CID_MPEG_VIDEO_BITRATE` / `BITRATE_MODE`; `v4l2-compliance` not run.
- Recommendation: stateful now (works with `v4l2h264enc` and `v4l2-ctl --stream-out-*` today), with
  the codec logic kept in `cedrus_enc_h264.c` so it can move under a stateless uAPI later. Upstream
  maintainers may prefer stateless for firmware-less hardware; ask the owner before any posting.
- User space to drive it:
  - `v4l2-ctl -d /dev/videoN --set-fmt-video-out=width=1920,height=1080,pixelformat=NV12
    --set-fmt-video=pixelformat=H264 --stream-out-mmap --stream-mmap --stream-from=in.nv12
    --stream-to=out.h264` (steps b-d);
  - `gst-launch-1.0 ... ! v4l2h264enc ! h264parse ! ...` once ENCODER_CMD is in (step d/e);
  - a small C test for dma-buf sharing (step e), in the repo, not `/tmp`.

### 2.5 Other WIP issues found while reading

1. `dev_info()` on every frame in `cedrus_enc_h264_job_configure()` (`cedrus_enc_h264.c:1124-1125`):
   debug only; costs CPU and floods the log at 30 fps. Make it `dev_dbg()` or drop it.
2. Header writing polls `PUT_BITS_READY` with `readl_poll_timeout_atomic(..., 10, 1000)` before every
   element (`cedrus_enc_h264.c:211-212`, `cedrus.h:116-122`) and ignores a timeout. If the bit is not
   already set, each element waits 10 µs; an IDR has about 100 elements. Measure in step (f); use a
   1 µs (or 0) delay and handle the timeout.
3. 1080 lines is not a multiple of 16: `v4l2_apply_frmsize_constraints()` makes the OUTPUT format
   1088 lines (`cedrus_enc.c:105`), so the encoder expects chroma at `bytesperline * 1088` and a
   3133440-byte buffer, while CSI NV12 at 1080 lines puts chroma at `bytesperline * 1080` with 3110400
   bytes (`sun6i_csi_capture.c:662-695`, `v4l2_fill_pixfmt`). See §3.2.
4. The watchdog resets the shared reset line with `reset_control_reset()` (`cedrus.c:165`), which
   also clears the top-level mode set at resume; the next job reconfigures the mode
   (`cedrus_enc.c:56-92`) but not the `+0x804` pulse. Check after a forced timeout.
5. The A733 compatibles are not in the binding (`branch:Documentation/devicetree/bindings/media/allwinner,sun4i-a10-video-engine.yaml`
   has no a733/a523 entries).
6. `docs/common/VIDEO_ENCODING_VPU_GUIDE.md` §1 gives `0x01C0E000` / SPI 49 as the A7A encoder;
   that is the decoder. The encoder is `0x01C10000` / SPI 48.
7. TODO.md's status matrix (row "Radxa Cubie A7A", added in `554e1f7`) says "Sunxi-Cedrus V4L2 M2M
   hardware H.264 encoder verified (runs at 27.22 FPS)". No log in the repo backs this; the capture
   tool in that commit converts to NV12 on the CPU, and `a7a_1080p30_video.avi` in the working tree
   is Motion JPEG. Treat the encoder as **not verified** until step (b) passes.

---

## 3. Zero-copy path

### 3.1 Export and import

- `sun6i-csi` (used for the A733 CSI through the `allwinner,sun55i-a523-csi` compatible,
  `sun60i-a733-cubie-a7a.dts:812-813`) supports `VB2_MMAP | VB2_DMABUF` and `VIDIOC_EXPBUF`
  (`branch:drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_capture.c:819, 1006-1009`), with
  `vb2_dma_contig_memops`. It can both export its buffers and import external ones.
- The encoder OUTPUT queue accepts `VB2_DMABUF` (`cedrus_context.c:639-642`) and takes the plane
  address with `vb2_dma_contig_plane_dma_addr()` (`cedrus.h:148`). Without an IOMMU, an imported
  dma-buf must be physically contiguous: CSI-exported buffers are (dma-contig/CMA); dma-heap buffers
  must come from the CMA heap.
- Two ways to share, both zero-copy:
  1. CSI exports (`VIDIOC_EXPBUF`), the encoder imports the fd (GStreamer `v4l2src
     io-mode=dmabuf ! v4l2h264enc output-io-mode=dmabuf-import`).
  2. Buffers from `/dev/dma_heap/linux,cma`, imported by both CSI (CAPTURE, `V4L2_MEMORY_DMABUF`) and
     the encoder (OUTPUT). This one lets the buffer be larger than the CSI `sizeimage` (needed for
     §3.2 and the RAW test below).
- Cache: CSI writes by DMA and the encoder reads by DMA; no CPU access, so no cache maintenance is
  needed as long as nobody maps the frames. vb2 does the `dma_buf_map_attachment()` syncs.

### 3.2 NV12 layout compatibility

| Property | CSI (`sun6i_csi_capture.c`) | Encoder (WIP) | Compatible? |
| --- | --- | --- | --- |
| Planes | single-plane NV12 (`V4L2_PIX_FMT_NV12`, `:171`) | single-plane NV12 | yes |
| Stride | `bytesperline = width` (`v4l2_fill_pixfmt`) | `ALIGN(bytesperline, 16)` | yes when width % 16 = 0 (1920, 1280, 3840) |
| Chroma offset | `bytesperline * height` | `bytesperline * ALIGN(height, 16)` | 720p, 2160p: yes; **1080p: no** (1080 vs 1088) |
| Size limits | 32..4800 (`sun6i_csi_capture.h:16-19`) | 16..4096 step 16 | yes |

Fix for 1080p (choose one, in the encoder): accept heights that are not a multiple of 16 and keep
the chroma plane at `bytesperline * height`, with the coded size rounded up and `frame_cropping`
in the SPS (the WIP already writes crop offsets from the selection rectangle,
`cedrus_enc_h264.c:800-830`). The engine then reads 8 lines past the luma plane (into chroma) and
past the chroma plane's end; the buffer needs that slack, so for 1080p allocate the shared buffer
for 1088 lines (dma-heap path, §3.1 way 2). Whether the hardware reads beyond the last MB row:
not found, check with a guard pattern in step (d).

The ISP output (future) will have its own stride and padding; the vendor ISP600 alignment is not
yet looked up (CAMERA_V4L2_GUIDE.md §5 owns that). The same check applies when it exists.

### 3.3 Test-only input before the ISP exists

- **Synthetic NV12 (test-only):** generated once into a file or a dma-heap buffer before streaming
  (no per-frame CPU work), e.g. `gst-launch-1.0 videotestsrc num-buffers=300 ! video/x-raw,format=NV12,width=1920,height=1080
  ! filesink location=...` on the PC, or the same frames looped from a few preallocated buffers.
- **CSI RAW → encoder (test-only, tests the plumbing, not the picture):** the OV5647/IMX415 give
  Bayer only, and CSI without the ISP cannot make NV12. For a zero-copy plumbing test: allocate NV12-
  sized CMA dma-heap buffers, fill the chroma half with `0x80` once at setup, let CSI capture RAW8
  into the first `width * height` bytes (CSI imports the fd; its `sizeimage` is smaller than the
  buffer), and queue the same fd to the encoder as NV12. The result is a grey picture with a Bayer
  pattern; what it proves is the dma-buf path, frame pacing and CPU load. RAW10 needs RAW8 mode on the
  sensor or the CSI packing; the CPU never touches the frames. Never ship this path.
- CPU debayer or CPU NV12 conversion (as in `a7a-cam-capture.c`) is not allowed, even as a
  measurement baseline for this plan.

---

## 4. Bring-up steps (smallest first)

Common rules: board A7A, `root@192.168.1.12` over `wlan0` (pass `--ip`), one serial reader at a
time (ttyUSB1). Kernel version must match `/lib/modules`; keep the backup Image. Build in
`linux-cubie` only, never in `bld.a7a/`. Each step records commands and output under
`logs/a7a-ve-enc-<date>/`. A step passes only on the stated readback.

### (a) Probe, clocks, power domain, reset, version register

Code: the first code step from §0 (master gates in DT, assigned parent PLL_VE1, probe-time readback
of `0x1B14`, `+0x800`, `+0x804`, `+0x8E4`, `+0x8F0`, `dev_dbg` per frame).

Board:
1. `modprobe sunxi-cedrus`; `dmesg | grep -i -e cedrus -e 1c10000`;
   `cat /sys/kernel/debug/devices_deferred`; `ls /sys/bus/platform/drivers/cedrus/` (or the driver
   name printed). The `sync_state() pending due to 1c10000.video-codec` line must be gone after the
   bind.
2. `cat /sys/kernel/debug/clk/clk_summary | grep -e ve -e pll-ve`: ve-enc0 400 MHz (or the chosen
   rate) from pll-ve1; bus-ve-enc0, mbus-ve-enc0, ahb-ve-enc, mbus-ve-enc0-gate enabled.
3. `cat /sys/kernel/debug/pm_genpd/pm_genpd_summary`: VE_ENC on while the device is resumed, off
   after autosuspend.
4. Register readback with the device resumed (hold it with an open fd, or the probe readback):
   `devmem 0x02002A80`, `0x02002A8C` (bit 0 and 16 set), `0x020025C0` bit 1, `0x020025E0` bit 12,
   `0x020025E4` bit 1, `0x02003B14` (bits 17:16 = 10, bit 0 = 1), `0x03000000`, `0x03000004`,
   `0x01C108F0`, `0x01C108E4`.
   Pass: version registers non-zero and stable; PD status "on". A zero version with everything else
   on points at §1.3 or the power domain.
5. `v4l2-ctl --list-devices`; `v4l2-ctl -d /dev/videoN --all` (H264 on CAPTURE, NV12 on OUTPUT).

### (b) One synthetic NV12 frame, interrupt masked (poll mode)

Goal (triage #8): tell "engine never started" from "IRQ not routed".

Code (debug-only module parameter, e.g. `poll=1`): trigger with `INT_EN` = 0, poll
`+0xB1C` for FINISH/STALL/OVERTIME with a timeout of a few hundred ms in the job path, read
`STM_BIT_LEN` (`+0xB90`), then complete the job as the IRQ path would.

Board: one 1280×720 grey frame (720 is MB-aligned), QP fixed, CAVLC first (`--set-ctrl
h264_entropy_mode=0`), `v4l2-ctl --stream-out-mmap --stream-mmap --stream-count=1`.

Outcomes:
- FINISH set, bit length > header bits, output parses with `ffprobe` / `h264_analyze`: engine runs,
  register map compatible (answers §2.3). Go to (c).
- Header bytes present (put-bits works) but no FINISH: encoder core configured wrongly or the input
  DMA cannot read (IOMMU master 3 not in bypass, address above 4 GiB, MBUS gate). Check `0x020025E4`
  bit 1, the IOMMU bypass state (read `0x03900000 + 0x400 + 0x200*3 + 0x4`, register layout from
  `sunxi-iommu-v2.c:101-102`), the printed DMA addresses.
- No put-bits progress at all (`STM_BIT_LEN` stays 0): block not powered/clocked/out of reset, or
  §1.3 bits. Go back to (a).
- STALL: bitstream buffer too small or wrong end address.

### (c) Interrupt path

Board: same frame with `poll=0`. `grep -e cedrus /proc/interrupts` before and after; the count must
go up by one per frame. If (b) passed and the count stays 0 here: the GIC route (SPI 48 level) or
`INT_EN` writes do not take; read `+0xB14` after trigger, and `/proc/irq/<n>/` affinity. This is
the vendor A7Z failure mode (triage #8), now testable with a known-good engine.

### (d) 1080p30 stream from a synthetic source

Board: 300 frames 1920×1080 (after the §3.2 crop fix; before it, 1920×1088), GOP 30, CABAC, High
profile, QP I/P 26/28. Checks: all 300 frames out, `ffprobe` frame count and resolution 1920×1080,
IDR every 30 frames, playable on the PC; per-frame encode time from `trace-cmd` / timestamps
(< 33 ms at 400 MHz, else raise the module clock toward the 624 MHz OPP). Guard pattern after the
NV12 plane to see whether the engine reads past it (§3.2). Forced timeout test: queue a frame with a
zero-size bitstream buffer; the watchdog must recover and the next stream must work (§2.5 item 4).
Then add `VIDIOC_ENCODER_CMD` and run the same through `v4l2h264enc` with `filesrc` /
`videotestsrc` (test-only source).

### (e) CSI → encoder zero-copy (test-only until the ISP exists)

Board, RAW plumbing test from §3.3: CMA dma-heap buffers imported by CSI and the encoder; a small C
program in `project-cubie-a5e/board/radxa/cubie_a7a/src/` (QBUF the CSI-done fd to the encoder
OUTPUT, the encoder-done fd back to CSI). Checks: frame rate held at 30, no dropped CSI frames
(sequence numbers), no IOMMU or CSI FIFO errors in `dmesg`, frames never mapped by the CPU (no
`mmap` in the program). Label the output files "RAW-as-luma, test-only".
When the ISP exists: the same program with ISP NV12, and the GStreamer line from TODO.md
(`v4l2src io-mode=dmabuf ! ... ! v4l2h264enc ! h264parse ! mp4mux ! filesink`).

### (f) CPU load < 2 % of one core

Board: 10 minutes of (e) (later: ISP path) at 1080p30. Measure with `pidstat -u -p <pid> 1`,
`/proc/stat` deltas for the whole system (softirq/irq included), and `perf top` for the hot spots;
count `/proc/interrupts` for cedrus and CSI (expect 30/s each). Budget items to look at: header
put-bits polling (§2.5 item 2), the per-frame `dev_info`, m2m scheduling, CSI IRQ. Pass: < 2 % of
one core for capture + encode + the file write (the write path is measured but reported separately).

---

## 5. Risks and open questions

1. **Encoder generation (§2.3).** If the A733 AVC encoder is the "Ver2" core with a different
   register layout, the WIP codec file needs real work and the only reference is the closed
   `libvencoder`. Step (b) decides this early. Not found: any public A733 encoder register
   documentation (the UM VE chapter, UM:35504 "14.1 Video Encoder", was not read for registers in
   this pass; read it before step (b)).
2. **IOMMU (§1.4).** Bypass after reset is inferred from CSI (master 1), not shown for master 3.
   Long term, 6 GiB boards want an A733 IOMMU driver (none upstream) or `dma-ranges`/CMA kept below
   4 GiB. Writing that driver is a separate project.
3. **Power domain and sync_state.** `sync_state() pending due to 1c10000.video-codec` shows the
   encoder never bound on the tested image: check whether `sunxi-cedrus.ko` was loaded and why its
   probe failed (`devices_deferred`). With `fw_devlink=permissive` in the boot arguments
   (`boot.cmd:8`) the PCK-600 keeps every domain on until consumers bind; when cedrus binds and
   autosuspends, VE_ENC switches off for the first time. A domain that does not come back on
   (`0x1B14` bits 17:16 not `10`) would look like "never started". The vendor DT has a
   `pd_ve_enc_test@0` node on the same domain (`vendor-a733-reference/dtbs/sun60i-a733-cubie-a7a.dts:7081-7086`);
   its purpose is not found.
4. **Vendor A7Z failure (triage #8).** IRQ 48 count 0 with the vendor stack on A7Z, encode reported
   working on A7S (unverified). Cause not found. Our steps (b)/(c) split it into engine vs IRQ for the
   A7A with our own code, so the vendor result does not block us; record the A7A answer in the triage.
5. **Master gates and `clk_ignore_unused` (§1.2).** Without the DT fix, dropping `clk_ignore_unused`
   (a TODO v6 board item) silently kills the encoder bus master.
6. **System-control bits (§1.3).** Unknown whether needed on A733.
7. **1080p layout (§3.2)** and the ISP output layout (unknown until the ISP work).
8. **Rate control.** Fixed QP only. For storage this is usable; for the radio link a frame-level CBR
   (cheap, in the kernel like Kocialkowski's `v4l2-h264-enc-rc.c` helper, or in user space for a
   stateless API) is needed. Hardware RC/MAD registers exist (`cedrus_regs.h:865-877`), meaning not
   documented.
9. **Upstream shape.** Bindings: a separate compatible for the A733 encoder instance (own clocks,
   PD, IOMMU master) vs the shared cedrus binding; stateful vs stateless (§2.4); the cedrus rework is
   large and belongs to Bootlin's design: coordinate before posting. Owner decision, nothing is sent
   without the go-ahead.
10. **H.265 and JPEG.** The DS lists H.265 and JPEG encoding on the same block; out of scope here.
    H.265 would also need the "Ver2"/VCU question answered.

---

## 6. Sources not found or not read

- A733 encoder register description: UM chapter 14.1 (UM:35504 on) not read for registers in this
  pass.
- Vendor user-space encoder source: not found under `~/projects/cubie` (only the closed
  `libvencoder`/`libve` archives in `vendor:drivers/ve/cedar-ve/codec/lib/`, headers
  `vendor-a733-reference/modules/*.h`, `libcedarc.a` for RISC-V in `tina-source/rtos`).
- Who sets `CM_VE_ENC_CFG_REG` bit 0 on the vendor image: not found.
- IOMMU0 bypass reset value: not in the UM text; inferred from CSI behaviour only.
- Any upstream A733 VE (decoder or encoder) patches: none found.
