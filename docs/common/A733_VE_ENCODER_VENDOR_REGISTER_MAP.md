# A733 VE Encoder: Vendor Register Map and Cedrus Fix List

Status 2026-11-14. **Start here to continue the encoder work.** Data: vendor Radxa r6 image on the
A7A, `demoVencoder` under an `LD_PRELOAD` shim that snapshots the registers at every
`IOCTL_WAIT_VE_EN` (all registers programmed, engine about to run).

- Raw data: `logs/a7a-ve-enc-20261114/vendor-r6/sweep/<run>/regs_NN.bin` (12 runs × 8 frames;
  `f0` = H.264, `f3` = H.265; 720p/1080p, fixed QP 20/40, key period 3, 500 kbit/s), plus `out.bin`
  (the stream) and `log.txt`. Layout of a snapshot: 8 KiB, `[0x000-0xFFF]` = `0x01C10000` window,
  `[0x1000-0x1FFF]` = `0x03900000` (IOMMU) window, `0xdeadbeef` = not read (VCU `+0x900` and
  `+0x000-0x7FF` never read: reading `+0x900` hung our kernel).
- Regenerate the tables below: `logs/a7a-ve-enc-20261114/vendor-r6/sweep/analyze.py <sweep dir>
  <linux-cubie>/drivers/staging/media/sunxi/cedrus`.
- Test log: `logs/a7a-ve-enc-20261114/RESULTS.md`. H.265 plan: `A733_VE_H265_ENCODER_PLAN.md`.
  H.264 plan: `A733_VE_H264_ENCODER_PLAN.md`.
- Note: the H.265 fixed-QP options of `demoVencoder` had no effect (same 43,743-byte stream for
  default, QP 20, QP 40); use `-b` (bitrate) or find the right option for H.265 QP sweeps.

## 1. How the vendor stack works

1. Kernel `sunxi-ve.ko` (GPL, `oldkernels/linux-a733/bsp/drivers/ve/cedar-ve/cedar_ve.c`): maps
   `0x01C10000`, enables clocks/reset/power, pulses ENC_TOP `+0x804` bits 0 and 4 after every clock
   enable and reset (`set_vcu_en_regmode_to1_to0`, `:380`), sleeps in `IOCTL_WAIT_VE_EN` until the
   IRQ. It programs no codec register.
2. Userspace `libvenc_*.so` (closed) mmaps the registers and programs everything: ENC_TOP mode,
   ENCPP ("ISP", `+0xA00`) for the input picture, AVC block (`+0xB00`) for the codec. Buffers come
   from `/dev/dma_heap/system` through the IOMMU (master 3 not in bypass).
3. Per frame (H.264 `SetH264RegVer2`, H.265 `h265WriteReg` + `h265WriteAddr`): write parameters and
   addresses, write headers through the hardware bit writer, start, `IOCTL_WAIT_VE_EN`, read the bit
   length (`+0xB90`).

## 2. Cedrus fix list for H.264 (in order; test after each on our kernel)

Files under `linux-cubie/drivers/staging/media/sunxi/cedrus/`.

| # | What the vendor does | Cedrus today | Change | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Output-side addresses `+0xB60`-`+0xBC4`, `+0xB80`/`+0xB84` are `addr >> 8` | full byte addresses | `enc_addr_shift = 8` in the A733 encoder variant | done, `0dd9d7650774`, **board test pending** |
| 2 | `+0xB60` (MV buffer) and `+0xBC4` (deblock buffer) point to real buffers (`0x00FF3E80`, `0x00FF3E00`) | writes 0 to both (`cedrus_enc_h264.c:1166`, `:1209`), so the engine may write to address 0 | allocate both (sizes: see address gaps in the snapshots, 720p vs 1080p) and program them `>> 8` | to do; first suspect if #1 alone is not enough |
| 3 | `+0xB9C` holds another buffer address (`0x00FF3DF0`) | never written | find its role (`h264encVersion2.o` in the BSP RISC-V lib has symbols), allocate | to do |
| 4 | `+0xB00` = picture size in MBs (`0x00A0005A` 720p, same value as `+0xA00`) | never written | write it with the `VE_ISP_PIC_INFO_REG` value | to do |
| 5 | ENC_TOP `+0x800` = `0xC00000C7` (bits 31:30 set while encoding) | clears bits 31:30 (`cedrus.c:315`, `cedrus_enc.c:76`, `:95`) | test with the bits set on the A733 | to do |
| 6 | ENCPP: `+0xA14` = stride in MBs (`0x50`), `+0xA80` chroma1 address, `+0xA90` = `0x03FF03FF`, `+0xA98` size + `+0xA9C` address (another buffer), `+0xAD4`, `+0xAF0` = `0x200` | not written | add for the A733; `+0xA9C` needs a buffer | to do |
| 7 | AVC `+0xB40` = `0x74000014`, `+0xB44` = `0x0C1E0000`, `+0xB70`, `+0xB74` (QP-table-like, changes per frame) | not written | add constants; decode `+0xB70`/`+0xB74` | to do |
| 8 | ENC_TOP `+0x81C` = `0x10000`, `+0x840` = `0xF`, `+0x880` = `0x1C55`, `+0x884` = `0xFFFFF`, `+0x888` = `0x8000` | not written | read them on our kernel after an encode (device resumed); write what differs | to do |
| 9 | Vendor leaves 0: `+0xB0C`, `+0xB24`, `+0xB28`, `+0xB3C`, `+0xB50`, `+0xB88` | cedrus writes them | check the values cedrus writes are 0 or harmless | to do |
| 10 | — | default CAPTURE `sizeimage` 1 KiB (`cedrus_enc.c:45`) | raise the default | to do |

Board test for each step: `modprobe sunxi-cedrus dump_coded=1`, encode grey and noise 720p frames
with `sizeimage=1048576` (commands in RESULTS.md), decode the output. Rules: read VE_ENC registers
only with the device resumed; never read `+0x900`.

For a direct comparison, add a debug dump of `+0x800-0x8FF` and `+0xA00-0xBFF` in
`cedrus_enc_h264_job_finish()` (same layout as the vendor snapshots) and diff against
`sweep/f0_720_def/regs_00.bin` (IDR) and `regs_01.bin` (P).

## 3. H.265

Same engine, same address form. Differences from H.264 are in the table below (ENC_TOP `+0x804`
`0x100`, AVC `+0xB08`-`+0xB70`). Steps: `A733_VE_H265_ENCODER_PLAN.md` (step 1, the sweep, is done:
this data).

## 4. Tables (generated)

## H.264: vendor register image (720p default, frame 0 = IDR, frame 1 = P)

| Offset | cedrus name | cedrus writes | frame 0 | frame 1 | varies with | 1080p frame 0 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `+0x800` | `VE_MODE_REG (top)` | yes | `c00000c7` | `c00000c7` | constant | `c00000c7` |
| `+0x804` | `VE_RESET_REG (top)` | yes | `00000300` | `00000300` | frame, QP, bitrate | `00000300` |
| `+0x81c` | — | no | `00010000` | `00010000` | frame, QP | `00010000` |
| `+0x840` | — | no | `0000000f` | `0000000f` | constant | `0000000f` |
| `+0x880` | — | no | `00001c55` | `00001c55` | constant | `00001c55` |
| `+0x884` | — | no | `000fffff` | `000fffff` | constant | `000fffff` |
| `+0x888` | — | no | `00008000` | `00008000` | constant | `00008000` |
| `+0x8e0` | — | no | `00d33310` | `00d33310` | constant | `00d33310` |
| `+0x8e4` | — | no | `00e21320` | `00e21320` | constant | `00e21320` |
| `+0xa00` | `VE_ISP_PIC_INFO_REG` | yes | `00a0005a` | `00a0005a` | size | `00f00087` |
| `+0xa04` | `VE_ISP_PIC_STRIDE0_REG` | yes | `00500000` | `00500000` | size | `00780000` |
| `+0xa08` | `VE_ISP_CTRL_REG` | yes | `00020001` | `00000001` | frame | `00020001` |
| `+0xa14` | — | no | `00000050` | `00000050` | size | `00000078` |
| `+0xa2c` | `VE_ISP_SCALER_SIZE_REG` | yes | `005a00a0` | `005a00a0` | size | `008700f0` |
| `+0xa78` | `VE_ISP_INPUT_LUMA_ADDR_REG` | yes | `ff000000` | `ff000000` | address | `fe400000` |
| `+0xa7c` | `VE_ISP_INPUT_CHROMA0_ADDR_REG` | yes | `fef80000` | `fef80000` | address | `fe300000` |
| `+0xa80` | `VE_ISP_INPUT_CHROMA1_ADDR_REG` | no | `fefb8400` | `fefb8400` | address | `fe37e900` |
| `+0xa90` | — | no | `03ff03ff` | `03ff03ff` | constant | `03ff03ff` |
| `+0xa98` | — | no | `00600000` | `00600000` | size | `00800000` |
| `+0xa9c` | — | no | `ff178000` | `ff178000` | address | `fe2f0000` |
| `+0xad4` | — | no | `00000002` | `00000002` | frame, size | `00000001` |
| `+0xaf0` | — | no | `00000200` | `00000200` | constant | `00000200` |
| `+0xb00` | — | no | `00a0005a` | `00a0005a` | size | `00f00087` |
| `+0xb04` | `VE_ENC_AVC_PARA0_REG` | yes | `00000100` | `00000510` | frame | `00000100` |
| `+0xb08` | `VE_ENC_AVC_PARA1_REG` | yes | `0000081e` | `0000091f` | frame, QP, size | `00000c1e` |
| `+0xb10` | `VE_ENC_AVC_ME_PARA_REG` | yes | `08001200` | `08001200` | constant | `08001200` |
| `+0xb14` | `VE_ENC_AVC_INT_EN_REG` | yes | `1b020007` | `2c020007` | frame, QP, size, key period, bitrate | `32010007` |
| `+0xb1c` | `VE_ENC_AVC_STATUS_REG` | yes | `8000be60` | `8000fe60` | frame, QP, size, key period, bitrate | `8080be60` |
| `+0xb20` | `VE_ENC_AVC_PUTBITSDATA_REG` | yes | `00000007` | `00000057` | frame, QP | `00000007` |
| `+0xb2c` | `VE_ENC_AVC_RC_INIT_REG` | yes | `80001900` | `c0027627` | frame | `80001900` |
| `+0xb30` | `VE_ENC_AVC_RC_MAD_TH0_REG` | yes | `0a080604` | `1d1c1b1a` | frame | `0a080604` |
| `+0xb34` | `VE_ENC_AVC_RC_MAD_TH1_REG` | yes | `12100e0c` | `21201f1e` | frame | `12100e0c` |
| `+0xb38` | `VE_ENC_AVC_RC_MAD_TH2_REG` | yes | `1a181614` | `25242322` | frame | `1a181614` |
| `+0xb40` | `VE_ENC_AVC_RC_MAD_SUM_REG` | no | `74000014` | `74000014` | constant | `74000014` |
| `+0xb44` | `VE_ENC_AVC_TEMPORAL_FILTER_PAR_REG` | no | `0c1e0000` | `0c1e0000` | constant | `0c1e0000` |
| `+0xb48` | `VE_ENC_AVC_DYNAMIC_ME_PAR0_REG` | yes | `00300008` | `00300008` | size | `0048000c` |
| `+0xb4c` | `VE_ENC_AVC_DYNAMIC_ME_PAR1_REG` | yes | `010000d0` | `010000d0` | size | `01800138` |
| `+0xb54` | `VE_ENC_AVC_RESIDUAL_BITS_REG` | yes | `00000068` | `00000000` | frame, QP, size | `00000062` |
| `+0xb58` | `VE_ENC_AVC_HEADER_BITS_REG` | yes | `000000bd` | `0000000c` | frame, QP, size, key period, bitrate | `000000bc` |
| `+0xb5c` | `VE_ENC_AVC_ME_INFO_REG` | yes | `00040000` | `000000cc` | frame, QP | `00040000` |
| `+0xb60` | `VE_ENC_AVC_MV_BUF_ADDR_REG` | yes | `00ff3e80` | `00ff3e80` | address | `00fe7d00` |
| `+0xb70` | — | no | `00000000` | `ff000000` | frame | `00000000` |
| `+0xb74` | — | no | `22201e1c` | `29282726` | frame | `22201e1c` |
| `+0xb80` | `VE_ENC_AVC_STM_START_ADDR_REG` | yes | `00ff8000` | `00ff8000` | address | `00ff8000` |
| `+0xb84` | `VE_ENC_AVC_STM_END_ADDR_REG` | yes | `00ff9fff` | `00ff9fff` | address | `00ff9fff` |
| `+0xb8c` | `VE_ENC_AVC_STM_BIT_MAX_REG` | yes | `01000000` | `01020000` | frame | `01000000` |
| `+0xb90` | `VE_ENC_AVC_STM_BIT_LEN_REG` | yes | `00000198` | `0002ba60` | frame, QP, size, key period, bitrate | `00000190` |
| `+0xb9c` | — | no | `00ff3df0` | `00ff3df0` | address | `00fe7c70` |
| `+0xba0` | `VE_ENC_AVC_REF0_ADDR_Y_REG` | yes | `00ff6000` | `00ff6000` | address, frame | `00fec000` |
| `+0xba4` | `VE_ENC_AVC_REF0_ADDR_C_REG` | yes | `00ff6f00` | `00ff6f00` | address, frame | `00fee1c0` |
| `+0xbb0` | `VE_ENC_AVC_REC_ADDR_Y_REG` | yes | `00ff6000` | `00ff2000` | address, frame | `00fec000` |
| `+0xbb4` | `VE_ENC_AVC_REC_ADDR_C_REG` | yes | `00ff6f00` | `00ff2f00` | address, frame | `00fee1c0` |
| `+0xbb8` | `VE_ENC_AVC_SUBPIX_ADDR_LAST_REG` | yes | `00ff3f80` | `00ff3f80` | address, frame | `00fe7f00` |
| `+0xbbc` | `VE_ENC_AVC_SUBPIX_ADDR_NEW_REG` | yes | `00ff3f80` | `00ff3f00` | address, frame | `00fe7f00` |
| `+0xbc0` | `VE_ENC_AVC_MB_INFO_ADDR_REG` | yes | `00ff3e40` | `00ff3e40` | address | `00fe7cc0` |
| `+0xbc4` | `VE_ENC_AVC_DEBLK_ADDR_REG` | yes | `00ff3e00` | `00ff3e00` | address | `00fe7c80` |

## H.265: vendor register image (720p default, frame 0 = IDR, frame 1 = P)

| Offset | cedrus name | cedrus writes | frame 0 | frame 1 | varies with | 1080p frame 0 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `+0x800` | `VE_MODE_REG (top)` | yes | `c00000c7` | `c00000c7` | constant | `c00000c7` |
| `+0x804` | `VE_RESET_REG (top)` | yes | `00000100` | `00000100` | frame, QP, key period, bitrate | `00000100` |
| `+0x81c` | — | no | `00010000` | `00010000` | frame, size, bitrate | `00000000` |
| `+0x840` | — | no | `0000000f` | `0000000f` | constant | `0000000f` |
| `+0x880` | — | no | `00001c55` | `00001c55` | constant | `00001c55` |
| `+0x884` | — | no | `000fffff` | `000fffff` | constant | `000fffff` |
| `+0x888` | — | no | `00008000` | `00008000` | constant | `00008000` |
| `+0x8e0` | — | no | `00d33310` | `00d33310` | constant | `00d33310` |
| `+0x8e4` | — | no | `00e21320` | `00e21320` | constant | `00e21320` |
| `+0xa00` | `VE_ISP_PIC_INFO_REG` | yes | `00a0005a` | `00a0005a` | size | `00f00087` |
| `+0xa04` | `VE_ISP_PIC_STRIDE0_REG` | yes | `00500000` | `00500000` | size | `00780000` |
| `+0xa14` | — | no | `00000050` | `00000050` | size | `00000078` |
| `+0xa2c` | `VE_ISP_SCALER_SIZE_REG` | yes | `005a00a0` | `005a00a0` | size | `008700f0` |
| `+0xa78` | `VE_ISP_INPUT_LUMA_ADDR_REG` | yes | `ff200000` | `ff200000` | address | `fe000000` |
| `+0xa7c` | `VE_ISP_INPUT_CHROMA0_ADDR_REG` | yes | `ff080000` | `ff080000` | address | `fdf00000` |
| `+0xa80` | `VE_ISP_INPUT_CHROMA1_ADDR_REG` | no | `ff0b8400` | `ff0b8400` | address | `fdf7e900` |
| `+0xad4` | — | no | `00000002` | `00000001` | frame, QP, size, key period, bitrate | `00000001` |
| `+0xaf0` | — | no | `00000200` | `00000200` | constant | `00000200` |
| `+0xb00` | — | no | `00a0005a` | `00a0005a` | size | `00f00087` |
| `+0xb04` | `VE_ENC_AVC_PARA0_REG` | yes | `00000100` | `00000110` | frame | `00000100` |
| `+0xb08` | `VE_ENC_AVC_PARA1_REG` | yes | `001c081c` | `001c091e` | frame, size, bitrate | `001c0c1d` |
| `+0xb10` | `VE_ENC_AVC_ME_PARA_REG` | yes | `08c00000` | `08400000` | frame | `08c00000` |
| `+0xb14` | `VE_ENC_AVC_INT_EN_REG` | yes | `0a020007` | `12010007` | frame, QP, size, key period, bitrate | `11010007` |
| `+0xb18` | `VE_ENC_AVC_STARTTRIG_REG` | yes | `00030000` | `00030000` | constant | `00030000` |
| `+0xb1c` | `VE_ENC_AVC_STATUS_REG` | yes | `8082ae60` | `80028e60` | frame, QP, size, key period | `80022a60` |
| `+0xb20` | `VE_ENC_AVC_PUTBITSDATA_REG` | yes | `00000026` | `000000c0` | frame, size, bitrate | `00000036` |
| `+0xb2c` | `VE_ENC_AVC_RC_INIT_REG` | yes | `90133332` | `d001d8ad` | frame, size, bitrate | `901ffffe` |
| `+0xb30` | `VE_ENC_AVC_RC_MAD_TH0_REG` | yes | `0a080604` | `1d1c1b1a` | frame | `0a080604` |
| `+0xb34` | `VE_ENC_AVC_RC_MAD_TH1_REG` | yes | `12100e0c` | `21201f1e` | frame | `12100e0c` |
| `+0xb38` | `VE_ENC_AVC_RC_MAD_TH2_REG` | yes | `1a181614` | `25242322` | frame | `1a181614` |
| `+0xb3c` | `VE_ENC_AVC_RC_MAD_TH3_REG` | yes | `00000000` | `00ff0780` | frame | `00000000` |
| `+0xb40` | `VE_ENC_AVC_RC_MAD_SUM_REG` | no | `18000014` | `18000014` | constant | `18000014` |
| `+0xb44` | `VE_ENC_AVC_TEMPORAL_FILTER_PAR_REG` | no | `0c1e0084` | `0c1e0084` | constant | `0c1e0084` |
| `+0xb48` | `VE_ENC_AVC_DYNAMIC_ME_PAR0_REG` | yes | `00000000` | `00180004` | frame | `00000000` |
| `+0xb4c` | `VE_ENC_AVC_DYNAMIC_ME_PAR1_REG` | yes | `00000000` | `00800068` | frame | `00000000` |
| `+0xb5c` | `VE_ENC_AVC_ME_INFO_REG` | yes | `000006f4` | `000004d0` | frame, QP, size, key period, bitrate | `00000627` |
| `+0xb60` | `VE_ENC_AVC_MV_BUF_ADDR_REG` | yes | `00ff4400` | `00ff4400` | address, frame | `00fe5c00` |
| `+0xb64` | — | no | `00ff4400` | `00ff4300` | address, frame | `00fe5c00` |
| `+0xb68` | — | no | `016002df` | `00e00cd6` | frame, QP, size, key period, bitrate | `012c039e` |
| `+0xb6c` | — | no | `3cc80020` | `3cc80072` | frame | `3cc80020` |
| `+0xb70` | — | no | `ff000123` | `ff000518` | address, frame | `ff00016f` |
| `+0xb74` | — | no | `22201e1c` | `29282726` | frame | `22201e1c` |
| `+0xb80` | `VE_ENC_AVC_STM_START_ADDR_REG` | yes | `00ff8000` | `00ff8000` | address | `00ff8000` |
| `+0xb84` | `VE_ENC_AVC_STM_END_ADDR_REG` | yes | `00ff9fff` | `00ff9fff` | address | `00ff9fff` |
| `+0xb8c` | `VE_ENC_AVC_STM_BIT_MAX_REG` | yes | `01000000` | `01030000` | frame | `01000000` |
| `+0xb90` | `VE_ENC_AVC_STM_BIT_LEN_REG` | yes | `00000158` | `00032c90` | frame, QP, size, key period, bitrate | `00000138` |
| `+0xb94` | — | no | `00000020` | `00000072` | frame | `00000020` |
| `+0xb9c` | — | no | `00ff3af0` | `00ff3af0` | address | `00fe58f0` |
| `+0xba0` | `VE_ENC_AVC_REF0_ADDR_Y_REG` | yes | `00ff7000` | `00ff7000` | address, frame | `00fec000` |
| `+0xba4` | `VE_ENC_AVC_REF0_ADDR_C_REG` | yes | `00ff6800` | `00ff6800` | address, frame | `00feb000` |
| `+0xba8` | `VE_ENC_AVC_REF1_ADDR_Y_REG` | no | `00ff4280` | `00ff4280` | address | `00fe5900` |
| `+0xbb0` | `VE_ENC_AVC_REC_ADDR_Y_REG` | yes | `00ff7000` | `00ff5000` | address, frame | `00fec000` |
| `+0xbb4` | `VE_ENC_AVC_REC_ADDR_C_REG` | yes | `00ff6800` | `00ff4800` | address, frame | `00feb000` |
| `+0xbb8` | `VE_ENC_AVC_SUBPIX_ADDR_LAST_REG` | yes | `00ff3c80` | `00ff3c80` | address, frame | `00fe5f00` |
| `+0xbbc` | `VE_ENC_AVC_SUBPIX_ADDR_NEW_REG` | yes | `00ff3c80` | `00ff4580` | address, frame | `00fe5f00` |
| `+0xbc0` | `VE_ENC_AVC_MB_INFO_ADDR_REG` | yes | `00ff4240` | `00ff4240` | address | `00fe5880` |
| `+0xbc4` | `VE_ENC_AVC_DEBLK_ADDR_REG` | yes | `00ff4200` | `00ff4200` | address | `00fe5840` |

## Registers cedrus writes that the vendor leaves 0 (H.264)

`+0xb0c` `VE_ENC_AVC_PARA2_REG`, `+0xb18` `VE_ENC_AVC_STARTTRIG_REG`, `+0xb24` `VE_ENC_AVC_OVERTIME_MB_REG`, `+0xb28` `VE_ENC_AVC_CYCLIC_INTRA_REFRESH_REG`, `+0xb3c` `VE_ENC_AVC_RC_MAD_TH3_REG`, `+0xb50` `VE_ENC_AVC_MAD_REG`, `+0xb88` `VE_ENC_AVC_STM_BIT_OFFSET_REG`
