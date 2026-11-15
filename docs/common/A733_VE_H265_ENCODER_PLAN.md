# A733 VE H.265 (HEVC) Encoder Plan

Status 2026-11-14: plan only. Depends on the H.264 encoder working on our kernel (address-shift fix,
`A733_VE_H264_ENCODER_PLAN.md`, `logs/a7a-ve-enc-20261114/RESULTS.md`). Owner goal: H.265 recording
for the flight stack (A7Z v2.00), 1080p30 first.

## 1. Facts (from the vendor r6 image, 2026-11-14)

| Fact | Source |
| :--- | :--- |
| H.265 encode works on the A7A with the vendor stack: `demoVencoder -f 3`, 3 frames 1280×720 → valid stream (decodes with GStreamer `libde265dec`) | `logs/a7a-ve-enc-20261114/vendor-r6/out3.h265`, `venc3_h265.log` |
| Same engine and register window as H.264 (`0x01C10000`, ENC_TOP `+0x800`, AVC block `+0xB00`); same `addr >> 8` output address registers (`+0xB60`-`+0xBC4`) | snapshots `vendor-r6/h264/regs_0.bin` vs `vendor-r6/h265/regs_0.bin` |
| H.264 → H.265 register differences (frame 0): ENC_TOP `+0x804` `0x300`→`0x100`, `+0x81C` `0`→`0x10000`; ENCPP `+0xA08`, `+0xA90`-`+0xA9C` cleared, `+0xAD4` `2`→`1`; AVC `+0xB08`, `+0xB10`-`+0xB20`, `+0xB2C`, `+0xB40`-`+0xB5C`, `+0xB64`-`+0xB70`, `+0xB90`, `+0xB94`, `+0xBA8` (about 35 words) | same, diff table in §4 step 1 |
| Stream layout: VPS (25 B), SPS (60 B), PPS (10 B), then one slice NAL per frame: IDR_W_RADL (type 19), then TRAIL_R (type 1) P-frames | NAL scan of `out3.h265` |
| Vendor library: `libvenc_h265.so` (arm64, stripped, 26 exported functions, unoptimised code): `h265WriteReg` (0x66c B), `h265WriteAddr` (0x84c B), `H265InitVPS`/`SPS`/`PPS`, `H265StartSlice`, `h265EncFrame`, `H265InitRcGop`/`RcFrame`, `H265UpdateRcFrame`, `H265SetParameter` | `radxa-r6/x/libvenc_h265.so` (from `libcedarc-dev-2.0.0-arm64`) |
| `h265WriteReg` writes AVC `+0x04`, `+0x08`, `+0x10`, `+0x28`-`+0x4C`, `+0x68`-`+0x74`, `+0x94`, `+0x98`, `+0xCC` per frame | disassembly, `scratchpad/parsewr.py` |
| `h265WriteAddr` writes bitstream `+0x80`/`+0x84`, `+0x88` = bit offset (`lsl #3`), `+0x8C`; reference/reconstruction `+0x78`/`+0x7C`, `+0xA0`-`+0xBC`; MV/info `+0x9C`, `+0xC0`, `+0xC4`, `+0x60`/`+0x64`, `+0xC8`, `+0xFC`, `+0x3C`, `+0x14`, `+0x1C`. Addresses come pre-shifted from the context | disassembly |
| No H.265 code in the unstripped BSP RISC-V `libvencoder` (H.264, JPEG only) | `ar t` of `bsp/drivers/ve/cedar-ve/codec/lib/libvencoder` |
| Mainline cedrus encoder (Bootlin WIP) is H.264 only; mainline has no stateless encoder uAPI; HEVC encoder controls exist for stateful encoders (`V4L2_CID_MPEG_VIDEO_HEVC_*`) | `linux-cubie` |

## 2. Approach

Two stages, the first good enough to fly:

- **Stage A, fixed setting (recording):** 1920×1080 (and 1280×720 for tests), 30 fps, fixed QP,
  GOP = IDR + P-frames, one slice per frame. Registers replayed from vendor snapshots of exactly this
  setting, with only addresses and per-frame fields (frame type, POC, reference) computed. VPS/SPS/PPS
  copied as constant bytes from the vendor stream of the same setting.
- **Stage B, general:** every written field understood (width/height, QP, GOP, bitrate control),
  headers generated from parameters. Needed for upstream and for a bitrate-controlled downlink.

Method for both: the vendor r6 image is the oracle. Snapshot registers per frame with the
`LD_PRELOAD` shim (`vendor-r6/vedump.c`), change one encoder parameter per run, diff.

## 3. Board logistics

One SD card: the vendor image and ours alternate (`dd`, ~5 min). Batch all vendor runs in one
vendor boot; keep `radxa-r6/r6.img` and `bld.a7a/images/sdcard.img`. Vendor console: `ttyAS0`
115200, `radxa`/`radxa`, `sudo -i`. Files move over serial (base64, checked by md5).

## 4. Steps

### Step 0 — prerequisite: H.264 works on our kernel
- [ ] Cedrus `enc_addr_shift` fix (built 2026-11-14, `linux-cubie`, not committed) board-tested:
  grey/noise 720p I+P frames decode; commit. If not: VE_MODE bits 31:30 (vendor `0xC00000C7`).
- [ ] 1080p30 synthetic H.264 (plan step d). Exit: the shared base (power, clocks, ENCPP input,
  addresses, IRQ) is proven.

### Step 1 — vendor register sweep (one vendor boot, offline analysis)
Extend `vedump.c`: snapshot every frame (not only the first 4), add ENC_TOP `+0x000`-`+0x7FF` only
after checking which ranges are safe (the VCU `+0x900` read hung our kernel; keep it excluded), and
record the bitstream bit length after each frame.
- [ ] Runs (`-f 3`, 10 frames each): 1280×720 and 1920×1080; QP 20/30/40 (`--i qp min` and fixed-QP
  options of `demoVencoder`, check `--help`); GOP/key-frame period 1/5/30; `--rc_mode` variants;
  same picture content as the H.264 tests.
- [ ] Same runs with `-f 0` (H.264) for the shared fields.
- [ ] Pull snapshots, streams and logs into `logs/a7a-ve-h265-<date>/`.
- [ ] Table of every AVC/ENC_TOP/ENCPP word: constant, size-dependent, QP-dependent,
  frame-type-dependent, per-frame counter, address. Exit: every word written per frame is classified.

### Step 2 — library reading (offline, parallel to step 1)
- [ ] `h265WriteReg`/`h265WriteAddr`: map context offsets → register offsets (finish the parser).
- [ ] `H265InitSPS`/`PPS`/`VPS` and `H265StartSlice`: which fields come from parameters, and whether
  the slice header is written by software (bit writer) or by the hardware (`+0x18` start-trigger
  `PUT_BITS`-style commands as in H.264).
- [ ] `h265EncFrame`: the per-frame sequence (reset, writes, start trigger value, wait, read bit
  length) to mirror in the driver.
- [ ] `H265InitRcFrame`/`UpdateRcFrame`: QP per frame (for Stage B rate control).

### Step 3 — Stage A driver (our kernel)
In `linux-cubie`, cedrus encoder:
- [ ] Codec abstraction: today the encoder path is H.264-specific (`cedrus_enc_h264.c`); add an
  `cedrus_enc_h265.c` with the same ops (setup, job configure, job finish), sharing buffers, ENCPP
  input setup and the address shift.
- [ ] `V4L2_PIX_FMT_HEVC` on the CAPTURE queue; HEVC profile/level/QP controls
  (`V4L2_CID_MPEG_VIDEO_HEVC_*`); capability flag `CEDRUS_CAPABILITY_H265_ENC` on the A733
  encoder variant only.
- [ ] Buffers: reconstruction, reference, MV/info sizes from the vendor addresses in the snapshots
  (gaps between `+0xB60`-`+0xBC4` values give the sizes for 1280×720 and 1920×1080).
- [ ] Headers: VPS/SPS/PPS bytes for the fixed setting at the start of the first CAPTURE buffer
  and every IDR; slice header per step 2.
- [ ] Per-frame fields from the step 1 table (frame type, POC, reference index).
- [ ] Board test: 10 frames 720p grey/noise → decodes with `libde265dec`/ffmpeg, picture matches;
  then 300 frames 1080p30, no errors, bit length per frame close to the vendor's.

### Step 4 — Stage A integration
- [ ] GStreamer `v4l2h265enc` (or ffmpeg `hevc_v4l2m2m`) pipeline from a file/test source to MP4
  or MKV on SD.
- [ ] CPU load < 2 % of one core at 1080p30 (same rule as H.264).
- [ ] Camera input after ISP Phase 1 (NV12 dma-buf), same as H.264 step (e).

### Step 5 — Stage B (general)
- [ ] Every field from the step 1 table computed from parameters (size, QP, GOP).
- [ ] SPS/PPS/VPS generated by a bit writer from parameters; compare byte-exact with the vendor
  output for the swept settings.
- [ ] Rate control: fixed QP → simple CBR/VBR (vendor RC functions as reference).
- [ ] `v4l2-compliance` on the encoder node; decode tests at several sizes and QPs.
- [ ] Upstream: only together with (after) the Bootlin cedrus encoder series.

## 5. Estimates (after step 0)

| Stage | Estimate |
| :--- | :--- |
| Steps 1-2 (sweep + reading) | 2-4 days |
| Step 3-4 (Stage A) | about 1 week |
| Step 5 (Stage B) | 2-4 more weeks |

## 6. Risks / open questions
- Whether the slice header is software- or hardware-written (step 2).
- ENC_TOP `+0x000`-`+0x7FF` and `+0x900` were not read; if H.265 needs registers there, reading them
  needs care (VCU read hung our kernel). The vendor run reports `bEnableVcuFuncFlag = 0`.
- The vendor uses the IOMMU (master 3 `BYP` = 0); we run master 3 in bypass with contiguous buffers.
  Fine for H.264 so far; keep watching.
- Reference/MV buffer sizes at 1080p (1088 lines?) must be checked against the 1080p snapshots.
