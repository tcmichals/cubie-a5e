# 🚀 Platform Guide: Radxa Cubie A7A (Allwinner A733 / `sun60iw2`)

This document is the dedicated hardware, bootloader, and bring-up specification for the **Radxa Cubie A7A** flight controller. It details SoC architecture, out-of-tree status, LPDDR5 DRAM training, GICv3 interrupt controller configuration, and all verified fixes.

---

## 1. Hardware Architecture Specification

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **SoC** | Allwinner A733 (`sun60iw2p1`) | 2× Cortex-A76 (Big) + 6× Cortex-A55 (LITTLE) DynamIQ Cluster |
| **RAM** | 6 GiB LPDDR5 (400 MHz to 2400 MHz) | Multi-PState dynamic training executed by `boot0` |
| **Interrupt Controller** | ARM GIC-600 (GICv3) | Distributor: `0x03400000`, Redistributors: `0x03460000` |
| **Mainline Kernel Status** | **Out-of-Tree** | Supported via patch `0001-arm64-dts-allwinner-add-sun60i-a733-cubie-a7a.patch` |
| **Bootloader Stack** | **Vendor-Extracted Blob** | 16 MB `radxa_a733_bootloader.bin` (contains `boot0` + BL31 + U-Boot 2018.07) |
| **Wi-Fi / BT Transport** | **USB 2.0 High-Speed** | `0xA69C:0x8800` (Uses standalone `aic8800_fdrv.ko`) |
| **BROM Boot Geometry** | Sector 256 / Offset 128 KiB | Skips sectors 1–33 to maintain full GPT partition table compatibility |

---

## 2. Boot Flow Architecture

```
   ┌──────────────────────────────────────────────────────────┐
   │ 1. BootROM (BROM in silicon ROM)                         │
   │    • Reads boot0 from SD card offset 128 KiB (Sector 256)│
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 2. boot0 (First-Stage Bootloader in internal SRAM)       │
   │    • Configures PMIC, sets DRAM_VCC = 560 mV             │
   │    • Dynamically trains LPDDR5 PStates (400 -> 2400 MHz) │
   │    • Validates 6144 MB RAM integrity (Result = 7)        │
   │    • Loads BL31 (0x48000000) & U-Boot (0x4A000000) in RAM│
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 3. ARM Trusted Firmware BL31 (Vendor sun60i)             │
   │    • Configures GICv3 system registers (ICC_SRE_EL1.SRE=1)│
   │    • Sets up secure monitor & PSCI v0.2 SMC services     │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 4. Vendor U-Boot 2018.07 (BL33 in LPDDR5)                │
   │    • Requires pre-existing /memory node in DTB           │
   │    • Executes boot.scr -> Loads Linux 7.1 Image & DTB    │
   └─────────────────────────────┬────────────────────────────┘
                                 │
                                 ▼
   ┌──────────────────────────────────────────────────────────┐
   │ 5. Mainline Linux 7.1 PREEMPT_RT Kernel                  │
   │    • GICv3 root IRQ domain                               │
   │    • XuanTie E907 remoteproc co-processor                │
   │    • Standalone USB AIC8800 wireless driver              │
   └──────────────────────────────────────────────────────────┘
```

---

## 3. Platform-Specific Quirks, Pitfalls & Verified Fixes

### Quirk 1: GICv2 vs GICv3 Firmware Panic
- **Problem**: The original vendor DTS declared legacy GICv2 (`compatible = "arm,cortex-a15-gic"` at `0x03021000`), but ATF BL31 enabled GICv3 system registers (`ICC_SRE_EL1.SRE = 1`). Linux 7.1 panicked at `irq-gic.c:57`.
- **Fix**: Replaced with native GICv3 distributor (`0x03400000`) and redistributor (`0x03460000`) mapping in `sun60i-a733-cubie-a7a.dts`, preserving `phandle = <0x9b>`.

### Quirk 2: Do NOT Include `sun55i-a523.dtsi` on A733
- **Problem**: Attempting to clean up the A733 DTS by including `sun55i-a523.dtsi` caused a silent hang at BL3-1 because the A523 CCU clock registers and CPU topology starved the A733 hardware of clocks.
- **Fix**: Maintain the complete 2,564-line native `sun60i` hardware device tree directly.

### Quirk 3: Vendor U-Boot 2018.07 `/memory` Query
- **Problem**: Vendor U-Boot queries for an *existing* `/memory` node with libfdt; if absent, it throws `FDT_ERR_NOTFOUND` and does not insert memory banks, causing Linux to see 0 MB RAM.
- **Fix**: Hardcode base 6 GiB memory definition in the DTS:
  ```dts
  memory@40000000 {
      device_type = "memory";
      reg = <0x00 0x40000000 0x01 0x80000000>;
  };
  ```

### Quirk 4: `post-image.sh` Stale DTB Overwrite
- **Problem**: `board/radxa/cubie_a7a/post-image.sh` was copying an outdated binary over the freshly compiled DTB.
- **Fix**: Removed the stale `cp -f` line in `post-image.sh`.

---

---

## 4. Engineering Status & Roadmap: Mainline Upstream Patch Integration

### Upstream Mainline Status & Patch Series Discovery

While the base Linux kernel does not yet have stable out-of-the-box A733 drivers, active patch series are currently under review in the `linux-sunxi`, `linux-clk`, and U-Boot communities to bring full upstream support:

| Subsystem / Driver | Author & Patch Series | Current Status | Integration in Our Stack |
| :--- | :--- | :--- | :--- |
| **Clock Controller (CCU & PRCM)** | Junhui Liu (`clk: sunxi-ng: Add support for Allwinner A733 CCU and PRCM`) | Active Review (v2/v7) on `linux-clk` & `lore.kernel.org` | Pull `ccu-sun60i-a733.c` & headers into `project-cubie-a5e/patches/linux/` |
| **RTC & Main Oscillator** | Jerome Brunet & Chen-Yu Tsai (`clk: sun6i-rtc: Add support for Allwinner A733 SoC`) | Queued for Linux 7.3 merge window (`clk-next`) | Included via upstream `sun6i-rtc` driver patches |
| **Pin Controller (Pinctrl)** | Yixun Lan (`pinctrl: sunxi: a733: add initial support`) | Active Review (v2) on `linux-sunxi` | Pull `pinctrl-sun60i-a733.c` into `project-cubie-a5e/patches/linux/` |
| **U-Boot Base SoC Support** | Yixun Lan (`sunxi: Add support for A733 SoC`) | Active Review (v2) on U-Boot Patchwork | Basic UART/MMC pinctrl; in review |
| **LPDDR5 DRAM Dynamic Training** | Closed vendor `boot0` sequence | Upstream Work In Progress | Handled by `radxa_a733_bootloader.bin` (Hybrid Boot) |

### Strategic Architecture: The Hybrid Mainline Approach

Rather than waiting 3–6 months for these patches to land in stable kernel releases, we leverage Buildroot's patch overlay mechanism:

1. **Hybrid Bootloader**: We use `radxa_a733_bootloader.bin` (vendor `boot0` + BL31 + U-Boot 2018.07) to initialize the complex LPDDR5 multi-PState memory controller.
2. **Mainline Linux 7.1 Kernel + Patches**: Buildroot applies the `ccu-sun60i-a733` and `pinctrl-sun60i-a733` patch series directly against Linux 7.1 `PREEMPT_RT`.
3. **Automated Upstream Patch Tracking**: We maintain a dedicated tool (`tools/watch_a733_upstream.py`) to query `lore.kernel.org` and U-Boot Patchwork for new patch revisions (v3, v4, etc.) so we can update our local patch tree cleanly as upstream stabilizes.

---

## 5. The Step-by-Step Implementation Path for A7A Mainline Bring-Up

Here is the exact blueprint to execute this integration:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. INGEST KERNEL PATCHES                                                    │
│    • Add 0003-clk-sunxi-ng-add-allwinner-a733-ccu-and-prcm.patch            │
│      (drivers/clk/sunxi-ng/ccu-sun60i-a733.c, ccu-sun60i-a733-r.c)          │
│      (include/dt-bindings/clock/sun60i-a733-ccu.h, reset/sun60i-a733-ccu.h) │
│    • Add 0004-pinctrl-sunxi-add-allwinner-a733-pinctrl.patch                │
│      (drivers/pinctrl/sunxi/pinctrl-sun60i-a733.c, pinctrl-sun60i-a733-r.c) │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. ENABLE KERNEL KCONFIG SYMBOLS                                            │
│    • Update project-cubie-a5e/board/radxa/cubie_a7a/linux.config:           │
│      CONFIG_SUNXI_CCU=y                                                     │
│      CONFIG_SUN60I_A733_CCU=y                                               │
│      CONFIG_SUN60I_A733_R_CCU=y                                             │
│      CONFIG_PINCTRL_SUNXI=y                                                 │
│      CONFIG_PINCTRL_SUN60I_A733=y                                           │
│      CONFIG_PINCTRL_SUN60I_A733_R=y                                         │
│      CONFIG_SERIAL_8250_SUNXI=y                                             │
│      CONFIG_MMC_SUNXI=y                                                     │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 3. MODERNIZE DEVICE TREE (sun60i-a733-cubie-a7a.dts)                        │
│    • Remove legacy 400-line `clocks { compatible = "allwinner,clk-init"; }` │
│    • Add clean CCU nodes: `ccu: clock-controller@2001000` and `r_ccu`       │
│    • Add standard PIO nodes: `pio: pinctrl@2000000` and `r_pio`             │
│    • Link peripheral phandles (UART0, MMC0, MMC2, USB, GMAC0, SPI0, I2C1)   │
│      to `<&ccu CLK_...>`, `<&ccu RST_...>`, and `<&pio ...>`                 │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 4. COMPILE & FLASH SD CARD IMAGE                                            │
│    • In bld.a7a: `make cubie_a7a_defconfig && make`                         │
│    • Produces complete bootable `images/sdcard.img`                         │
│    • Flash to MicroSD: `sudo dd if=images/sdcard.img of=/dev/sdX bs=4M`     │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 5. CONTINUOUS UPSTREAM MONITORING                                           │
│    • Run `python3 tools/watch_a733_upstream.py` to check for new v3/v4      │
│      revisions or upstream git pull requests on lore.kernel.org             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Phase 2: Upgrading to Mainline U-Boot 2026.x (Post-Linux Boot)

Once Linux 7.1 is verified booting to userspace on real hardware via the kernel CCU/Pinctrl patches, we execute **Phase 2: Modernizing the Bootloader** to eliminate the legacy vendor U-Boot 2018.07.

### Architecture: Decoupling SRAM DRAM Training from U-Boot Proper

```
┌─────────────────────────────────────────────────────────────┐
│ STAGE 1: First-Stage Bootloader (boot0 in SRAM at 128KB)    │
│ • Powers PMIC rails (AXP) & trains 6 GiB LPDDR5 RAM PHY     │
└──────────────────────────────┬──────────────────────────────┘
                               │ (RAM initialized & active)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ STAGE 2: Modern ARM Trusted Firmware (TF-A BL31 in DRAM)    │
│ • Configures GICv3 system registers & PSCI v0.2 services    │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ STAGE 3: Modern Mainline U-Boot 2026.x (BL33 in DRAM)       │
│ • Pure upstream U-Boot 2026.01 running in AArch64 EL2       │
│ • Native extlinux, modern DT overlays (fdt apply), Fastboot │
└─────────────────────────────────────────────────────────────┘
```

### Upstream U-Boot A733 Patch Series (Yixun Lan / Series 491919)

The following 10-patch series from U-Boot Patchwork (`patchwork.ozlabs.org`) provides native A733 support in U-Boot:

| Patch ID | Subject / Functional Area | Target Source Files in U-Boot |
| :--- | :--- | :--- |
| **`[01/10]`** | **SoC Architecture Base** | `arch/arm/mach-sunxi/Kconfig`, `include/configs/sun60i.h` |
| **`[02/10]`** | **SPL Text/Stack Alignment** | `include/configs/sunxi-common.h`, `arch/arm/mach-sunxi/spl.c` |
| **`[03/10]`** | **CCU Clocks & Reset** | `drivers/clk/sunxi/clk_a733.c`, `drivers/reset/reset-sunxi.c` |
| **`[04/10]`** | **Pinctrl Driver** | `drivers/pinctrl/sunxi/pinctrl-sun60i-a733.c` |
| **`[05/10]`** | **GPIO Controller & PIO_OFFSET** | `drivers/gpio/sunxi_gpio.c` (adds 0x80 port offset) |
| **`[06/10]`** | **MMC / SD / eMMC Driver** | `drivers/mmc/sunxi_mmc.c` |
| **`[07/10]`** | **AXP318W / AXP717 PMIC** | `drivers/power/pmic/axp318w.c` |
| **`[08/10]`** | **Base SoC Device Tree** | `arch/arm/dts/sun60i-a733.dtsi` |
| **`[09/10]`** | **Radxa Cubie A7A Board DTS** | `arch/arm/dts/sun60i-a733-cubie-a7a.dts` |
| **`[10/10]`** | **Defconfig Definition** | `configs/radxa_cubie_a7a_defconfig` |

### Buildroot Configuration Blueprint for U-Boot 2026.01

To enable mainline U-Boot once Phase 1 is validated:

1. **Place Patches in Buildroot Tree:**
   ```bash
   mkdir -p project-cubie-a5e/patches/uboot/
   # Drop patches 0001-0010 into project-cubie-a5e/patches/uboot/
   ```
2. **Enable U-Boot in `cubie_a7a_defconfig`:**
   ```kconfig
   BR2_TARGET_ARM_TRUSTED_FIRMWARE=y
   BR2_TARGET_ARM_TRUSTED_FIRMWARE_CUSTOM_GIT=y
   BR2_TARGET_ARM_TRUSTED_FIRMWARE_PLATFORM="sun60i"
   BR2_TARGET_ARM_TRUSTED_FIRMWARE_BL31=y

   BR2_TARGET_UBOOT=y
   BR2_TARGET_UBOOT_BUILD_SYSTEM_KCONFIG=y
   BR2_TARGET_UBOOT_CUSTOM_VERSION=y
   BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2026.01"
   BR2_TARGET_UBOOT_BOARD_DEFCONFIG="radxa_cubie_a7a"
   BR2_TARGET_UBOOT_NEEDS_DTC=y
   BR2_TARGET_UBOOT_NEEDS_PYLIBFDT=y
   BR2_TARGET_UBOOT_NEEDS_OPENSSL=y
   BR2_TARGET_UBOOT_NEEDS_ATF_BL31=y
   BR2_TARGET_UBOOT_PATCH="$(BR2_EXTERNAL_CUBIE_A5E_PATH)/patches/uboot"
   ```
3. **Packaging with `boot0` Header (`post-image.sh`):**
   `post-image.sh` embeds `boot0` at 128KB (Sector 256) for DRAM training and packages the compiled `u-boot.itb` (BL31 + U-Boot 2026.01) at Sector 24576 (~12.6 MB).

---

## 7. Automated Upstream Patch Watcher Tool

To inspect the latest patches and check if newer revisions exist:

```bash
python3 tools/watch_a733_upstream.py
```

This queries:
- `lore.kernel.org/linux-sunxi` (A733 / sun60i kernel patches)
- `lore.kernel.org/linux-clk` (CCU and clock controller pull requests)
- `patchwork.ozlabs.org` (U-Boot A733 series 491919)

---

## 8. Build Commands for A7A Reference

```bash
# In build directory (e.g. bld.a7a)
make cubie_a7a_defconfig
make
```
- Output DTB: `images/sun60i-a733-cubie-a7a.dtb` (`41,583 bytes`)
- Output Disk Image: `images/sdcard.img` (`620,756,992 bytes`)

### Flashing SD Card
```bash
sudo dd if=images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```
