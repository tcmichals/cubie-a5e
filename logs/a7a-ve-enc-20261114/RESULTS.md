# A7A VE encoder, step (a) board test, 2026-11-14

Image: `bld.a7a/images/sdcard.img` (kernel `7.1.0 #2 SMP PREEMPT_RT Sat Nov 14 01:48:51 CST 2026`),
`linux-cubie` `a7a_remoteproc_camera_usb` `7452c982f912`, 26 MHz `osc24M` DTB. Serial `ttyUSB0` 115200.
Plan: `docs/common/A733_VE_H264_ENCODER_PLAN.md` §4 (a).

## Probe (cedrus auto-loaded at boot)

```
[    0.107296] sunxi-pck-600 7060000.power-controller: domain "VE_ENC" (id 3) pstate=0x8, on
[    2.897487] cedrus 1c10000.video-codec: readback: mod 400000000 Hz, top+0x0 000000c7 top+0x4 00000300 +0x8e4 00e21320 +0x8f0 00000000
[    2.897504] cedrus 1c10000.video-codec: readback: CCU 0x1b14 00020001 (PD status 2), sys 0x0 00000000 0x4 00000000
[    6.799689] sunxi-pck-600 7060000.power-controller: failed to turn domain "VE_ENC" off: -110
```

- **Pass:** version register `+0x8e4` = `0x00e21320` (non-zero; `libve` only rejects "both 0xf0 and
  0xe4 is 0"); `+0x8f0` = 0. PD status 2 (on), `0x1b14` bit 0 set. `0x03000000`/`0x03000004` both 0:
  not a "never started" candidate (plan §1.3).
- `v4l2-ctl --list-devices`: `cedrus-enc (platform:cedrus)` → `/dev/video1`, `/dev/media1`.

## Clocks (`clk_summary`, device suspended)

All five consumers named correctly (`ahb`, `mod`, `ram`, `ahb-master`, `mbus-master`).
**`ve-enc0` is on `pll-ve0` (400 MHz), not `pll-ve1` (696 MHz):** `assigned-clock-parents` is applied,
then cedrus `clk_set_rate(mod, 400 MHz)` re-parents (mux has `CLK_SET_RATE_PARENT`, no
`CLK_SET_RATE_NO_REPARENT`; pll-ve0 gives 400 MHz exactly). Works; the DT parent line is ineffective.
PLL regs: VE0 `0x02002220` = `0xF1203100`, VE1 `0x02002240` = `0xF1205600` (both enabled, locked).

## `VE_ENC` power-off `-110` (open; was already seen 2026-11-11)

genpd: `VE_ENC on`, device `suspended`. PPU VE_ENC at `0x07063000` (PCK-600 base `0x07060000`, domain 3):

| Reg | VE_ENC | VE_DEC (off OK) | Arm PPU meaning |
| --- | --- | --- | --- |
| `PWPR` +0x0 | `0x0` (OFF requested) | | power policy |
| `PWSR` +0x8 | `0x8` (ON) | `0x0` | power status |
| `DISR` +0x10 | **`0x1`** | `0x0` | device DEVACTIVE[0] asserted |
| `MISR` +0x14 | `0x100` | `0x0` | DEVACCEPT[0], no DEVDENY |
| `PWCR` +0x20 | `0x101` | | DEVREQEN[0], DEVACTIVEEN[0] |
| `IMR`/`AIMR`/`ISR` | `0x2`/`0x6`/`0x1` | ISR `0x1` | |
| `DCDR0/1` | `0x1F1F1F`/`0x1F1F` | | match vendor |
| `IDR0` +0xFB0 | `0x10130101` | | |

Register layout assumed from the Arm PPU architecture, not yet checked against the A733 UM.

Manual OFF requests (`PWPR` 8 → 0, read `PWSR` after 1 s), each still `0x8`:
1. clocks/reset as left by runtime suspend (`0xA80`=0, `0xA8C`=0, `0x5C0` bit 1 = 0, `0x5E0` bit 12 = 0, `0x5E4` bit 1 = 0);
2. + bus/MBUS/master gates on, reset released (`0x5C0`=`0x10010306`, `0x5E0`=`0xF1001003`, `0x5E4`=`0xA06`, `0xA8C`=`0x10001`);
3. + module clock on (`0xA80`=`0x80000000`, pll-ve0).

So clocks and reset are not the cause. Hypothesis: the encoder holds DEVACTIVE because ENC_TOP mode
(`+0x800`) is left at `0xc7` (encoder enable, set in `cedrus_resume()`, never cleared in suspend).

Test 4 (clear `0x01C10800`, retry OFF) **hung the board**: the read of `0x01C10800` was issued with
`PWPR` still at OFF from test 3; no console after that (needs a power cycle). Lesson: set `PWPR`
back to ON (`0x8`) and wait for `PWSR`=8 before touching the device after a manual OFF request.

Vendor: `bsp/drivers/pm_domain/pck600_domains.c` uses the same delays and the same command/poll with
a 10 ms timeout, but on timeout only logs and returns 0, so the vendor kernel may hit the same and hide it.

## Next

- After power cycle: test 4 done safely (`PWPR`=8 first), i.e. with the device resumed, write ENC_TOP
  mode 0 (and/or set global reset), then request OFF; watch `DISR`.
- If that is it: `cedrus_suspend()` clears ENC_TOP mode (A733 only) before gating clocks.
- Decide the `pll-ve0` vs `pll-ve1` question (drop the DT parent, or add `assigned-clock-rates` and stop
  `clk_set_rate()` re-parenting).
- Wi-Fi: the board's `/etc/wpa_supplicant.conf` has the network but no `ctrl_interface`; three
  `wpa_supplicant` instances were running (ifupdown + `S40network-wifi`); no association yet.

## After the power cycle: cause found and fixed (2026-11-14)

Safe order this time: `PWPR`=8 first (the boot-time failed OFF leaves `PWPR`=0), then
`power/control`=on to resume through the driver.

| Step | `MODE` +0x800 | `0x1b14` | `DISR` | Result |
| --- | --- | --- | --- | --- |
| resumed | `0xC7` | `0x00020001` | 1 | |
| ENC_TOP mode → 0 | `0x00` | `0x00020001` | **1** | hypothesis "mode reg" wrong |
| `0x1b14` bit 0 → 0 | `0x00` | `0x00020000` | **0** | DEVACTIVE released at once |
| `power/control`=auto | | | | genpd `VE_ENC off-0`, `PWSR`=0, no `-110` |
| resume again | `0xC7` | `0x00020000` | 0 | `PWSR`=8, `+0x8e4` still `0x00E21320` |
| auto again | | | | `PWSR`=0, no `-110` |

**Cause:** our CCU probe set `CM_VE_ENC_CFG_REG` bit 0 ("module mode", from `c9d1a8f3fb9c`); the
vendor kernel never writes it (reset value `0x2`). Set, it holds the VE_ENC PPU DEVACTIVE input.
**Fix:** `linux-cubie` `01e37bcf6fa7` drops the write.

Kernel `#3` (Image sha256 `d426eed2…eaa8`) copied to `/boot/Image` over Wi-Fi (old kernel kept as
`/boot/Image.bak-20261114`), `reboot` worked: readback `0x1b14 00020000`, no `-110` line, genpd
`VE_ENC off-0` with the device suspended, `PWSR` 0.

Board notes: login `root` with no password on the serial console after a cold boot. Wi-Fi: the SSID is
`TP-Link_4EA4_5G` (case matters); with that and a `ctrl_interface` line in `/etc/wpa_supplicant.conf`
the board joins 5 GHz and gets `192.168.0.194` by DHCP, also after `reboot`. Ethernet `eth0` is on a
different router (`192.168.3.4`). SSH from this PC needs a key on the board (not installed).

## Encoder clock on PLL_VE1 (vendor way), kernel #4

`CLK_SET_RATE_NO_REPARENT` on `ve-enc0` (`linux-cubie`, see the commit after `01e37bcf6fa7`). Board:
`ve-enc0` on `pll-ve1`, both 400 MHz; `0xA80` = `0x01000000` (mux 1 = PLL_VE1, gated while
suspended); `pll-ve0` 546 MHz unused; `VE_ENC off-0`, no `-110`. `ve-dec0` also sits on `pll-ve1`
(mux reset default) and moved to 400 MHz with it: pin it to PLL_VE0 when the decoder gets a node.

The `reboot` into #4 gave no console output at all (not even SPL) and no ping until a power cycle;
the earlier `reboot` into #3 worked. Cause not known (USB-serial node was re-created at 09:47); watch
the next `reboot`.

## Step (b)/(c) first run (IRQ path, no poll mode needed yet), kernel #4

`/tmp/grey720.nv12` = 1280×720 NV12, all bytes `0x80`. CAVLC, Main, QP 26/28 (defaults).

```
v4l2-ctl -d /dev/video1 --set-fmt-video-out=width=1280,height=720,pixelformat=NV12 \
  --set-fmt-video=pixelformat=H264,sizeimage=1048576 --stream-out-mmap --stream-mmap \
  --stream-count=5 --stream-loop --stream-from=grey720.nv12 --stream-to=out720.h264 --verbose
cap dqbuf: 0 seq: 0 bytesused: 1280 (keyframe)   # 10240 bits
cap dqbuf: 1 seq: 0 bytesused: 1604 (P-frame)    # same for the next 3 P frames
```

- **IRQ works:** `/proc/interrupts` `GICv3 80 Level cedrus` goes up by one per frame (0 → 1 → 6 → 11).
  So step (c) passes, and triage #8 (vendor A7Z: IRQ never fires) does not happen on mainline.
- **Engine runs:** `STM_BIT_LEN` (`+0xB90`) gives plausible sizes, constant for identical frames.
- **Output is all zeros:** `out720.h264` 7696 bytes of `0x00`, not even the SPS/PPS start codes.
  The bitstream DMA does not land in the buffer. Default `sizeimage` is 1024 (too small for a
  frame): pass `sizeimage=` until the driver sets a sane default.
- IOMMU0 gates on (`0x58C` = `0x00010007`). Master 1 (CSI): `MIC_AUTO_GATING`/`BYP`/`WBUF`/`TLB_EN`
  (`0x03900600/04/08/14`) all 1; bypass on, which is why CSI DMA works without an IOMMU driver.
- **Reading master 3 (`0x03900A00`…) hung the board** with VE_ENC suspended: master 3's micro-TLB is
  most likely in the VE_ENC power domain, which now turns off when idle. **Rule: touch VE_ENC
  registers and IOMMU0 master 3 registers only with the device resumed (`power/control`=on) and
  `PWSR`=8.**
- Next: resume, read master 3 `BYP` (`0x03900A04`); if 0, set 1 and re-encode.

## Output zeros: what is ruled out (after power cycle, device held resumed)

| Suspect | Readback | Verdict |
| --- | --- | --- |
| IOMMU0 master 3 translating | `0x03900A00/04/08/0C/14` = 1 (auto-gating, **bypass**, wbuf, ooo, tlb-en), `INT_STA`/err addr/data (`+0x38..0x44`) = 0 | not it: bypass, no fault (same as CSI master 1) |
| Bus address offset (old sunxi MBUS `PHYS_OFFSET`) | vendor `cedar_ve.c:998` `phymem_start = 0`; A733 not in `sunxi_mbus.c` | not it: physical addresses |
| Buffer above 4 GiB | input luma `0xA78` = `0xF3100000`, chroma `0xA7C` = `0xF31E1000`, STM `0xB80/0xB84` = `0xF3600000`/`0xF36FFFFF`, MB info `0xBC0` = `0xF3004000` | not it: all below 4 GiB, in RAM |
| Bus gates | `0x5C0`=`0x10010306` (bit 1), `0x5E0`=`0xF1001003` (bit 12), `0x5E4`=`0xA06` (bit 1), `0xA80`=`0x81000000`, `0xA8C`=`0x10001` | not it: all on, same bits as vendor `ccu-sun60iw2.c:593-708` |
| Engine not running | `STM_BIT_LEN` `0xB90` = `0x3220` (12832 bits), IRQ per frame | engine runs and counts bits |

`STM_BIT_MAX` `0xB8C` = `0x00800000` (1 MiB × 8). `devmem` cannot read RAM (`STRICT_DEVMEM`), so
whether bytes reach `0xF3600000` is not known from user space.

Still open, in order:
1. Do the bits reach DRAM? Kernel debug in `cedrus_enc_h264_job_finish()`: `memremap()` the coded
   buffer's DMA address (or drop `DMA_ATTR_NO_KERNEL_MAPPING` for the CAPTURE queue) and print the
   first 32 bytes. Zeros there = the engine's stream writer does not write to DRAM; data there = a
   CPU-mapping/sync bug in the driver path.
2. If zeros in DRAM: the A733 "H264 Ver2" core (plan §2.3) may need stream-writer setup the V3s-era
   WIP does not do (VCUENC `+0x900`, ENC_TOP bits): compare with the register dumps of a vendor-image
   encode (Radxa Debian, `libvencoder`), the one case where the vendor image is worth booting.
3. Driver default CAPTURE `sizeimage` is 1024: raise it (for example width × height × 3 / 2 / 2).

## The bits never reach DRAM (kernel-side dump), and the VCU

Debug module parameter `dump_coded` (`cedrus_enc_h264.c`, bring-up only), module only replaced on
the board (old one kept as `sunxi-cedrus.ko.bak-20261114`):

```
coded: dma 0x00000000f3500000 stm f3500000-f35fffff len 1280 vaddr yes
coded: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

STM start/end registers equal the buffer's DMA address; the kernel mapping reads zeros, including the
SPS/PPS that the CPU writes through put-bits. **The AVC stream writer does not write to DRAM.**

Vendor `ve_plat_sun60iw2.c:23-34,153-170` knows a VCU (`VCUENC` at `+0x900`, `VCU_CFG` = ENC_TOP
`+0x5c`; with `VCU_CFG` bit 0 set, the encoder completion comes from `VCUENC +0x28`). The WIP has no
VCU code. Readback with the device resumed: ENC_TOP `+0x800` `0xC7`, `+0x804` `0x300`, `+0x81C`
`0x00010000`, **`+0x85C` (VCU_CFG) `0`**, `+0x8E4` `0x00E21320`.

**Reading `0x01C10900` (VCUENC) hung the board even with the device resumed and `PWSR`=8** (third
hang today). The VCU is likely clock-gated while `VCU_CFG`=0. Rule: on the board, read only registers
the driver already programs; find the rest offline first.

Next (offline): disassemble the vendor `libvencoder`/`libve` objects
(`bsp/drivers/ve/cedar-ve/codec/lib/`, `ar` archives with symbols, e.g. `h264encVersion2.o`,
`vcu.o`) for writes to ENC_TOP `+0x5c` and `+0x9xx`, i.e. the setup the V3s-era WIP misses.

## Reads work, writes do not (after the 3rd power cycle)

`ddr_mode` (VE_MODE bits 17:16) = 3, 2, 1: no change, buffers still zero, same lengths. Not it.

Input content test (720p, 2 frames: I then P of the same picture, `dump_coded=1`):

| Input | I | P |
| --- | --- | --- |
| grey `0x80` | 1280 B | 1604 B |
| black `0x00` | 1297 B | 1621 B |
| `/dev/urandom` | 228961 B | 211470 B |

- Sizes follow the content: **input DMA reads work**, the core encodes.
- A P frame of an identical picture should be small; the noise P is 211 KB, so the reference
  (reconstruction, written then read back by the engine) is wrong too: **all encoder DMA writes are
  lost** (bitstream and reconstruction), reads are fine.
- Rules out everything that would break reads as well (clocks, PD, IOMMU translation, address offset,
  DDR mode). Left: write-side only, e.g. a write high-address/extension register of the A733 "Ver2"
  core (6 GiB DRAM; a non-zero default would send writes outside RAM, or into it), or a write-side
  bus/security setting for this master outside the VE block.

Offline so far: vendor `libvencoder`/`libve` objects are RISC-V (rt-media build) with symbols;
`/opt/riscv/bin/riscv32-unknown-elf-objdump` works on them. `h264ConfigEncoderRegs` /
`h264InitRegInfo` fill a shadow register image through nested structs (hours to map fully).
Cheaper next: list every `+0xBxx`/`+0xAxx`/`+0x8xx` offset the vendor shadow image covers and diff
against `cedrus_regs.h` for registers the WIP never writes (high-address / write-control candidates).
