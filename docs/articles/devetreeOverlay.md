# Dynamic Device Tree Overlays in U-Boot: From `config.txt` to Linux Kernel Handoff

*A Technical Guide to In-Memory Device Tree Merging, Dynamic Configuration Files, and the Bootloader-to-Kernel Pipeline*

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

---

## 1. Introduction & The Architecture Problem

In modern embedded Linux systems—particularly heterogeneous architectures pairing multi-core ARM application processors with real-time co-processors or dynamic expansion headers—peripheral configurations change depending on the deployment scenario:

* **Scenario A (Avionics / Flight Stack)**: Dedicated hardware UARTs and SPI buses are assigned to a real-time co-processor (e.g., XuanTie E907 RISC-V); onboard sensors (IMU, barometer, compass) are enabled on host I2C buses.
* **Scenario B (Userspace Driver / UIO)**: Hardware mailboxes or custom peripherals are detached from kernel subsystem drivers and rebound to generic Userspace I/O (`generic-uio`) drivers for ultra-low latency userspace control.
* **Scenario C (General-Purpose IO)**: Header pins are exposed as standard `/dev/spidev` and GPIO lines for prototyping.

### The Monolithic DTB Anti-Pattern
Historically, supporting multiple configurations meant building separate monolithic Device Tree Blobs (`board-flight.dtb`, `board-uio.dtb`, `board-gpio.dtb`). This creates significant drawbacks:
1. **Combinatorial Explosion**: If you have 3 sensor configurations and 2 IPC modes, you must build and maintain 3 × 2 = 6 full DTBs.
2. **Maintenance Overhead**: Any upstream kernel update to core clocks, power domains, or memory controller nodes must be duplicated across every custom DTB.
3. **Flashing Friction**: Switching configurations requires rewriting partition images or modifying low-level bootloader binary environments.

### The Solution: Dynamic In-Memory Overlays
The industry standard solution separates the architecture into:
1. **A Single Base Device Tree (`.dtb`)**: Describes the unchanging motherboard hardware (CPU cores, RAM, interrupt controllers, system buses).
2. **Device Tree Overlays (`.dtbo`)**: Small modular fragments describing specific peripheral changes.
3. **A Human-Readable Configuration File (`config.txt`)**: Located on the FAT boot partition, where users simply list which overlays to load.
4. **U-Boot Overlay Engine**: At boot time, U-Boot loads the base DTB into RAM, reads `config.txt`, applies each overlay directly in memory using `libfdt`, and hands the unified device tree to the Linux kernel.

```text
+-------------------------------------------------------+
|                 DYNAMIC BOOT PIPELINE                 |
+-------------------------------------------------------+
  |
  +-> 1. U-Boot reads /boot/config.txt (FAT partition)
  |      Parses: dtoverlay=cubie-a5e-flight-stack uio
  |
  +-> 2. Load Base DTB into RAM @ ${fdt_addr_r}
  |      sun55i-a527-cubie-a5e.dtb
  |
  +-> 3. Expand in-memory Device Tree buffer
  |      fdt resize 65536
  |
  +-> 4. Apply Overlays sequentially into RAM
  |      - load cubie-a5e-flight-stack.dtbo -> fdt apply
  |      - load cubie-a5e-uio.dtbo          -> fdt apply
  |
  +-> 5. Load Kernel Image @ ${kernel_addr_r}
  |
  +-> 6. Execute booti ${kernel_addr_r} - ${fdt_addr_r}
         ARM64 Register x0 = Physical RAM Address of FDT
         Kernel boots with unified, fully-merged tree
```

---

### Why Do This in U-Boot Instead of the Linux Kernel? (The KISS Principle)

A frequent design question in embedded engineering is: *The Linux kernel supports runtime overlays via `CONFIG_OF_OVERLAY` and `configfs`—why not just let Linux handle overlays in userspace after booting?*

The answer is the **KISS (Keep It Simple, Stupid) principle**, driven by fundamental architectural constraints:

#### 1. The Boot-Time "Chicken-and-Egg" Problem
Runtime overlays in the kernel are applied from userspace (via `/sys/kernel/config/device-tree/overlays/` or init scripts). But in real-world systems, overlays frequently configure hardware that the kernel needs **at the very first millisecond of boot**:
* **Early Serial Console & Pinmux**: If an overlay assigns UART0 to Linux and isolates UART2 for the RISC-V co-processor, waiting for userspace to apply this creates pin conflicts on power-up and blinds you to early kernel panics (`earlycon`).
* **Reserved Memory Carveouts (`reserved-memory`)**: The XuanTie E907 firmware requires dedicated non-cacheable DMA regions (`rproc_vdev` @ `0x48000000`). The Linux memory subsystem (Buddy allocator, page tables, CMA zones) establishes physical memory boundaries during early architecture initialization (`setup_arch()`). **You cannot dynamically insert `reserved-memory` carveouts into a running kernel memory map from userspace.**
* **Core Clocks and Power Domains**: Changing clock gates or PMIC regulator voltages after drivers have already probed causes clock tree desynchronization or peripheral brownouts.

#### 2. Avoiding Kernel Driver Fragility & Memory Leaks
Applying and removing Device Tree nodes inside a running kernel is notoriously complex:
* The kernel must dynamically generate new `platform_device` objects, resolve deferred probes, and track device-node reference counts.
* If an overlay disables a node (`status = "disabled"`), the associated driver must cleanly unbind. In practice, many kernel drivers do not implement flawless `.remove()` routines for Device Tree hot-unplug, leading to dangling pointers, kernel memory leaks, or oopses.

#### 3. The Pure Determinism of Bootloader Merging
By executing all overlay merges in **U-Boot before the kernel boots**:
* **The Kernel Stays Simple**: To Linux, the Device Tree looks like a standard, 100% static hardware description. The kernel needs zero dynamic overlay patches, no `configfs` daemons, and zero runtime overhead.
* **Atomic Hardware State**: When the kernel entry point (`head.S`) runs, the entire hardware topology—reserved memory, clocks, pinmux, and driver bindings—is already unified, coherent, and immutable.
* **No RootFS Dependency**: If an overlay is required to configure the storage controller (eMMC/SDIO) or root filesystem bus, U-Boot handles it before storage is even mounted.

---

## 2. The Tale of Two Environments: Static `uboot.env` vs Dynamic `config.txt`

A common source of confusion when inspecting a freshly flashed SD card is discovering a file named `uboot.env` alongside `boot.scr`, but wondering where `config.txt` fits in.

### The Real-World Target Experience: Why Editing `uboot.env` is Painful
When you log into Linux on the board and mount the FAT boot partition, you see the contents of partition 1 (`/dev/mmcblk0p1`):

```bash
cubie-a5e-flight login: root
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

If you try to inspect `uboot.env` using `more` or edit it with `vi`:
```bash
# more /boot/uboot.env
--More-- (2% of 65536 bytes) loglevel=8bootcmd=load mmc 0:1 0x4fc00000 boot.scr && source 0x4fc00000kernel_addr_r=0x40080000kernel_comp_addr_r=0x4400)
```
The screen fills with control codes and unreadable binary characters. And if you attempt to edit `uboot.env` with `vi` and save, U-Boot greets you on the next boot with:

```text
*** Bad CRC, using default environment ***
```
Your edits are completely discarded, and U-Boot falls back to hardcoded compiled defaults. Why does this happen?

---

### Inside `uboot.env`: CRC32 Checksums and Fixed Binary Structures

`uboot.env` is **not a text file**. It is a raw binary image compiled by the host tool `mkenvimage` from a text template ([`uboot-env.txt`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt)):

```bash
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "${BOARD_DIR}/uboot-env.txt"
```

The internal binary anatomy of `uboot.env` is strictly structured:

```text
+-------------------------------------------------------------------------+
|                  U-BOOT BINARY ENVIRONMENT STRUCTURE                    |
+-------------------------------------------------------------------------+
| Offset 0x0000 - 0x0003 : 4-Byte CRC32 Checksum (uint32_t, Little-Endian)|
| Offset 0x0004 - 0x0007 : [Optional Flags Byte for Redundant Envs]      |
| Offset 0x0004/08 ...   : Sequence of NULL-delimited strings:            |
|                          "bootdelay=1\0"                                |
|                          "baudrate=115200\0"                            |
|                          "bootcmd=load mmc 0:1 0x4fc00000 boot.scr ...\0|
|                          "kernel_addr_r=0x40080000\0"                   |
| ...                    : Trailing zero padding (\0)                     |
| Offset 0x0000 - 0xFFFF : Exactly 65,536 Bytes (0x10000) Total Size      |
+-------------------------------------------------------------------------+
```

#### Why Modifying `uboot.env` with a Text Editor Fails:
1. **CRC32 Invalidation**: The first 4 bytes (`0x00000000 - 0x00000003`) store a CRC32 checksum computed across the remaining 65,532 bytes. If you change even a single character in `vi` (such as modifying `bootdelay=1` to `bootdelay=3`), the checksum no longer matches.
2. **Corrupted NULL Delimiters**: Standard text editors treat `\0` (ASCII NUL) bytes as file terminators or replace them with spaces and newlines (`\n`). Saving the file destroys the internal string boundary table.
3. **Truncated Padding**: Text editors typically strip trailing NULL padding, changing the file size from exactly 65,536 bytes to a smaller size.

To safely modify `uboot.env`, a user would normally need either:
* Access to a serial console to interrupt U-Boot and run `setenv` followed by `saveenv`.
* Linux userspace tools (`fw_setenv` / `fw_printenv`) configured with an exact `/etc/fw_env.config` matching MMC sector offsets.
* Running `mkenvimage` on a desktop development PC to rebuild the binary.

None of these options are convenient for an end user or field engineer who simply wants to enable a sensor or change an overlay!

---

### The Modern Solution: Human-Readable `config.txt`

To solve this usability bottleneck, we separate firmware plumbing from user configuration:

| Feature | `uboot.env` (Binary Firmware Baseline) | `config.txt` (User Runtime Configuration) |
| :--- | :--- | :--- |
| **File Format** | 64 KB Binary Blob with 4-byte CRC32 | Pure Plain ASCII / UTF-8 Text |
| **Primary Purpose** | U-Boot bootloader bootstrap plumbing | User overlays, pinmux, and kernel cmdline |
| **Safe to Edit with `vi` / Notepad?** | ❌ **No** (Causes `*** Bad CRC ***`) | ✅ **Yes** (100% human-editable) |
| **Where to Edit** | Requires `mkenvimage` or U-Boot serial shell | Live on Linux (`/boot/config.txt`) or PC SD reader |
| **Ingestion Mechanism** | Loaded automatically by U-Boot core | Imported dynamically into RAM by `boot.cmd` via `env import -t` |

#### How `boot.cmd` Bridges the Two Worlds
Instead of requiring users to touch `uboot.env`, `uboot.env` provides one critical, immutable baseline command:
```text
bootcmd=load mmc 0:1 0x4fc00000 boot.scr && source 0x4fc00000
```
This hands control over to our dynamic boot script ([`boot.cmd`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd)), which reads `config.txt` directly from the FAT partition into RAM:

```sh
if load mmc 0:1 ${ramdisk_addr_r} config.txt; then
    echo ">>> Found Raspberry Pi-style config.txt! Importing configuration..."
    env import -t ${ramdisk_addr_r} ${filesize}
fi
```

The U-Boot `env import -t <address> <size>` command parses plain text `KEY=VALUE` lines in memory and dynamically injects them into U-Boot's active environment table. This allows users to configure overlays (`dtoverlay=...`) and kernel bootargs (`cmdline=...`) in plain text, completely bypassing `uboot.env`!

---

## 3. Accessing and Modifying the Boot Partition on the Target Board

To edit `config.txt` directly on a running Radxa Cubie A5E board, you must know how the SD card storage is organized and mounted.

### Storage Partition Architecture
The system SD card is partitioned into two distinct filesystems:

```text
+--------------------------------------------------------------------------+
|                       SD CARD STORAGE LAYOUT (/dev/mmcblk0)              |
+--------------------------------------------------------------------------+
| Sector 0 - 32767   : Bootloader Carveout (SPL, ATF BL31, Mainline U-Boot) |
+--------------------+-----------------------------------------------------+
| Partition 1        : FAT32 Boot Partition (/dev/mmcblk0p1) - 64 MB        |
|                    : - Image (ARM64 Kernel)                              |
|                    : - sun55i-a527-cubie-a5e.dtb (Base DTB)              |
|                    : - boot.scr (Compiled boot engine)                   |
|                    : - config.txt (Human-readable user configuration)    |
|                    : - cubie-a5e-flight-stack.dtbo (Flight stack overlay)|
|                    : - cubie-a5e-uio.dtbo (Userspace UIO doorbell overlay)|
|                    : - uboot.env (Static U-Boot plumbing environment)    |
+--------------------+-----------------------------------------------------+
| Partition 2        : Linux Root Filesystem (/dev/mmcblk0p2) - ext4        |
|                    : Mounted as root directory ('/')                     |
+--------------------------------------------------------------------------+
```

### Automatic Mount via `/etc/fstab`
In our Buildroot root filesystem overlay ([`project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab)), we declare:

```fstab
# /etc/fstab: static file system information.
# <file system> <mount pt>     <type>   <options>         <dump> <pass>
/dev/root       /              ext4     rw,noatime        0      1
/dev/mmcblk0p1  /boot          vfat     defaults          0      2
proc            /proc          proc     defaults          0      0
devpts          /dev/pts       devpts   defaults,gid=5,mode=620,ptmxmode=0666 0 0
tmpfs           /dev/shm       tmpfs    mode=0777         0      0
tmpfs           /tmp           tmpfs    mode=1777         0      0
tmpfs           /run           tmpfs    mode=0755,nosuid,nodev 0 0
sysfs           /sys           sysfs    defaults          0      0
```

Because `/dev/mmcblk0p1` is configured to mount at `/boot`, all bootloader files—including `config.txt`—are directly accessible immediately upon system login.

### Manual Mount Procedure
If you are running on a minimal or rescue rootfs where `/boot` is not automatically mounted:

```bash
# 1. Create the mount directory if it doesn't already exist:
mkdir -p /boot

# 2. Mount the FAT32 boot partition:
mount -t vfat /dev/mmcblk0p1 /boot

# 3. Verify that the files are present:
ls -l /boot/config.txt
```

### Live On-Board Editing Workflow
With `/boot` mounted, configuring the board is effortless:

```bash
# Open config.txt with vi:
vi /boot/config.txt

# Flush dirty filesystem buffers to SD card:
sync

# Reboot into the new hardware configuration:
reboot
```

### Buildroot Image Assembly Fix: Staging `config.txt`
In Buildroot, `genimage` packages the FAT partition (`boot.vfat`) by taking files strictly from `${BINARIES_DIR}`. 

To guarantee that `config.txt` and `uEnv.txt` are always placed onto the SD card, [`post-image.sh`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh) stages them before `genimage` executes:

```bash
# 1. Compile boot.cmd into boot.scr using host mkimage
${HOST_DIR}/bin/mkimage -A arm64 -T script -C none -d "$(dirname $0)/boot.cmd" "${BINARIES_DIR}/boot.scr"

# 2. Compile uboot-env.txt into uboot.env binary using host mkenvimage
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "$(dirname $0)/uboot-env.txt"

# 3. Stage plain-text runtime configuration templates into BINARIES_DIR
cp -f "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/config.txt"
cp -f "${BOARD_DIR}/uEnv.txt"   "${BINARIES_DIR}/uEnv.txt"

# 4. Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"
```

---

## 4. Anatomy of a Device Tree Overlay (`.dtso`)

Before analyzing the bootloader script, we must understand how Device Tree Overlays are structured and compiled.

### Base Device Tree Compilation (`-@` Flag)
For a base device tree to accept overlays, it **must** be compiled by the Device Tree Compiler (`dtc`) with the symbols flag (`-@`):
```bash
dtc -@ -I dts -O dtb -o base.dtb base.dts
```
The `-@` flag instructs `dtc` to generate a special `__symbols__` node containing a lookup table of every label defined in the tree (e.g., `uart0 = "/soc/serial@2500000"`). Without `__symbols__`, U-Boot cannot resolve phandle references made by overlays.

### Overlay Source Format (`/plugin/`)
An overlay source file (`.dtso`) declares `/plugin/;` at the top. Instead of defining a complete hardware tree, it references target nodes in the base tree using either:
* **Node Labels**: Reference by symbol name (e.g., `&uart0`, `&msgbox`, `&i2c1`).
* **Target Paths**: Explicit path strings (e.g., `target-path = "/soc/serial@2500000"`).

Here is an example overlay ([`cubie-a5e-uio.dtso`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-uio.dtso)) that modifies an existing base node and adds memory regions:

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

When compiled with `dtc -@ -I dts -O dtb -o cubie-a5e-uio.dtbo cubie-a5e-uio.dtso`, `dtc` generates internal metadata:
* `__fixups__`: Lists which labels the overlay expects the base tree to provide (`msgbox`, `rproc`).
* `fragment@0`: Contains the property updates and new nodes to merge into `&msgbox`.

---

## 5. The User Configuration Layer: Raspberry Pi-Style `config.txt`

The default configuration file ([`project-cubie-a5e/board/radxa/cubie_a5e/config.txt`](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/config.txt)) provides two primary directives:

```ini
# /boot/config.txt - Radxa Cubie A5E Hardware & Overlay Configuration
# (Raspberry Pi style configuration for Allwinner T527 / A527)

# ==============================================================================
# 1. Device Tree Overlays (dtoverlay)
# ==============================================================================
# Space-separated list of overlays. You can omit the '.dtbo' extension!
#
# Default: Flight stack (dedicated RISC-V UART2/SPI0, sensors, pin muxing)
dtoverlay=cubie-a5e-flight-stack

# To add the Lite-libmetal UIO Doorbell overlay, simply append it to the list:
# dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio

# ==============================================================================
# 2. Kernel Command-Line Arguments (cmdline)
# ==============================================================================
# Optional extra bootargs appended to the kernel command line.
# Example: Isolate CPU 7 for hard real-time execution with zero OS jitter:
# cmdline=isolcpus=7 nohz_full=7 rcu_nocbs=7
cmdline=
```

### Supported Configuration Directives:
1. `dtoverlay=`: Space-separated list of overlays to apply. You can specify the file name with or without `.dtbo` (e.g., `cubie-a5e-flight-stack` or `cubie-a5e-flight-stack.dtbo`). Multiple overlays are merged sequentially in the order specified.
2. `cmdline=`: Additional arguments to append to the Linux kernel command line. Useful for configuring CPU isolation (`isolcpus`), dynamic printk debugging (`ignore_loglevel`), or setting custom init targets.
3. Fallback compatibility: The boot engine also supports legacy `uEnv.txt` directives (`overlays=` and `extra_bootargs=`) if `config.txt` is absent.

---

## 6. Deconstructing the U-Boot Boot Engine (`boot.cmd` -> `boot.scr`)

U-Boot executes a compiled command script (`boot.scr`) created from a plain-text source script (`boot.cmd`) using `mkimage`:
```bash
mkimage -A arm64 -T script -C none -d boot.cmd boot.scr
```

Below is the complete, production-grade `boot.cmd` script used on the Radxa Cubie A5E / A7A platforms:

```sh
# ==============================================================================
# Radxa Cubie A5E Dynamic Multi-Overlay Boot Script (boot.cmd -> boot.scr)
# Supports Raspberry Pi-style config.txt & standard uEnv.txt
# ==============================================================================

echo "=== Initializing Radxa Cubie A5E Dynamic Boot Sequence ==="

# 1. Base boot arguments (UART console, rootfs, panic handling)
setenv bootargs "console=ttyS0,115200 earlycon root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8"

# 2. Standard Memory Map Addresses (Allwinner 64-bit DRAM base 0x40000000)
if test -z "${kernel_addr_r}";     then setenv kernel_addr_r     0x40080000; fi
if test -z "${fdt_addr_r}";        then setenv fdt_addr_r        0x4fa00000; fi
if test -z "${fdtoverlay_addr_r}"; then setenv fdtoverlay_addr_r 0x4fe00000; fi
if test -z "${ramdisk_addr_r}";    then setenv ramdisk_addr_r    0x4ff00000; fi

# 3. Default base DTB and default overlays
setenv base_dtb sun55i-a527-cubie-a5e.dtb
setenv overlays "cubie-a5e-flight-stack"

# 4. Check for Raspberry Pi-style config.txt first, then uEnv.txt
if load mmc 0:1 ${ramdisk_addr_r} config.txt; then
    echo ">>> Found Raspberry Pi-style config.txt! Importing configuration..."
    env import -t ${ramdisk_addr_r} ${filesize}
elif load mmc 0:1 ${ramdisk_addr_r} uEnv.txt; then
    echo ">>> Found uEnv.txt! Importing environment..."
    env import -t ${ramdisk_addr_r} ${filesize}
fi

# 5. Handle Raspberry Pi-style dtoverlay or standard overlays variable
if test -n "${dtoverlay}"; then
    setenv overlays "${dtoverlay}"
fi

# 6. Append optional user bootargs from cmdline (Pi-style) or extra_bootargs
if test -n "${cmdline}"; then
    echo ">>> Appending cmdline: ${cmdline}"
    setenv bootargs "${bootargs} ${cmdline}"
elif test -n "${extra_bootargs}"; then
    echo ">>> Appending extra_bootargs: ${extra_bootargs}"
    setenv bootargs "${bootargs} ${extra_bootargs}"
fi

# 7. Load base Device Tree into memory
echo ">>> Loading Base Device Tree: ${base_dtb}..."
if load mmc 0:1 ${fdt_addr_r} ${base_dtb}; then
    fdt addr ${fdt_addr_r}
    # Expand FDT buffer by 64 KB to accommodate multiple overlays
    fdt resize 65536
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

---

### Step-by-Step Execution Analysis

#### 1. Memory Map Allocation & Address Safety
In Allwinner 64-bit systems, system DRAM begins at physical address `0x40000000`. To prevent memory corruption when loading uncompressed kernel images, base device trees, and overlays simultaneously, distinct address slots are allocated:

* **`${kernel_addr_r}` (`0x40080000`)**: Staging buffer for the uncompressed ARM64 kernel Image (~30 MB allocation headroom).
* **`${fdt_addr_r}` (`0x4fa00000`)**: Base Device Tree Blob (`sun55i-a527-cubie-a5e.dtb`), expanded in-place by `fdt resize`.
* **`${fdtoverlay_addr_r}` (`0x4fe00000`)**: Staging buffer for loading `.dtbo` overlay fragments (reused sequentially).
* **`${ramdisk_addr_r}` (`0x4ff00000`)**: Scratchpad buffer for `config.txt` text parsing via `env import -t`.
Each buffer has several megabytes of clearance, eliminating any risk of `kernel` data overwriting the `fdt` or vice versa.

#### 2. Parsing the Configuration File (`env import -t`)
```sh
if load mmc 0:1 ${ramdisk_addr_r} config.txt; then
    echo ">>> Found Raspberry Pi-style config.txt! Importing configuration..."
    env import -t ${ramdisk_addr_r} ${filesize}
fi
```
The U-Boot `env import -t <addr> <size>` command parses text files containing `KEY=VALUE` pairs separated by newlines. 
* When `config.txt` contains `dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio`, U-Boot sets `${dtoverlay}` in active RAM.
* When it contains `cmdline=isolcpus=7`, U-Boot sets `${cmdline}` in active RAM.
* The script checks for `config.txt` first, and gracefully falls back to legacy `uEnv.txt` if not present.

#### 3. Why `fdt resize` is Strictly Mandatory
```sh
fdt addr ${fdt_addr_r}
fdt resize 65536
```
This is the single most common point of failure in embedded overlay implementations!
* A compiled base DTB is generated with a fixed header field `totalsize` matching its exact byte length (e.g., 62,914 bytes).
* When U-Boot executes `fdt apply`, `libfdt` attempts to insert new nodes, properties, and strings into the tree.
* **If the buffer is not resized, `libfdt` returns `-FDT_ERR_NOSPACE` (`-3`) and the overlay fails.**
* `fdt resize 65536` expands the active device tree buffer by 64 KB, allocating extra headroom for strings and node descriptors.

#### 4. The Multi-Overlay Application Loop with Smart Extension Resolution
```sh
for overlay in ${overlays}; do
    setenv loaded 0
    if load mmc 0:1 ${fdtoverlay_addr_r} ${overlay}.dtbo; then
        setenv loaded 1
    elif load mmc 0:1 ${fdtoverlay_addr_r} ${overlay}; then
        setenv loaded 1
    elif load mmc 0:1 ${fdtoverlay_addr_r} overlays/${overlay}.dtbo; then
        setenv loaded 1
    fi

    if test "${loaded}" = "1"; then
        fdt apply ${fdtoverlay_addr_r}
    fi
done
```
The script implements smart path resolution:
1. It loops through every space-separated token in `${overlays}`.
2. It attempts to load `${overlay}.dtbo` from the root of the FAT partition.
3. If not found, it tries `${overlay}` (in case the user explicitly included `.dtbo`).
4. If not found, it checks `overlays/${overlay}.dtbo` (matching the Raspberry Pi directory convention).
5. Once staged in memory at `${fdtoverlay_addr_r}`, `fdt apply` merges the fragment directly into the active tree at `${fdt_addr_r}`.

---

## 7. The Handoff: From U-Boot to the Linux Kernel

Once all overlays have been sequentially merged into `${fdt_addr_r}`, U-Boot initiates the boot handoff:

```sh
booti ${kernel_addr_r} - ${fdt_addr_r}
```

### The ARM64 Boot Protocol Handoff
Under the ARM64 Linux kernel boot protocol (`Documentation/arm64/booting.rst`), the bootloader must fulfill strict hardware contracts before jumping to the kernel entry point:

* **Register `x0`**: 64-bit physical RAM address of the Device Tree Blob (`${fdt_addr_r}` = `0x4fa00000`).
* **Registers `x1`, `x2`, `x3`**: Reserved for future architectural use (must be initialized to `0`).
* **MMU & Caches**: MMU disabled; Data Cache cleaned to Point of Coherency (PoC); Instruction Cache invalidated.
* **CPU Mode**: EL2 (Hypervisor Mode) or EL1, with DAIF interrupts strictly masked.

When `booti` executes:
1. U-Boot flushes the data cache across the modified Device Tree region (`0x4fa00000 - 0x4fb00000`) so the kernel's initial uncached memory reads see the merged data.
2. It loads physical address `0x4fa00000` into core CPU register `x0`.
3. It branches directly to `${kernel_addr_r}` (`0x40080000`).

### Early Kernel Ingestion
Inside the Linux kernel:
1. Early assembly (`head.S`) reads register `x0` and verifies the FDT magic number (`0xd00dfeed`).
2. `setup_machine_fdt()` unrolls the tree into the kernel's internal unflattened device tree structure (`struct device_node`).
3. Platform bus drivers (`of_platform_default_populate()`) instantiate drivers matching the merged `compatible` strings.

---

## 8. Runtime Verification in Linux Userspace

Once Linux has booted, how do we prove that U-Boot successfully merged the overlays?

### 1. Inspecting `/proc/device-tree`
The Linux kernel exposes the live, unflattened Device Tree in sysfs at `/proc/device-tree` (symlinked to `/sys/firmware/devicetree/base`). Every directory represents a node; every file represents a property.

To verify that an overlay successfully modified a node:
```bash
# Check the compatible string of the mailbox node:
cat /proc/device-tree/soc/mailbox@3003000/compatible
# Expected output when cubie-a5e-uio.dtbo was applied:
# generic-uio
```

To verify that dual-MMIO registers were merged into the node:
```bash
# Dump the reg property names:
xxd -p /proc/device-tree/soc/mailbox@3003000/reg-names | xxd -r -p
# Expected output:
# msgbox
# sram
```

### 2. Checking Driver Enumeration
Because the overlay changed `compatible = "generic-uio"`, the kernel binds `uio_pdrv_genirq` and enumerates `/dev/uio0`:
```bash
# Verify UIO device presence:
ls -l /dev/uio0
# crw-rw---- 1 root root 242, 0 Sep  4 22:30 /dev/uio0

# Verify memory maps exported by the overlay:
cat /sys/class/uio/uio0/maps/map0/name   # -> msgbox (0x3003000)
cat /sys/class/uio/uio0/maps/map1/name   # -> sram   (0x7131000)
```

---

## 9. Troubleshooting & Common Pitfalls

### 1. Accidentally Editing `uboot.env` Directly: `*** Bad CRC ***`
* **Symptom**: You edited `/boot/uboot.env` with `vi` or `nano`. On next reboot, U-Boot outputs:
  ```text
  *** Bad CRC, using default environment ***
  ```
  and ignores all your custom parameters.
* **Root Cause**: `uboot.env` is a compiled 64 KB binary blob protected by a 4-byte CRC32 header. Editing it directly in a text editor corrupts the NULL delimiters and invalidates the checksum.
* **Fix**: **Do not edit `uboot.env`**. Use `/boot/config.txt` instead! `config.txt` is pure ASCII text, has no checksum restrictions, and is automatically parsed into memory by U-Boot at boot time.

### 2. The `/boot` Partition Is Not Mounted in Linux
* **Symptom**: You log in and `/boot` is completely empty, or `ls /boot` returns nothing.
* **Root Cause**: In minimal rootfs configurations, `/dev/mmcblk0p1` may not have been mounted yet.
* **Fix**: Run:
  ```bash
  mkdir -p /boot
  mount -t vfat /dev/mmcblk0p1 /boot
  ```
  To make this permanent across reboots, ensure your rootfs `/etc/fstab` includes:
  ```text
  /dev/mmcblk0p1  /boot  vfat  defaults  0  2
  ```

### 3. `config.txt` Is Missing on the SD Card After Flashing
* **Symptom**: You mount `/dev/mmcblk0p1` and see `uboot.env`, `boot.scr`, and `Image`, but `config.txt` is absent.
* **Root Cause**: The Buildroot post-image packaging script did not copy `config.txt` from the board directory to `${BINARIES_DIR}` before invoking `genimage`.
* **Fix**: Update `board/radxa/cubie_a5e/post-image.sh` to copy `config.txt` to `${BINARIES_DIR}`:
  ```bash
  cp -f "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/config.txt"
  ```
  Alternatively, you can manually create `/boot/config.txt` directly on the target:
  ```bash
  echo "dtoverlay=cubie-a5e-flight-stack" > /boot/config.txt
  ```

### 4. `fdt apply` Fails with Error `-3` (`-FDT_ERR_NOSPACE`)
* **Symptom**: Overlay fails to apply with return code `-3`.
* **Root Cause**: The base DTB buffer in RAM was not expanded before applying overlays.
* **Fix**: Execute `fdt resize 65536` immediately after `fdt addr ${fdt_addr_r}` to allocate buffer headroom.

### 5. `fdt apply` Fails with Error `-13` (`-FDT_ERR_NOTFOUND`)
* **Symptom**: Overlay fails to find target nodes with return code `-13`.
* **Root Cause**: The base DTB was compiled without `-@` (symbols), preventing phandle resolution.
* **Fix**: Ensure `BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y` is enabled in your Buildroot defconfig so `dtc` runs with the `-@` flag.

### 6. Kernel Panics with `FDT: bad magic` During `booti`
* **Symptom**: Kernel halts immediately during early boot with corrupt FDT magic.
* **Root Cause**: Memory addresses overlap (e.g., kernel uncompression overwrote the FDT buffer).
* **Fix**: Verify memory spacing. Ensure `${fdt_addr_r}` (`0x4fa00000`) is located well above the kernel memory footprint (`0x40080000`).

### 7. Changes in `config.txt` Have No Effect (DOS Line Endings)
* **Symptom**: Overlays listed in `config.txt` are ignored by U-Boot.
* **Root Cause**: The file was saved on Windows with DOS carriage returns (`\r\n`), corrupting variable names when imported.
* **Fix**: Save `config.txt` with standard UNIX line endings (`\n`). You can run `dos2unix /boot/config.txt` if needed.

---

## 10. Summary

By decoupling **low-level bootloader plumbing** (`uboot.env`) from **runtime user configuration** (`config.txt`), we eliminate developer friction while preserving rock-solid boot reliability:

1. **`uboot.env` Stays Untouched**: Serves as the static firmware foundation, initializing serial clocks, DRAM memory maps, and launching `boot.scr`. Developers never need to struggle with binary editors or CRC calculation tools.
2. **`config.txt` Delivers Raspberry Pi Simplicity**: A clean, plain-text configuration file located on the FAT partition. Anyone can enable peripherals, toggle UIO drivers, or isolate CPU cores using standard text editors on Linux, macOS, or Windows.
3. **Deterministic Boot Pipeline**: U-Boot's `env import -t` dynamically bridges the configuration into memory, expands the base Device Tree with `fdt resize`, applies overlays via `fdt apply`, and passes an immutable hardware contract to Linux via ARM64 register `x0`.
