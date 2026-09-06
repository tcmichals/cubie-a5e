# Dynamic Device Tree Overlays in U-Boot: From `config.txt` to Linux Kernel Handoff

*A Technical Deep-Dive into In-Memory FDT Merging, Dynamic Configuration Parsers, and the Bootloader-to-Kernel Contract*

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

---

## 1. The Combinatorial Hardware Nightmare

Embedded hardware rarely stays static. On modern heterogeneous SoCs—like the Allwinner T527 / A527 and A733 pairing octa-core ARM Cortex-A55 cores with dedicated XuanTie E907/E902 RISC-V real-time coprocessors—the exact pin routing, peripheral assignments, and memory maps shift depending on what the board is doing:

* **Flight Stack / Avionics**: Hardware UART0 is dedicated to the Linux debug console, UART2 and SPI0 are isolated and handed directly to the RISC-V core for sub-millisecond sensor acquisition, and onboard I2C sensors (IMU, barometer) are enabled on the Linux bus.
* **Userspace I/O (UIO) / High-Rate IPC**: The hardware inter-processor mailbox (`msgbox`) and dedicated MCU SRAM blocks are detached from the standard kernel mailbox subsystem and bound to `generic-uio`, allowing userspace ring buffers to poll at microsecond latencies.
* **Standard Prototyping**: Expansion pins are exposed as standard `/dev/spidev0.0` nodes and userspace GPIO lines.

### The Monolithic DTB Anti-Pattern

Building a standalone Device Tree Blob (`.dtb`) for every imaginable hardware permutation (`board-flight.dtb`, `board-flight-uio.dtb`, `board-sensors-uio.dtb`, `board-gpio.dtb`) is an engineering dead end:

1. **Combinatorial Explosion**: 4 sensor layouts and 3 IPC configurations force you to compile, test, and ship 12 distinct monolithic DTB files.
2. **Maintenance Hell**: Upstream kernel changes to core clocks, power domains, or pin controller bindings have to be hand-ported across a dozen separate `.dts` files.
3. **Field Failure Risk**: Switching modes in the field requires either rewriting raw bootloader partitions or maintaining brittle boot scripts with massive `if/else` ladders.

---

## 2. The KISS Architecture: In-Memory Bootloader Merging

Rather than building multiple monolithic trees, the clean architecture separates hardware descriptions into modular building blocks:

1. **One Base Device Tree (`.dtb`)**: Describes the immutable motherboard hardware (CPU cores, DRAM controller, interrupt controllers, system interconnects).
2. **Modular Overlays (`.dtbo`)**: Small standalone fragments that mutate specific nodes, enable peripheral clocks, re-route pinmuxes, or carve out shared memory.
3. **A Human-Readable Configuration File (`config.txt`)**: Placed on the FAT32 boot partition so developers can enable or disable features with simple key-value entries.
4. **An In-Memory Overlay Engine in U-Boot**: At boot time, U-Boot loads the base DTB into RAM, reads `config.txt`, merges the selected overlays sequentially using `libfdt`, and passes the unified tree directly to the Linux kernel.

```text
+-------------------------------------------------------------------------+
|                        DYNAMIC BOOTLOADER PIPELINE                      |
+-------------------------------------------------------------------------+
  |
  +-> 1. U-Boot reads /boot/config.txt (FAT partition)
  |      Parses: dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio
  |
  +-> 2. Load Base DTB into RAM @ ${fdt_addr_r} (0x4fa00000)
  |      sun55i-a527-cubie-a5e.dtb (compiled with -@ symbols)
  |
  +-> 3. Expand in-memory Device Tree buffer
  |      fdt resize 0x10000 (adds 64 KB of headroom in hex)
  |
  +-> 4. Apply Overlays sequentially in RAM via libfdt
  |      - load cubie-a5e-flight-stack.dtbo -> fdt apply 0x4fe00000
  |      - load cubie-a5e-uio.dtbo          -> fdt apply 0x4fe00000
  |
  +-> 5. Load Kernel Image @ ${kernel_addr_r} (0x40200000: strictly 2MB-aligned)
  |
  +-> 6. Execute booti ${kernel_addr_r} - ${fdt_addr_r}
         ARM64 Register x0 = Physical RAM Address of merged FDT
         Kernel boots with zero runtime overlay overhead
```

### Why Merge in U-Boot Instead of the Linux Kernel?

The Linux kernel technically supports dynamic overlays at runtime through `CONFIG_OF_OVERLAY` and `configfs`. In practice, relying on userspace to apply hardware overlays is a recipe for silent instability:

#### 1. The Boot-Time "Chicken-and-Egg" Problem
Runtime kernel overlays are applied late in the boot sequence from userspace init scripts. Real-world overlays, however, configure hardware that the kernel needs on the very first instruction:
* **Early Serial Console & Pinmux**: If an overlay assigns UART0 to Linux and isolates UART2 for the RISC-V coprocessor, waiting for userspace to apply this creates pin conflicts on power-up and blinds you to early kernel panics (`earlycon`).
* **Reserved Memory Carveouts (`reserved-memory`)**: The XuanTie E907 firmware requires dedicated, non-cacheable DMA memory (`rproc_vdev` @ `0x48000000`). The Linux memory subsystem (Buddy allocator, page tables, CMA zones) establishes physical memory boundaries during early architecture initialization (`setup_arch()`). **You cannot dynamically insert `reserved-memory` carveouts into a running kernel memory map from userspace.**
* **Core Clocks and Power Domains**: Mutating clock trees or PMIC regulators after platform drivers have already probed causes clock desynchronization or peripheral brownouts.

#### 2. Kernel Driver Unbind Fragility
Modifying Device Tree nodes inside a running kernel forces the kernel to dynamically instantiate `platform_device` objects, resolve deferred probes, and track device-node reference counts. If an overlay disables a node (`status = "disabled"`), the bound driver must cleanly unbind. Many kernel drivers do not have battle-tested `.remove()` paths for Device Tree hot-unplug, leading to dangling pointers, kernel memory leaks, or oopses.

#### 3. Pure Determinism
Merging overlays in U-Boot gives the kernel a completely static, fully-resolved hardware description. To Linux, the device tree is indistinguishable from a custom monolithic DTB. The kernel requires zero dynamic overlay patches, no `configfs` daemons, and zero runtime overhead.

---

## 3. The Two Environments: Static `uboot.env` Binary vs Dynamic `config.txt`

If you inspect a newly flashed SD card, you will find `uboot.env` sitting in the same boot partition alongside `config.txt` and `boot.scr`. Understanding the architectural divide between these two files is essential.

### The Real-World Target Experience: Why Editing `uboot.env` Fails

Mount the FAT boot partition on a running board:

```bash
cubie-a5e login: root
# mkdir -p /boot
# mount -t vfat /dev/mmcblk0p1 /boot
# ls -la /boot
total 24832
drwxr-xr-x    2 root     root         16384 Jan  1  1970 .
drwxr-xr-x   18 root     root          4096 Sep  5 09:40 ..
-rwxr-xr-x    1 root     root      20140544 Sep  5 09:30 Image
-rwxr-xr-x    1 root     root          3573 Sep  5 09:35 boot.scr
-rwxr-xr-x    1 root     root          1241 Sep  5 09:35 config.txt
-rwxr-xr-x    1 root     root          5487 Sep  5 09:30 cubie-a5e-flight-stack.dtbo
-rwxr-xr-x    1 root     root          1114 Sep  5 09:30 cubie-a5e-uio.dtbo
-rwxr-xr-x    1 root     root         62914 Sep  5 09:30 sun55i-a527-cubie-a5e.dtb
-rwxr-xr-x    1 root     root         65536 Sep  5 09:35 uboot.env
-rwxr-xr-x    1 root     root           557 Sep  5 09:35 uEnv.txt
```

If you try to view `uboot.env` with `more` or edit it with `vi`:
```bash
# more /boot/uboot.env
--More-- (2% of 65536 bytes) loglevel=8bootcmd=load mmc 0:1 0x4fc00000 boot.scr && source 0x4fc00000kernel_addr_r=0x40200000kernel_comp_addr_r=0x4400)
```
The terminal fills with control characters. If you save changes with `vi`, the next reboot produces:

```text
*** Bad CRC, using default environment ***
```

U-Boot rejects the file, discards every variable, and falls back to hardcoded compiled defaults.

### Inside `uboot.env`: CRC32 Checksums and Binary Layout

`uboot.env` is **not a text file**. It is a raw binary image compiled during the build by the host tool `mkenvimage` from a text template ([`project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt)):

```bash
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "${BOARD_DIR}/uboot-env.txt"
```

In the U-Boot source tree (`include/env_internal.h`), the environment binary structure is defined as:

```c
/* U-Boot standard non-redundant environment image format */
struct env_image_single {
    uint32_t crc;       /* 4-byte CRC32 checksum over the data array */
    char     data[];    /* Sequential NULL-separated key=value strings */
};
```

On Allwinner platforms without redundant environment enabled, `struct env_image_single` is stored directly on flash.

#### Byte-by-Byte Hex Dump Breakdown
Inspecting `uboot.env` with `hexdump -C` reveals the layout:

```text
Offset    Hexadecimal Bytes                                 ASCII Representation
--------  ------------------------------------------------  --------------------
00000000  7b e2 4c 3f 62 6f 6f 74  64 65 6c 61 79 3d 31 00  |{.L?bootdelay=1.|
00000010  62 61 75 64 72 61 74 65  3d 31 31 35 32 30 30 00  |baudrate=115200.|
00000020  62 6f 6f 74 61 72 67 73  3d 63 6f 6e 73 6f 6c 65  |bootargs=console|
00000030  3d 74 74 79 53 30 2c 31  31 35 32 30 30 20 65 61  |=ttyS0,115200 ea|
...
000001c0  72 5f 6d 6f 64 65 3d 64  65 6d 6f 00 00 00 00 00  |r_mode=demo.....|
000001d0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00010000
```

1. **Bytes `0x00000000 - 0x00000003` (`7b e2 4c 3f`)**: The 32-bit CRC checksum stored in little-endian byte order (`0x3F4CE27B`). It is calculated over the entire remaining payload: bytes `0x00000004` through `0x0000FFFF` (65,532 bytes).
2. **Bytes `0x00000004 - 0x0000000F` (`bootdelay=1\0`)**: The first environment variable string, terminated by a single ASCII NUL byte (`0x00`).
3. **Subsequent Strings**: Each variable is stored as `KEY=VALUE\0`.
4. **End of Environment Marker**: Marked by two consecutive NUL bytes (`\0\0`).
5. **Zero Padding**: The remaining ~65 KB of the file is filled with zeroes (`0x00`) to guarantee an exact total file size of 65,536 bytes (`0x10000`).

When you edit `uboot.env` with a text editor:
* **The CRC Breaks**: Modifying a single character invalidates the 4-byte CRC header.
* **String Boundaries Corrupt**: Text editors treat `\0` as end-of-file or convert it to `\n` or `\r\n`.
* **File Truncation**: Text editors strip the trailing zero padding, changing the total file size from 65,536 bytes.

### The Solution: Decoupling Low-Level Plumbing from User Config

We split configuration responsibilities completely:

* **`uboot.env`**: Static low-level firmware baseline. Holds DRAM addresses, baud rates, and one critical command:
  ```text
  bootcmd=load mmc 0:1 0x4fc00000 boot.scr && source 0x4fc00000
  ```
* **`config.txt`**: Pure ASCII text file on the FAT partition. Users can edit it with `vi` on the target or in Notepad on Windows.
* **`boot.cmd`**: The script engine that reads `config.txt` into RAM using U-Boot's `env import -t` command:
  ```sh
  if load mmc 0:1 ${ramdisk_addr_r} config.txt; then
      echo ">>> Found Raspberry Pi-style config.txt! Importing configuration..."
      env import -t ${ramdisk_addr_r} ${filesize}
  fi
  ```

---

## 4. SD Card Storage Architecture & On-Target Access

The SD card layout uses two distinct partitions:

* **Sectors 0 - 32767 (Offset 8 KB)**: Raw bootloader carveout (`u-boot-sunxi-with-spl.bin` holding SPL, ATF BL31, and Mainline U-Boot).
* **Partition 1 (`/dev/mmcblk0p1`, 64 MB FAT32)**: Mounted at `/boot`. Contains the uncompressed kernel `Image`, base DTB, `.dtbo` overlays, `config.txt`, `boot.scr`, and `uboot.env`.
* **Partition 2 (`/dev/mmcblk0p2`, ext4)**: Root filesystem (`/`).

In the Buildroot rootfs overlay ([`project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab)), the FAT partition is mounted automatically on boot:

```text
/dev/root       /              ext4     rw,noatime        0      1
/dev/mmcblk0p1  /boot          vfat     defaults          0      2
proc            /proc          proc     defaults          0      0
sysfs           /sys           sysfs    defaults          0      0
```

Editing the hardware configuration directly on the board is a 3-step workflow:
```bash
vi /boot/config.txt
sync
reboot
```

---

## 5. Anatomy of an Overlay (`.dtso`) & The `-@` Symbol Trap

An overlay source file (`.dtso`) declares `/plugin/;` at the top. Instead of defining a complete system, it targets specific nodes in the base tree using labels (e.g. `&msgbox`) or absolute paths (`target-path = "/soc/mailbox@3003000"`).

Here is the Userspace I/O overlay ([`project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-uio.dtso`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-uio.dtso)):

```dts
/dts-v1/;
/plugin/;

/*
 * cubie-a5e-uio.dtso - Convert hardware mailbox to userspace UIO device
 */

&msgbox {
    /* 1. Override the compatible string to bind generic-uio */
    compatible = "generic-uio";

    /* 2. Extend reg to expose both Mailbox MMIO and Dedicated MCU SRAM C */
    reg = <0x03003000 0x1000>,
          <0x07131000 0x1000>;
    reg-names = "msgbox", "sram";

    /* 3. Ensure the node is enabled */
    status = "okay";
};

&rproc {
    /* Place RemoteProc into standalone mode (no kernel mailbox binding) */
    status = "okay";
};
```

### The Missing `__symbols__` Trap (`FDT_ERR_NOTFOUND`)

When the Device Tree Compiler (`dtc`) compiles a standard `.dts` without the `-@` flag, it converts all human-readable node labels (`&msgbox`, `&i2c1`, `&uart0`) into anonymous integer phandles and completely strips the string label names.

When an overlay is compiled with `/plugin/;`, its label references cannot be assigned fixed phandles at compile time; `dtc` records them in a `__fixups__` table.

At boot time, U-Boot's `fdt apply` command cross-references the overlay's `__fixups__` table against a top-level `__symbols__` node in the base tree:

```dts
__symbols__ {
    uart0 = "/soc/serial@2500000";
    msgbox = "/soc/mailbox@3003000";
    i2c1 = "/soc/i2c@2502400";
    ccu = "/soc/clock-controller@2001000";
};
```

**If the base DTB was compiled without `-@`, the `__symbols__` node does not exist.** `fdt apply` fails with:
```text
libfdt fdt_apply_overlay(): FDT_ERR_NOTFOUND (-1)
```

To fix this, enable overlay symbols in your build system:
* **Buildroot**: Set `BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y` in defconfig.
* **Standalone Kernel Build**: Pass `DTC_FLAGS="-@"` during build:
  ```bash
  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- DTC_FLAGS="-@" dtbs
  ```
* **Inspect Symbols in DTB**:
  ```bash
  fdtdump sun55i-a527-cubie-a5e.dtb | grep -A 5 __symbols__
  ```

---

## 6. The `config.txt` Interface & Armbian Comparison

The default [`config.txt`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/config.txt) on the boot partition exposes two primary keys:

```ini
# /boot/config.txt - Radxa Cubie A5E Hardware & Overlay Configuration

# 1. Device Tree Overlays (dtoverlay)
# Space-separated list of overlays (omitting .dtbo extension is supported)
dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio

# 2. Kernel Command-Line Arguments (cmdline)
# Optional bootargs appended to kernel command line
cmdline=isolcpus=3 nohz_full=3 rcu_nocbs=3
```

### Armbian Comparison

Armbian popularized the `env import -t` pattern using `/boot/armbianEnv.txt` (`overlays=`, `extraargs=`). Our implementation adopts this mechanic while addressing several structural constraints:

1. **Partition Isolation**: Armbian uses a single monolithic `ext4` root partition. If an uncontrolled power cut corrupts the `ext4` filesystem, the board cannot boot. Our architecture puts bootloader files, kernels, and overlays onto a dedicated 64 MB FAT32 partition.
2. **Multi-Format Ingestion**: Our boot script checks for `config.txt` first, falls back to `armbianEnv.txt`, and finally checks legacy `uEnv.txt`. Dropping an existing `armbianEnv.txt` onto the SD card works without modification.
3. **Cross-Platform Host Editing**: FAT32 mounts natively on Windows, macOS, and Linux PCs without requiring third-party ext4 drivers.

---

## 7. Anatomy of `boot.cmd`: The U-Boot Script Engine

The plain-text source script ([`project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd)) is compiled into `boot.scr` using `mkimage`:

```bash
mkimage -A arm64 -T script -C none -d boot.cmd boot.scr
```

Here is the complete script running on the platform:

```sh
# ==============================================================================
# Radxa Cubie A5E Dynamic Multi-Overlay Boot Script (boot.cmd -> boot.scr)
# Supports Raspberry Pi-style config.txt, Armbian armbianEnv.txt, & uEnv.txt
# ==============================================================================

echo "=== Initializing Radxa Cubie A5E Dynamic Boot Sequence ==="

# 1. Base boot arguments (UART console, rootfs, panic handling)
setenv bootargs "console=ttyS0,115200 earlycon root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8"

# 2. Standard Memory Map Addresses (Allwinner 64-bit DRAM base 0x40000000)
# kernel_addr_r strictly placed at 2MB boundary (0x40200000) per ARM64 boot constraints
if test -z "${kernel_addr_r}";     then setenv kernel_addr_r     0x40200000; fi
if test -z "${fdt_addr_r}";        then setenv fdt_addr_r        0x4fa00000; fi
if test -z "${fdtoverlay_addr_r}"; then setenv fdtoverlay_addr_r 0x4fe00000; fi
if test -z "${ramdisk_addr_r}";    then setenv ramdisk_addr_r    0x4ff00000; fi

# 3. Default base DTB and default overlays
setenv base_dtb sun55i-a527-cubie-a5e.dtb
setenv overlays "cubie-a5e-flight-stack"

# 4. Check for Raspberry Pi-style config.txt first, then armbianEnv.txt, then uEnv.txt
if load mmc 0:1 ${ramdisk_addr_r} config.txt; then
    echo ">>> Found Raspberry Pi-style config.txt! Importing configuration..."
    env import -t ${ramdisk_addr_r} ${filesize}
elif load mmc 0:1 ${ramdisk_addr_r} armbianEnv.txt; then
    echo ">>> Found Armbian-style armbianEnv.txt! Importing environment..."
    env import -t ${ramdisk_addr_r} ${filesize}
elif load mmc 0:1 ${ramdisk_addr_r} uEnv.txt; then
    echo ">>> Found uEnv.txt! Importing environment..."
    env import -t ${ramdisk_addr_r} ${filesize}
fi

# 5. Handle Raspberry Pi-style dtoverlay or standard overlays variable
if test -n "${dtoverlay}"; then
    setenv overlays "${dtoverlay}"
fi

# 6. Append optional user bootargs from cmdline (Pi-style), extraargs (Armbian), or extra_bootargs
if test -n "${cmdline}"; then
    echo ">>> Appending cmdline: ${cmdline}"
    setenv bootargs "${bootargs} ${cmdline}"
elif test -n "${extraargs}"; then
    echo ">>> Appending extraargs: ${extraargs}"
    setenv bootargs "${bootargs} ${extraargs}"
elif test -n "${extra_bootargs}"; then
    echo ">>> Appending extra_bootargs: ${extra_bootargs}"
    setenv bootargs "${bootargs} ${extra_bootargs}"
fi

# 7. Load base Device Tree into memory
echo ">>> Loading Base Device Tree: ${base_dtb}..."
if load mmc 0:1 ${fdt_addr_r} ${base_dtb}; then
    fdt addr ${fdt_addr_r}
    # Expand FDT buffer by 64 KB (0x10000 in hex radix) to accommodate multiple overlays
    fdt resize 0x10000
else
    echo "ERROR: Failed to load base DTB ${base_dtb}!"
    reset
fi

# 8. Dynamically iterate and apply each Device Tree Overlay in ${overlays}
# Automatically resolves both bare names (e.g. 'cubie-a5e-uio') and '.dtbo' extensions
echo ">>> Processing Device Tree Overlays: ${overlays}..."
for overlay in ${overlays}; do
    echo "    Searching overlay: ${overlay}..."
    setenv loaded 0
    if load mmc 0:1 ${fdtoverlay_addr_r} ${overlay}.dtbo; then
        setenv loaded 1
    elif load mmc 0:1 ${fdtoverlay_addr_r} ${overlay}; then
        setenv loaded 1
    elif load mmc 0:1 ${fdtoverlay_addr_r} overlays/${overlay}.dtbo; then
        setenv loaded 1
    fi

    if test "${loaded}" = "1"; then
        if fdt apply ${fdtoverlay_addr_r}; then
            echo "    [OK] Applied ${overlay} successfully."
        else
            echo "    [ERROR] fdt apply failed for ${overlay}!"
        fi
    else
        echo "    [WARN] Could not find overlay file for ${overlay} on mmc 0:1!"
    fi
done

# 9. Load Linux kernel Image and boot
echo ">>> Loading Linux Kernel Image..."
if load mmc 0:1 ${kernel_addr_r} Image; then
    echo ">>> Booting Linux Kernel with Dynamic Overlays..."
    booti ${kernel_addr_r} - ${fdt_addr_r}
else
    echo "ERROR: Failed to load Linux Kernel Image!"
    reset
fi
```

### Critical Implementation Details & Pitfalls

#### 1. The `kernel_addr_r` 2MB Boundary Rule
In Allwinner 64-bit systems, physical DRAM begins at `0x40000000`. Legacy 32-bit scripts often set `kernel_addr_r=0x40080000` (a 512 KB offset).

**On ARM64, this causes silent boot loops or alignment panics.**

Per the Linux kernel ARM64 booting protocol (`Documentation/arch/arm64/booting.rst`), the uncompressed kernel `Image` must be placed at a **2MB-aligned** physical memory address. Setting `kernel_addr_r=0x40200000` satisfies this constraint and preserves the lower 2MB (`0x40000000 - 0x401FFFFF`) for ARM Trusted Firmware (TF-A BL31) and secure monitor carveouts.

#### 2. The U-Boot Hex Radix Trap in `fdt resize`
When `dtc` generates a DTB, the header field `totalsize` matches the exact compiled byte length. When `fdt apply` attempts to insert new nodes, strings, and phandles, `libfdt` returns `-FDT_ERR_NOSPACE` (`-3`) unless the buffer is expanded first.

U-Boot's command-line parser interprets integer arguments as **hexadecimal by default**.
* Writing `fdt resize 0x10000` adds exactly 65,536 bytes (64 KB) of padding headroom.
* Writing decimal `65536` without prefix will be parsed by U-Boot as `0x65536` (415,030 bytes). While it allocates extra memory, on memory-constrained buffers or scripts expecting strict byte counts, omitting the `0x` prefix leads to unexpected buffer overflows or parse failures. Always write `fdt resize 0x10000`.

#### 3. Hush Shell Spacing Bug in Conditional Checks
In U-Boot's Hush parser, `test` is a built-in command that evaluates whitespace-delimited tokens. 

A common bug in generated scripts is accidental whitespace insertion:
```sh
# BROKEN: evaluates the literal string " 1" with leading space
if test "${loaded}" = " 1"; then
```
If `${loaded}` is `"1"`, the string equality check fails silently, and the overlay is never applied. Ensure conditionals use clean token spacing:
```sh
if test "${loaded}" = "1"; then
```

#### 4. Trailing Newlines in `env import -t`
U-Boot's `env import -t` expects newline (`\n`) delimiters. If the last line of `config.txt` does not have a trailing newline (the user didn't press <kbd>Enter</kbd> at the end of the file), U-Boot's parser silently drops the final key-value pair. Always ensure configuration files end with an empty blank line.

---

## 8. Buildroot Automation Pipeline

Buildroot coordinates the compilation, staging, and packaging of every boot component automatically within [`project-cubie-a5e`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e).

```text
+---------------------------------------------------------------------------------------------------+
|                                  BUILDROOT PACKAGING PIPELINE                                     |
+---------------------------------------------------------------------------------------------------+
| 1. Out-of-Tree Overlays: project-cubie-a5e/dts-overlay/allwinner/*.dtso                            |
|    Buildroot Linux package compiles with dtc -@ ---> ${BINARIES_DIR}/*.dtbo                       |
+---------------------------------------------------------------------------------------------------+
| 2. RootFS Pre-Assembly: rootfs-overlay/etc/fstab & post-build.sh                                  |
|    Copies fstab (/dev/mmcblk0p1 -> /boot) and creates /boot directory in ${TARGET_DIR}           |
+---------------------------------------------------------------------------------------------------+
| 3. Post-Image Processing: post-image.sh                                                           |
|    - mkimage compiles boot.cmd ---> ${BINARIES_DIR}/boot.scr                                      |
|    - mkenvimage compiles uboot-env.txt ---> ${BINARIES_DIR}/uboot.env                             |
|    - Staging: copies config.txt and uEnv.txt into ${BINARIES_DIR}/                                |
+---------------------------------------------------------------------------------------------------+
| 4. Final Disk Assembly: genimage.cfg                                                              |
|    Stitches SPL, boot.vfat (with config.txt, dtbos, Image), and rootfs.ext4 into sdcard.img       |
+---------------------------------------------------------------------------------------------------+
```

### Post-Image Script (`post-image.sh`)

When the kernel and rootfs finishes building, Buildroot executes [`project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh):

```bash
#!/bin/sh
BOARD_DIR="$(dirname $0)"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

# 1. Compile boot.cmd into boot.scr using host mkimage
${HOST_DIR}/bin/mkimage -A arm64 -T script -C none -d "${BOARD_DIR}/boot.cmd" "${BINARIES_DIR}/boot.scr"

# 2. Compile uboot-env.txt into uboot.env binary using host mkenvimage
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "${BOARD_DIR}/uboot-env.txt"

# 3. Stage plain-text runtime configuration templates into BINARIES_DIR for genimage
cp -f "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/config.txt"
cp -f "${BOARD_DIR}/uEnv.txt"   "${BINARIES_DIR}/uEnv.txt"

# 4. Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
genimage --config "${GENIMAGE_CFG}" \
         --rootpath "${TARGET_DIR}" \
         --tmppath "${GENIMAGE_TMP}" \
         --inputpath "${BINARIES_DIR}" \
         --outputpath "${BINARIES_DIR}"

exit 0
```

### Partition Assembly (`genimage.cfg`)

Host `genimage` reads [`project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg):

```cfg
image boot.vfat {
    vfat {
        files = {
            "sun55i-a527-cubie-a5e.dtb",
            "cubie-a5e-flight-stack.dtbo",
            "cubie-a5e-uio.dtbo",
            "config.txt",
            "uEnv.txt",
            "boot.scr",
            "Image",
            "uboot.env"
        }
    }
    size = 64M
}

image sdcard.img {
    hdimage {}

    partition u-boot {
        in-partition-table = false
        image = "u-boot-sunxi-with-spl.bin"
        offset = 8K
        size = 1016K
    }

    partition boot {
        partition-type = 0xC
        bootable = "true"
        image = "boot.vfat"
        offset = 4M
    }

    partition rootfs {
        partition-type = 0x83
        image = "rootfs.ext4"
    }
}
```

Building the entire stack requires two commands:
```bash
make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig
make -C bld
```

Flash the generated image:
```bash
sudo dd if=bld/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

---

## 9. The Kernel Handoff Contract (ARM64 Register `x0`)

Once U-Boot applies all overlays into memory at `0x4fa00000`, it executes:
```sh
booti ${kernel_addr_r} - ${fdt_addr_r}
```

Under the ARM64 boot protocol:
* **Register `x0`**: Holds the 64-bit physical DRAM address of the Device Tree Blob (`0x4fa00000`).
* **Registers `x1 - x3`**: Must be set to `0`.
* **MMU**: Disabled.
* **Caches**: Data cache cleaned to Point of Coherency (PoC), instruction cache invalidated.
* **CPU Mode**: EL2 (Hypervisor) or non-secure EL1.

When `booti` jumps to `0x40200000`, the kernel entry point (`arch/arm64/kernel/head.S`) reads `x0`, verifies the `0xd00dfeed` FDT header magic, and unrolls the merged nodes via `setup_machine_fdt()`. To Linux, the device tree is completely static.

---

## 10. Live Verification on Hardware

Boot the board with `dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio` in `/boot/config.txt`.

### 1. Serial Console U-Boot Log
During boot, U-Boot outputs the sequential merge:

```text
=== Initializing Radxa Cubie A5E Dynamic Boot Sequence ===
>>> Found Raspberry Pi-style config.txt! Importing configuration...
>>> Loading Base Device Tree: sun55i-a527-cubie-a5e.dtb...
62914 bytes read in 6 ms (10.0 MiB/s)
>>> Processing Device Tree Overlays: cubie-a5e-flight-stack cubie-a5e-uio...
    Searching overlay: cubie-a5e-flight-stack...
5487 bytes read in 2 ms (2.6 MiB/s)
    [OK] Applied cubie-a5e-flight-stack successfully.
    Searching overlay: cubie-a5e-uio...
1114 bytes read in 1 ms (1.1 MiB/s)
    [OK] Applied cubie-a5e-uio successfully.
>>> Loading Linux Kernel Image...
20140544 bytes read in 868 ms (22.1 MiB/s)
>>> Booting Linux Kernel with Dynamic Overlays...
## Flattened Device Tree blob at 4fa00000
   Booting using the fdt blob at 0x4fa00000
   Loading Device Tree to 0000000049ff0000, end 0000000049ffffff ... OK

Starting kernel ...
```

### 2. Live Linux Inspection via Sysfs and `/proc/device-tree`

Verify the mailbox node was converted from the standard kernel driver to Userspace I/O:

```bash
# 1. Verify compatible string is generic-uio
cat /proc/device-tree/soc/mailbox@3003000/compatible
# Output: generic-uio

# 2. Check dual-MMIO reg names added by the overlay
xxd -p /proc/device-tree/soc/mailbox@3003000/reg-names | xxd -r -p
# Output: msgboxsram

# 3. Check /dev/uio0 driver binding
ls -la /dev/uio0
# crw-rw---- 1 root root 242, 0 Sep  6 12:00 /dev/uio0

# 4. Verify physical memory map carveouts exported by the kernel
cat /sys/class/uio/uio0/maps/map0/name && cat /sys/class/uio/uio0/maps/map0/addr
# msgbox
# 0x3003000

cat /sys/class/uio/uio0/maps/map1/name && cat /sys/class/uio/uio0/maps/map1/addr
# sram
# 0x7131000
```

---

## 11. Field Triage & Troubleshooting Matrix

| Symptom | Root Cause | Fix |
| :--- | :--- | :--- |
| `*** Bad CRC, using default environment ***` | Editing `uboot.env` with `vi` broke the 4-byte CRC32 header and null delimiters. | Never edit `uboot.env` directly. Use `/boot/config.txt`. Re-flash or delete `uboot.env` to restore defaults. |
| `libfdt fdt_apply_overlay(): FDT_ERR_NOSPACE (-3)` | The base DTB buffer at `${fdt_addr_r}` ran out of memory during overlay node insertion. | Call `fdt resize 0x10000` in `boot.cmd` immediately after `fdt addr ${fdt_addr_r}`. |
| `libfdt fdt_apply_overlay(): FDT_ERR_NOTFOUND (-1)` | Base DTB was compiled without `-@` (symbols), omitting the `__symbols__` lookup table. | Set `BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y` in defconfig, or build DTBs with `make DTC_FLAGS="-@" dtbs`. |
| Kernel hangs immediately after `Starting kernel ...` | `kernel_addr_r` was set to an unaligned offset (e.g. `0x40080000`), violating ARM64 2MB alignment. | Set `kernel_addr_r=0x40200000` (2MB boundary from DRAM base `0x40000000`). |
| Overlays defined in `config.txt` are completely ignored | `config.txt` was saved with DOS CRLF (`\r\n`) line endings or lacks a trailing newline. | Convert with `dos2unix /boot/config.txt` and ensure the file ends with an empty line. |
| `[WARN] Could not find overlay file` | File naming mismatch in `dtoverlay=`. | Use the exact file basename without `.dtbo` (e.g., `dtoverlay=cubie-a5e-uio`). |
| `/boot` is empty on target | The FAT partition was not mounted at boot. | Run `mount -t vfat /dev/mmcblk0p1 /boot` and add the mount to `/etc/fstab`. |
