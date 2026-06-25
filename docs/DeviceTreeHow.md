# DeviceTreeHow

This guide explains how device tree and overlays work in this project — written so someone new to embedded Linux can follow along.

## What is a device tree?

A device tree is a text file that tells the Linux kernel what hardware is on the board — which buses exist, what chips are connected, what pins they use, memory addresses, interrupt lines, etc.

It gets compiled from a `.dts` text file into a binary `.dtb` file that U-Boot loads and passes to the kernel at boot.

You do not need to understand every line of a device tree to work with this project — you only need to know which file to edit and how changes flow through to the running system.

## What is an overlay?

An overlay (`.dtbo`) is a small patch to an existing device tree. Instead of editing the main board `.dts` file, you write a small additional file that describes only the changes you need.

Think of it like a Git patch: the base DTB is the original, the overlay is the diff, and U-Boot applies the diff in memory before handing the result to the kernel.

This is how this project enables SPI buses, I2C buses, and custom nodes without touching the upstream Allwinner board file.

## How it works in plain steps

1. Buildroot compiles the kernel and produces `sun55i-a527-cubie-a5e.dtb` (base board tree)
2. Buildroot compiles your overlay source (`cubie-a5e-flight-stack.dts`) into `cubie-a5e-flight-stack.dtbo`
3. Both files are copied into the FAT boot partition of `sdcard.img`
4. At power-on, U-Boot reads `boot.scr`, loads both files into RAM, merges them, then boots the kernel
5. The kernel starts up seeing one clean merged device tree — it never knows an overlay was involved

## Editing the overlay — what to edit and where

The overlay source file is:

- `project-cubie-a5e/overlays/cubie-a5e-flight-stack.dts`

Open it in any text editor. It is plain text.

Each block targets a named node from the base device tree using `&nodename` syntax:

```dts
/dts-v1/;
/plugin/;         /* marks this file as an overlay, not a full DTS */

&spi0 {           /* target the existing spi0 node in the base tree */
    status = "okay";   /* enable it */
    /* add child nodes here for devices on this bus */
};
```

After editing:

1. Run a Buildroot rebuild — the `.dts` is recompiled to `.dtbo` automatically
2. Flash the new `sdcard.img` to SD card
3. Boot and check `/proc/device-tree` or `dmesg` to verify your changes took effect

Do **not** edit files under `buildroot/` — those are upstream source and will be overwritten on update.

## Why overlays are used here

Primary goal:

- keep the upstream/base board DTB unchanged
- place project-specific hardware changes in an overlay (`.dtbo`)

In this tree, that means:

- base DTB: `sun55i-a527-cubie-a5e.dtb` (from kernel intree DTS)
- project overlay: `cubie-a5e-flight-stack.dtbo` (from external tree)

This is the safer maintenance model: fewer merge conflicts and easier upstream kernel updates.

## Source files involved

- Base DT configuration:
  - `project-cubie-a5e/configs/cubie_a5e_defconfig`
  - `BR2_LINUX_KERNEL_INTREE_DTS_NAME="allwinner/sun55i-a527-cubie-a5e"`

- Overlay source:
  - `project-cubie-a5e/overlays/cubie-a5e-flight-stack.dts`

- Overlay build hook:
  - `BR2_LINUX_KERNEL_DTB_OVERLAYS="$(BR2_EXTERNAL_CUBIE_A5E_PATH)/overlays/cubie-a5e-flight-stack.dts"`

- Boot script source:
  - `project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd`

- Image layout:
  - `project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg`

- Post-image packaging:
  - `project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh`

## Overlay approach: U-Boot `fdt apply` (not kernel overlays)

There are three common overlay approaches. This project uses **U-Boot `fdt apply`**:

| Approach | Who applies overlay | Complexity | Used here? |
|---|---|---|---|
| Raspberry Pi | VideoCore firmware + `config.txt` | Simple, firmware-driven | No |
| BeagleBone kernel configfs | Running Linux kernel (`/sys/kernel/config/device-tree/overlays/`) | High — requires `CONFIG_OF_OVERLAY` + `configfs` in kernel | No |
| **U-Boot `fdt apply`** | **U-Boot before kernel boots** | **Simple — kernel sees merged tree** | **Yes** |

### How BeagleBone does it (for reference)

BeagleBone (and BeaglePlay) load overlays at **runtime** using the Linux kernel's `configfs` overlay interface:

- Kernel is compiled with `CONFIG_OF_OVERLAY=y` and `CONFIG_CONFIGFS_FS=y`
- After boot, overlays are applied by writing `.dtbo` files into `/sys/kernel/config/device-tree/overlays/<name>/`
- Enables hot-loading and hot-removing overlays at runtime without rebooting

This is powerful for development but adds complexity:
- Kernel must be patched/configured for overlay support
- Overlay compatibility with running kernel state can cause failures
- Not needed for a flight controller where hardware config is fixed at build time

### Why we use U-Boot `fdt apply` instead

- Simpler: no extra kernel config needed (`CONFIG_OF_OVERLAY` not required)
- Deterministic: overlay is applied once at boot, before any userspace runs
- Safe for flight use: hardware topology is fixed and verified before kernel starts
- Kernel receives one clean merged DTB — no runtime overlay management needed
- Aligns with how Allwinner/Sunxi mainline boards are typically brought up

The kernel in this project receives a fully-merged device tree. It never knows overlays were involved. No special kernel config is needed for overlays.

## U-Boot requirements for overlay support

For `fdt apply` to work, U-Boot must be built with:

- `CONFIG_OF_LIBFDT_OVERLAY=y` — enables in-memory DTB overlay merging
- `CONFIG_CMD_FDT=y` — enables the `fdt` command in U-Boot shell

Both are set in:

- `project-cubie-a5e/board/radxa/cubie_a5e/u-boot-fragment.config`

Without these, `fdt apply` silently fails or is not available and the kernel boots with the base DTB only.

## Boot-time DT/overlay flow

At boot, U-Boot executes `boot.scr` (compiled from `boot.cmd`):

```
# 1. Load base DTB from FAT boot partition into memory
load mmc 0:1 ${fdt_addr_r} sun55i-a527-cubie-a5e.dtb

# 2. Load overlay DTBO from FAT boot partition into memory
load mmc 0:1 ${ramdisk_addr_r} cubie-a5e-flight-stack.dtbo

# 3. Point fdt command at base DTB, expand for overlay data, apply overlay
#    8192 = 8 KB headroom for overlay properties
#    If overlay is large, increase this value (see Troubleshooting)
fdt addr ${fdt_addr_r}
fdt resize 8192
fdt apply ${ramdisk_addr_r}

# 4. Load kernel and boot — kernel receives fully merged FDT, never sees overlay
load mmc 0:1 ${kernel_addr_r} Image
booti ${kernel_addr_r} - ${fdt_addr_r}
```

Key points:

- Everything happens in RAM — no files are modified on disk.
- `fdt resize 8192` pre-expands the in-memory DTB by 8 KB to accommodate overlay properties.
- After `fdt apply`, the base DTB address (`${fdt_addr_r}`) contains the fully merged tree.
- The kernel receives only the merged tree via `booti`. No kernel-side overlay support needed.
- `boot.scr` is generated from `boot.cmd` by `mkimage` in `post-image.sh`.

## `sdcard.img` layout and where DT files live

From `genimage.cfg`:

- `u-boot` partition/image at offset `8K`
  - contains `u-boot-sunxi-with-spl.bin`
- `boot` partition (FAT32) at offset `1M`, bootable
  - contains:
    - `sun55i-a527-cubie-a5e.dtb`
    - `cubie-a5e-flight-stack.dtbo`
    - `boot.scr`
    - `Image`
- `rootfs` partition (ext4)
  - contains the Linux root filesystem

Generated image:

- `bld/images/sdcard.img`

## How to add a new overlay

Recommended workflow:

1. Create a new overlay DTS file in external tree, e.g.:
   - `project-cubie-a5e/overlays/my-feature.dts`

2. Add the overlay path in defconfig (`BR2_LINUX_KERNEL_DTB_OVERLAYS`), space-separated if multiple overlays are supported in your Buildroot/kernel version.

3. Ensure the resulting `.dtbo` gets packaged into boot partition.
   - If needed, add file entry in `genimage.cfg` under `boot.vfat.files`.

4. Update boot logic in `boot.cmd` to load and apply the new `.dtbo`.
   - For multiple overlays, load/apply each in deterministic order.

5. Rebuild image and flash:
   - re-run Buildroot build
   - write updated `sdcard.img`

## How to modify existing overlay safely

- Edit only:
  - `project-cubie-a5e/overlays/cubie-a5e-flight-stack.dts`
- Avoid modifying base intree DTS unless absolutely required.
- Keep each hardware domain isolated in overlay blocks (`&i2cX`, `&spiX`, etc.).
- Keep overlay changes minimal and commented.

## Current overlay intent in this repo

`cubie-a5e-flight-stack.dts` currently demonstrates enabling/declaring:

- `i2c3`
- `spi0` (example IMU node)
- `spi1` (example FPGA node)

This matches the architecture goal: SPI-based FPGA offload + sensor bus enablement without changing base board DT source.

## Practical checks on target

After boot:

- verify merged nodes are visible in `/proc/device-tree`
- verify devices appear (`/dev/spidev*`, `/dev/i2c-*`, etc. depending on drivers/config)
- verify pin/peripheral behavior with:
  - `i2c-tools`
  - `spi-tools`
  - `libgpiod` utilities

## Troubleshooting

- `boot.scr` is generated from `boot.cmd` by `mkimage` in `post-image.sh`. If boot scripts seem stale, check that `post-image.sh` ran.
- If `fdt apply` fails in U-Boot, the kernel will still boot but with the **base DTB only** — your overlay changes will be silently missing. Check U-Boot serial output for `fdt apply` error messages.
- If a peripheral device node is missing after boot, check `/proc/device-tree` to see if the node was merged in.
- Overlay ordering matters when multiple overlays touch the same nodes or properties — apply in dependency order.
- If you see `fdt: FDT_ERR_NOSPACE`, increase the `fdt resize` value in `boot.cmd` (e.g. `fdt resize 16384`).

## Quick reference — which file to edit for what

| What you want to change | File to edit |
|---|---|
| Enable/disable a peripheral (SPI, I2C, etc.) | `project-cubie-a5e/overlays/cubie-a5e-flight-stack.dts` |
| Add a new device node (IMU, sensor, FPGA) | `project-cubie-a5e/overlays/cubie-a5e-flight-stack.dts` |
| Add a whole new overlay file | Add `.dts` to `overlays/`, update `defconfig` + `genimage.cfg` + `boot.cmd` |
| Change kernel boot arguments | `project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd` |
| Change what goes into the boot FAT partition | `project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg` |
| Change U-Boot build options | `project-cubie-a5e/board/radxa/cubie_a5e/u-boot-fragment.config` |
| Change kernel config options | `project-cubie-a5e/board/radxa/cubie_a5e/linux.config` |

---

# Deep dive — for those who want to understand every bit

> Everything below assumes you have read the plain-language sections above. This section covers the mechanics in detail: memory layout, U-Boot internals, FDT binary format, and the exact call chain.

## Full boot chain with DT flow

```
SD card on-disk layout
───────────────────────────────────────────────────────────────
  offset 8K   : u-boot-sunxi-with-spl.bin   (SPL + U-Boot)
  offset 1M   : boot.vfat (FAT32 partition)
                  ├── Image                  (kernel, uncompressed AArch64)
                  ├── sun55i-a527-cubie-a5e.dtb   (base board DTB)
                  ├── cubie-a5e-flight-stack.dtbo  (overlay binary)
                  └── boot.scr              (compiled U-Boot script)
  after boot  : rootfs.ext4 (ext4 partition)
───────────────────────────────────────────────────────────────

Power-on reset
      │
      ▼
┌─────────────────────────────┐
│  BootROM (on-chip)          │  reads SPL from SD offset 8K
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  SPL (Secondary Program     │  minimal init: DRAM, clocks
│  Loader, inside u-boot-     │  loads full U-Boot into DRAM
│  sunxi-with-spl.bin)        │
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  ARM Trusted Firmware BL31  │  EL3 secure monitor
│  (loaded by SPL via ATF)    │  stays resident for PSCI calls
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  U-Boot (full)              │
│  reads boot.scr from FAT   │◄── CONFIG_CMD_FAT, CONFIG_FS_FAT
│  executes script commands  │
└────────────┬────────────────┘
             │  load mmc 0:1 ${fdt_addr_r} sun55i-a527-cubie-a5e.dtb
             │  load mmc 0:1 ${ramdisk_addr_r} cubie-a5e-flight-stack.dtbo
             │  fdt addr / fdt resize / fdt apply  ◄── CONFIG_OF_LIBFDT_OVERLAY
             │  load mmc 0:1 ${kernel_addr_r} Image
             │  booti                              ◄── CONFIG_CMD_BOOTI
             ▼
┌─────────────────────────────┐
│  Linux kernel               │  receives merged FDT at ${fdt_addr_r}
│  (EL1, non-secure)          │  never sees overlay — only merged result
└─────────────────────────────┘
```

## U-Boot memory map during overlay apply

```
RAM during U-Boot execution (approximate, addresses from u-boot env)

  ${kernel_addr_r}    ┌──────────────────────────┐
  (e.g. 0x40200000)  │  Image (kernel binary)    │  ~20-30 MB
                      └──────────────────────────┘

  ${fdt_addr_r}       ┌──────────────────────────┐
  (e.g. 0x4FA00000)  │  base DTB                 │  ~50-100 KB
                      │  (expanded by fdt resize) │  + 8 KB headroom
                      │  ← fdt apply merges here  │
                      └──────────────────────────┘

  ${ramdisk_addr_r}   ┌──────────────────────────┐
  (e.g. 0x4FE00000)  │  overlay DTBO             │  ~2-10 KB
                      │  (read-only input to      │
                      │   fdt apply, not reused)  │
                      └──────────────────────────┘
```

`fdt resize 8192` expands the in-memory DTB allocation **before** apply.
If the overlay adds more than 8 KB of new data, increase this value.

## What `fdt apply` does internally (libfdt level)

```
fdt apply ${ramdisk_addr_r}
      │
      ▼
  fdt_overlay_apply() in lib/libfdt/fdt_overlay.c
      │
      ├── parse overlay header (check /dts-v1/ + /plugin/ markers)
      ├── iterate overlay fragments:
      │     each fragment has:
      │       __overlay__ { ... }   ← new properties/nodes to merge
      │       target = <&phandle>   ← which base node to patch
      │       OR target-path = "/soc/spi@..."
      │
      ├── for each fragment:
      │     resolve phandle → node offset in base DTB
      │     fdt_overlay_merge_node()
      │       └── copies properties from __overlay__ into base node
      │           creates child nodes if not present
      │           overwrites properties if already present
      │
      └── fixup phandles (renumber to avoid collisions with base tree)

Result: base DTB at ${fdt_addr_r} is modified in-place.
        The DTBO at ${ramdisk_addr_r} is no longer needed.
```

## DTS/DTBO file format — what the text files compile to

```
Source text (.dts / .dtbo source)         Binary output
─────────────────────────────────         ─────────────

/dts-v1/;                                 FDT magic: 0xd00dfeed
/plugin/;                                 totalsize, version, etc.

&spi0 {                      ──dtc──►     fragment@0 {
    status = "okay";                        target = <&spi0>;
    my_device@0 {                           __overlay__ {
        compatible = "vendor,part";           status = "okay";
        reg = <0>;                            my_device@0 {
        spi-max-frequency = <10000000>;         ...
    };                                        };
};                                          };
                                          };
```

Buildroot compiles `.dts` → `.dtbo` using `dtc` (device tree compiler),
invoked automatically when `BR2_LINUX_KERNEL_DTB_OVERLAYS` is set.

## U-Boot config options — what each one gates

```
u-boot-fragment.config option     What it enables in U-Boot source
──────────────────────────────    ───────────────────────────────────────────
CONFIG_OF_LIBFDT=y                lib/libfdt/ — base FDT read/write library
CONFIG_OF_LIBFDT_OVERLAY=y        lib/libfdt/fdt_overlay.c — fdt_overlay_apply()
CONFIG_SUPPORT_OF_CONTROL=y       U-Boot internal DT used for its own config
CONFIG_CMD_FDT=y                  cmd/fdt.c — 'fdt' command (addr/resize/apply/print)
CONFIG_CMD_MMC=y                  drivers/mmc/ + cmd/mmc.c — 'mmc' command
CONFIG_CMD_FAT=y                  fs/fat/ + cmd/fat.c — 'fatload' / 'load' on FAT
CONFIG_FS_FAT=y                   fs/fat/fat.c — FAT filesystem driver
CONFIG_CMD_LOAD=y                 cmd/load.c — generic 'load' command dispatcher
CONFIG_CMD_BOOTI=y                cmd/booti.c — 'booti' for AArch64 Image format
```

## Overlay DTS syntax — annotated reference

```dts
/dts-v1/;       /* required: DTS version marker                        */
/plugin/;       /* required: marks this as overlay, not standalone DTS */

/* Each hardware change is a "fragment".
   You can have as many fragments as you need.               */

&spi0 {
/*   ^ reference to existing node in base tree by label      */
/*     U-Boot/dtc resolves this to the real node path        */

    status = "okay";
/*  ^ overwrite property in the base node                    */

    #address-cells = <1>;
    #size-cells = <0>;

    imu_node@0 {
    /*          ^ unit address = chip-select number           */
        compatible = "rohm,dh2228fv";
        /*            ^ driver match string                   */
        reg = <0>;
        /*        ^ chip select 0                             */
        spi-max-frequency = <10000000>;
        /*                   ^ Hz, 10 MHz                     */
        status = "okay";
    };
};

&spi1 {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;

    fpga_node@0 {
        compatible = "rohm,dh2228fv";
        reg = <0>;
        spi-max-frequency = <25000000>;  /* 25 MHz for FPGA link */
        status = "okay";
    };
};

&i2c3 {
    status = "okay";
    pinctrl-names = "default";
    /* child sensor nodes would go here */
};
```

## On-target verification commands

```bash
# Check merged device tree nodes exist
ls /proc/device-tree/soc/spi@*/
ls /proc/device-tree/soc/i2c@*/

# Check SPI devices appeared
ls /dev/spidev*

# Check I2C buses appeared
i2cdetect -l

# Probe I2C bus 3 for connected devices
i2cdetect -y 3

# Read SPI device (loopback test, cs0)
spi-config -d /dev/spidev0.0 -s 1000000
echo -ne '\x9f' | spi-pipe -d /dev/spidev0.0 -s 100000 | xxd

# Check U-Boot applied overlays cleanly (check boot log)
dmesg | grep -i "fdt\|overlay\|spi\|i2c" | head -40
```
