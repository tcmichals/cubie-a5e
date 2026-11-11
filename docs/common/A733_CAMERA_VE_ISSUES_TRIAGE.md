# A733 camera / VE community issue triage

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

## Summary table

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

## Correction to `CAMERA_V4L2_GUIDE.md` §5.3a

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

## Per-issue notes

### 1. radxa-a733#39: IMX219 `tdm0 rx0 frame error`, CSI/ISP clock 704 → 360/300 MHz

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

### 2. radxa-a733#19: IMX415 colour cast, `libisp_ini.so` missing the profile

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

### 3. Forum 31224: IMX415 continuous capture, `STITCH_2IN1` with one buffer

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

### 4. allwinner-bsp PR #33: ISP1 frame size limits

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

### 5. Forum 29717: IMX214 colours, `ISP_ERR` TDM event subscribe

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

### 6. Forum 30357: IMX415 `sensor_detect read value is 0x0`

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

### 7. Forum 30626: Radxa Camera 8M 219 not working

- **Report** (Xawery, 2026-04-07, A7Z?, image r4): `/boot/dtbo` empty, so the user compiled
  `cubie-a7a-radxa-camera-8m-219.dtso`. `/dev/video*` appears, but the Radxa docs command fails. Qinxiong_Tang (2026-07-27):
  "ISP error" with the official preview command. Radxa (Winston, 2026-10-28): use the latest release, pick the right overlay.
- **Category:** UNKNOWN (no logs). The same overlay has the 704 MHz CSI/ISP clocks of #1, so #1 is a
  candidate cause *(not verified)*. The empty dtbo directory in r4 is a packaging problem (krumpex says the same in
  radxa-a733#3).

### 8. radxa-a733#35: Cedar VE H.264 encoder IRQ never fires

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

## Open items (to be checked by readback or the UM, not taken from vendor or forum)

1. Real CSI/ISP clock maximum: not in DS or UM. Try rates on silicon (frame error counters at 300 / 400 / 540 / 600 MHz).
2. Is ISP0 normal mode really limited to 3264 wide (DS) or 4224 (vendor clamp)?
3. Is the vendor 2-ISP left/right split a documented hardware mode? Not found in the DS. UM CSIC sections not yet searched for it.
4. VE_ENC: read back the power-domain state, `0x0A8C`/`0x0A80`, and the encoder status after a polled frame.
