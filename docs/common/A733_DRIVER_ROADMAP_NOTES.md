# A733 Driver Roadmap Notes (session 2026-11-14)

Findings and discussion that are not in the encoder or camera documents. Nothing here is an owner
decision unless marked so; decisions live in `upstream-remoteproc/DESIGN_DECISIONS.md`.

Related: `A733_VE_ENCODER_VENDOR_REGISTER_MAP.md` (encoder fix list), `A733_VE_H264_ENCODER_PLAN.md`,
`A733_VE_H265_ENCODER_PLAN.md`, `CAMERA_V4L2_GUIDE.md` §5 (ISP), `logs/a7a-ve-enc-20261114/RESULTS.md`.

## 1. Owner statements (2026-11-14)
- Goal: H.264 and then H.265 encode; all camera drivers including the IOMMU; ISP tuning must be
  something we can offer (not a closed blob).
- NPU is wanted, after first flight. GPU/desktop not needed.
- Next upstream driver: decide after the remoteproc v4 review, not before.
- Flight camera not chosen yet (see §5).

## 2. Vendor reference material on this PC
| What | Where |
| :--- | :--- |
| Radxa r6 image (Debian 11, kernel 5.15.147-21-a733), unpacked | `~/projects/cubie/radxa-r6/` (`r6.img`, `root.ext4`, `config`, `x/` extracted libs); download `~/Downloads/radxa-a733_bullseye_kde_r6.output_512.img.xz`; ~12 GB, deletable |
| Vendor BSP kernel source | `~/projects/cubie/oldkernels/linux-a733/` (`bsp/drivers/{ve,vin,iommu,pcie,phy,bus,clk}`, board DTs in `device-a733/configs/cubie_a7a|a7z/`) |
| Radxa open ISP userspace source (tuning headers, ISP server) | `~/projects/cubie/downloads/allwinner-isp6xx/src` (camera guide §5.6; licence unclear) |
| Orange Pi 4 Pro community A733 tree (vendor BSP drivers on 6.18; decoder-only cedrus patch; DP/combo-PHY fixes) | `~/projects/cubie/downloads/jonas5-a733/repo` |
| Vendor encoder runs and register snapshots | `logs/a7a-ve-enc-20261114/vendor-r6/` |

Vendor image use: flash `r6.img` with `dd` (card ≥ 6,776,878,080 bytes), console `ttyUSB0` 115200,
first boot ~70 s, login `radxa`/`radxa`, `sudo -i`. `STRICT_DEVMEM` is off. `demoVencoder` and the
codec libraries are preinstalled (`libcedarc-dev-2.0.0-arm64`). `ve_printf_register` in
`/etc/cedarc.conf` prints nothing for the encoder; use the `LD_PRELOAD` shim (`vendor-r6/vedump.c`).
Vendor sensors: imx214, imx219, imx415 only (no OV5647); camera overlays in `/boot/dtbo/*.disabled`.
The BSP RISC-V `libvencoder` archive is for another SoC (probably V821/sun300iw1, same VE family),
H.264/JPEG only; the A733's own libraries are the arm64 ones in the image.

## 3. State of A733 support elsewhere (checked 2026-11-14)
- Radxa forum: vendor camera/ISP problems dominate (IMX214 colour/ghosting, IMX415 4K stitched-mode
  chroma, IMX219 missing binned mode and gain); vendor encoder times out on camera dma-bufs unless
  `io-mode=2` (relevant to our step (e)); no image has GPU + NPU + correct CPU topology together.
  Threads: forum.radxa.com/t/…/31256, /29717, /31414, /30598.
- Armbian A733 builds use the vendor kernel (`sun60iw2-cubie-vendor`, 6.6.98-vendor); GPU, NPU and
  VPU excluded (armbian/build PR #10036).
- Mainline: clocks (Junhui Liu, v6 in review), pinctrl, DMA, PCK-600, U-Boot posted. Not posted by
  anyone: ISP, VE encoder, IOMMU v2, PCIe, combo PHY, NPU.

## 4. Driver order discussed (proposal, not decided)
| Order | Driver | Notes | Estimate |
| :--- | :--- | :--- | :--- |
| 1 | H.264 encoder | fix list in the register map doc | days |
| 2 | ISP Phase 1 + VIPP (NV12) | ISP has no output DMA: ISP0 → VIPP0 → CSI DMA0 | 1-2 weeks |
| 3 | IOMMU v2 | from GPL `bsp/drivers/iommu/sunxi-iommu-v2.c`; masters: CSI 1, ISP 2, VE_ENC 3; today all in bypass with contiguous buffers | 1-2 weeks |
| 4 | Camera → ISP → encoder dma-buf | needs 1-3 | days |
| 5 | H.265 Stage A (fixed setting, replayed registers) | H.265 plan | ~1 week |
| 6 | ISP parameters, statistics, libcamera AE/AWB, tuning | Phases 2-4 | several weeks |
| later | PCIe + Cadence combo PHY | see §6 | 3-6 weeks |
| after first flight | NPU (VIP9000) | | |

Upstream candidate after remoteproc (proposal): A733 CSI/MIPI capture path (already working).
The encoder cannot merge before Bootlin's cedrus encoder base does.

## 5. Camera modules (advice given, not verified on hardware)
- Recording/FPV: **IMX415** (Radxa Camera 4K): mainline `imx415`, vendor driver, and readable A733
  tuning (`imx415_mipi_1080p_a733.h`, `_3840_2160_a733.h`); overlay `cubie-a7a-imx415.dtso` exists.
  Rolling shutter: soft-mount, short exposure.
- Vision/NPU: global shutter, **IMX296** (colour, mainline `imx296`) or **OV9281** (mono, mainline
  `ov9282`). No vendor tuning. Connector/pinout for Radxa's 31-pin port not verified.
- OV5647: works with the ISP (raw Bayer in) and is fine for ISP Phase 1; no vendor tuning or
  reference, so tuning is ours.
- A7Z v2.00 has two 4-lane CSI ports: one of each is possible.

## 6. PCIe (A7A and A7Z both have it)
- Vendor DT: `pcie@6000000`, `allwinner,sunxi-pcie-v300-rc`, 1 lane, Gen3, SPI 152…, PHY
  `combo1_pcie` (`sun60iw2p1.dtsi:5002`). A7A board DT (`cubie_a7a/linux-5.15/board.dts:1031`):
  power PE11, wake PE12, reset PE13, 1.8 V `bldo1`, 3.3 V `dcdc1`. A7Z: PD20/PD21/PD22.
- Controller is Synopsys DesignWare (standard iATU registers in `pcie-sunxi.h`): mainline needs
  only a glue driver on `drivers/pci/controller/dwc/`. Clocks (`pcie-aux` `0x1380`, `pcie-axi-slv`
  `0x1384`, `serdes-phy` `0x13c0`, gates) are in our CCU; `PCIE` power domain is in the PCK-600 driver.
- The hard part: Cadence combo PHY (`allwinner,cadence-combophy`, `0x06C00000`, vendor driver
  4,372 lines), shared by USB3/PCIe/DP; no mainline driver. On the A7A `xhci2` uses `combo1_usb`
  unless `TYPEC_DP`: USB3 and PCIe may be mutually exclusive on combo PHY 1 (check the schematic).
- Nobody has posted a mainline A733 PCIe or PHY driver. Closest: A523 USB3/PCIe PHY series
  (Mikhail Kalashnikov, 2025-08, USB3 only so far).

## 7. ISP tuning procedure (to offer openly)
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

## 8. Ways to work faster
1. A second SD card (vendor image on one, ours on the other).
2. A cedrus debug dump of `+0x800-0x8FF` and `+0xA00-0xBFF` per frame in the vendor snapshot layout,
   then `analyze.py`-style diff against `vendor-r6/sweep/`.
3. Replay the vendor register image for one fixed setting first, then generalise field by field.
4. SSH/Wi-Fi on our image instead of base64 over serial (owner has to add the key; our image also
   needs the `/etc/wpa_supplicant.conf` fix redone after a reflash).
5. An IMX415 or IMX219 module for a vendor ISP reference (registers while streaming + video).

## 9. Board rules learned
- Read VE_ENC / ISP / IOMMU-master registers only with that device's power domain on and clocks
  running; never read VE_ENC `+0x900` (VCU). Three hangs came from breaking this.
- Buildroot output dirs (`bld.a7a`) are build-only; `/tmp` on this PC is a 3.6 GB RAM disk with a
  quota: unpack large images under `~/projects/cubie/`.
