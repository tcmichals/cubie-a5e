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
  |      sun55i-a527-cubie-a5e.dtb (compiled with -@ symbols)
  |
  +-> 3. Expand in-memory Device Tree buffer
  |      fdt resize 0x10000 (adds 64 KB padding headroom)
  |
  +-> 4. Apply Overlays sequentially into RAM
  |      - load cubie-a5e-flight-stack.dtbo -> fdt apply
  |      - load cubie-a5e-uio.dtbo          -> fdt apply
  |
  +-> 5. Load Kernel Image @ ${kernel_addr_r} (0x40200000: 2MB aligned)
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
--More-- (2% of 65536 bytes) loglevel=8bootcmd=load mmc 0:1 0x4fc00000 boot.scr && source 0x4fc00000kernel_addr_r=0x40200000kernel_comp_addr_r=0x4400)
```
The screen fills with control codes and unreadable binary characters. And if you attempt to edit `uboot.env` with `vi` and save, U-Boot greets you on the next boot with:

```text
*** Bad CRC, using default environment ***
```
Your edits are completely discarded, and U-Boot falls back to hardcoded compiled defaults. Why does this happen?

---

### Inside `uboot.env`: Binary Architecture, CRC32 Checksums, and C Structs

`uboot.env` is **not a text file**. It is a raw binary image compiled by the host tool `mkenvimage` from a text template ([`project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt`](/project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt)):

```bash
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "${BOARD_DIR}/uboot-env.txt"
```

#### The C Structure Representation (`include/env_internal.h`)
Inside the U-Boot source tree, the environment binary layout is defined by the following C data structure:

```c
/* U-Boot standard non-redundant environment image format */
struct env_image_single {
    uint32_t crc;       /* 4-byte CRC32 checksum over the data array */
    char     data[];    /* Sequential NULL-separated key=value strings */
};

/* Redundant environment format (when CONFIG_SYS_REDUNDAND_ENVIRONMENT=y) */
struct env_image_redundant {
    uint32_t crc;       /* 4-byte CRC32 checksum over data array + flags */
    unsigned char flags;/* Generation counter / active buffer indicator */
    char     data[];    /* Sequential NULL-separated key=value strings */
};
```

On Allwinner platforms without redundant environment enabled, `struct env_image_single` is used.

#### Byte-by-Byte Hex Dump Breakdown
If you inspect the binary `uboot.env` using `hexdump -C` or `xxd`, the byte layout is exposed:

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

---

### How `mkenvimage` Compiles the Environment
When the Buildroot host tool `mkenvimage` executes during image assembly, it performs four strict operations:
1. **Syntax Parsing**: It reads [`project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt`](/project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt), stripping comment lines (beginning with `#`) and empty lines.
2. **Buffer Packaging**: It replaces line feeds (`\n`) with null bytes (`\0`), sequentializing the tokens into an in-memory buffer starting at offset `0x0004`.
3. **CRC32 Calculation**: It calculates a standard Ethernet CRC32 checksum (polynomial `0xEDB88320`) over all bytes from offset `0x0004` to `size - 1` (`0xFFFF`).
4. **Binary Emission**: It writes the 4-byte CRC header to bytes `0x00 - 0x03`, followed by the payload, and pads the file with null bytes until exactly `0x10000` (65,536) bytes are written.

---

### How U-Boot Validates `uboot.env` at Boot Time
During the U-Boot board initialization sequence (`env_init()` and `env_relocate()`):
1. U-Boot reads the 64 KB block from the storage medium (FAT partition or raw MMC offset) into a memory buffer.
2. It extracts the 4-byte CRC header at offset `0x0000`.
3. It recalculates the CRC32 of the remaining 65,532 bytes.
4. **Verification**:
   * **If CRC matches**: The environment is marked `ENV_VALID`. U-Boot calls `himport_r()` to parse the null-separated strings into its internal hash table.
   * **If CRC fails**: The environment is marked `ENV_INVALID`. U-Boot prints:
     ```text
     *** Bad CRC, using default environment ***
     ```
     It immediately discards the file buffer and falls back to the hardcoded default environment compiled into the U-Boot binary (`default_environment[]`).

#### Why Modifying `uboot.env` with `vi` or `nano` Always Fails:
* **Destroying CRC32**: Changing even one letter changes the checksum. Since a text editor cannot recalculate the CRC header, U-Boot flags the file as corrupt.
* **Mangled Null Characters**: Text editors interpret `0x00` as an EOF or replace it with line endings (`\n`, `\r\n`), destroying string boundaries.
* **Truncated Filesize**: Text editors drop trailing zeroes on save, producing a truncated file that U-Boot rejects.

---

### The Spectrum of Bootloader Files: When to Use What

To avoid confusion, here is how each configuration and boot file is used across the platform:

* **config.txt** (Plain Text): Raspberry Pi-style overlay selection (`dtoverlay=`) and kernel args. Safely editable live with `vi` or on any PC.
* **boot.cmd** (Shell Script): Dynamic boot script implementing overlay loading logic (`config.txt`). Editable in repository.
* **boot.scr** (Binary Script): Compiled boot engine executed by U-Boot (`source 0x4fc00000`).
* **uboot.env** (64 KB Binary): Static firmware baseline with CRC32. Do not edit directly.
* **uboot-env.txt** (Plain Text): Default environment template compiled into `uboot.env`.
* **uEnv.txt** (Plain Text): Legacy fallback configuration (`overlays=`). Editable live with `vi`.

*Repository file locations:*
* `config.txt`: [`project-cubie-a5e/board/radxa/cubie_a5e/config.txt`](/project-cubie-a5e/board/radxa/cubie_a5e/config.txt)
* `boot.cmd`: [`project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd`](/project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd)
* `uboot-env.txt`: [`project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt`](/project-cubie-a5e/board/radxa/cubie_a5e/uboot-env.txt)
* `uEnv.txt`: [`project-cubie-a5e/board/radxa/cubie_a5e/uEnv.txt`](/project-cubie-a5e/board/radxa/cubie_a5e/uEnv.txt)


---

### The Modern Solution: Human-Readable `config.txt`

To solve this usability bottleneck, we separate firmware plumbing from user configuration:

* **File Format**: `uboot.env` is a 64 KB binary blob protected by a 4-byte CRC32 header, whereas `config.txt` is pure, human-readable plain text (ASCII/UTF-8).
* **Primary Purpose**: `uboot.env` handles low-level firmware bootstrap plumbing (baud rates, DRAM addresses, immutable `bootcmd`), whereas `config.txt` provides user runtime configuration (enabling overlays and kernel command-line options).
* **Editing with vi**: Direct editing of `uboot.env` with `vi` or `nano` corrupts the CRC32 checksum, forcing U-Boot to revert to compiled-in defaults. In contrast, `config.txt` is 100% safe to edit live on the running board or in any text editor on a host PC.
* **Where to Edit**: Modifying `uboot.env` requires the host `mkenvimage` utility or the U-Boot serial console, while `config.txt` can be edited directly on the target at `/boot/config.txt` or on a PC SD card reader.
* **Ingestion Mechanism**: `uboot.env` is loaded automatically by U-Boot at reset, while `config.txt` is imported dynamically into RAM by `boot.cmd` using U-Boot's `env import -t` command.

#### How `boot.cmd` Bridges the Two Worlds
Instead of requiring users to touch `uboot.env`, `uboot.env` provides one critical, immutable baseline command:
```text
bootcmd=load mmc 0:1 0x4fc00000 boot.scr && source 0x4fc00000
```
This hands control over to our dynamic boot script ([`project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd`](/project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd)), which reads `config.txt` directly from the FAT partition into RAM:

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

* **Sector 0 - 32767** (Raw Flash Blocks): Bootloader Carveout (SPL, ATF BL31, Mainline U-Boot)
* **Partition 1** (FAT32 Boot Partition, `/dev/mmcblk0p1` - 64 MB): Bootloader, kernel, device tree, overlays, and config
* **Partition 2** (Linux Root Filesystem, `/dev/mmcblk0p2` - ext4): Mounted as root directory (`/`)

Partition 1 files:
* `Image` (ARM64 Kernel)
* `sun55i-a527-cubie-a5e.dtb` (Base DTB)
* `boot.scr` (Compiled boot engine)
* `config.txt` (Human-readable user configuration)
* `cubie-a5e-flight-stack.dtbo` (Flight stack overlay)
* `cubie-a5e-uio.dtbo` (Userspace UIO doorbell overlay)
* `uboot.env` (Static U-Boot plumbing environment)

### Automatic Mount via `/etc/fstab`
In our Buildroot root filesystem overlay ([`project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab`](/project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab)), we declare:

```text
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

---

## 4. Anatomy of a Device Tree Overlay (`.dtso`)

Before analyzing the bootloader script, we must understand how Device Tree Overlays are structured and compiled.

### Base Device Tree Compilation (`-@` Flag) & Symbol Resolution

For a base device tree to accept overlays, it **must** be compiled by the Device Tree Compiler (`dtc`) with the symbols flag (`-@`):
```bash
dtc -@ -I dts -O dtb -o base.dtb base.dts
```

> [!IMPORTANT]
> **The Missing `__symbols__` Trap (`FDT_ERR_NOTFOUND`)**
> 
> Under normal compilation without `-@`, `dtc` converts all human-readable node labels (e.g. `uart0: serial@2500000`, `msgbox: mailbox@3003000`, `&i2c1`) into anonymous integer phandles and completely strips the string label names to minimize binary size.
>
> When `-@` is passed, `dtc` retains every label by generating a dedicated top-level node named `__symbols__`:
> ```dts
> __symbols__ {
>     uart0 = "/soc/serial@2500000";
>     msgbox = "/soc/mailbox@3003000";
>     i2c1 = "/soc/i2c@2502400";
>     ccu = "/soc/clock-controller@2001000";
> };
> ```
> 
> When overlays (`.dtbo`) are compiled with `/plugin/;`, their external label references (e.g., `&msgbox`, `&i2c1`) cannot be assigned fixed phandles at compile time; instead, `dtc` records them in a `__fixups__` table. 
> 
> At boot time, U-Boot's `fdt apply` command cross-references the overlay's `__fixups__` table against the base tree's `__symbols__` node to resolve and patch the integer phandle values.
> 
> **If the base DTB was compiled without `-@`, the `__symbols__` node does not exist!** U-Boot has no way to map labels, causing `fdt apply` to immediately fail with the cryptic error:
> ```text
> libfdt fdt_apply_overlay(): FDT_ERR_NOTFOUND (-1)
> ```
> 
> **How to ensure `-@` is enabled in your build system:**
> * **Buildroot**: Enable `BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y` in your board defconfig. Buildroot automatically appends `-@` to all `dtc` invocations.
> * **Kernel Makefile**: If building the kernel standalone, pass `DTC_FLAGS="-@"` during DTB compilation:
>   ```bash
>   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- DTC_FLAGS="-@" dtbs
>   ```
> * **Verification**: You can verify whether a base DTB has symbols using `fdtdump` or `fdtgrep`:
>   ```bash
>   fdtdump sun55i-a527-cubie-a5e.dtb | grep -A 5 __symbols__
>   ```

### Overlay Source Format (`/plugin/`)
An overlay source file (`.dtso`) declares `/plugin/;` at the top. Instead of defining a complete hardware tree, it references target nodes in the base tree using either:
* **Node Labels**: Reference by symbol name (e.g., `&uart0`, `&msgbox`, `&i2c1`).
* **Target Paths**: Explicit path strings (e.g., `target-path = "/soc/serial@2500000"`).

Here is an example overlay ([`project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-uio.dtso`](/project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-uio.dtso)) that modifies an existing base node and adds memory regions:

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

The default configuration file ([`project-cubie-a5e/board/radxa/cubie_a5e/config.txt`](/project-cubie-a5e/board/radxa/cubie_a5e/config.txt)) provides two primary directives:

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
3. Fallback compatibility: The boot engine also supports legacy `uEnv.txt` directives (`overlays=` and `extra_bootargs=`).

---

### Architectural Comparison: How Armbian Does It

If you have worked with Armbian on Allwinner (Sunxi), Rockchip, or Amlogic boards, this pattern will look very familiar. Armbian pioneered this text-import workflow to solve the same usability problem.

#### 1. Why Armbian Abandoned Direct `uboot.env` Editing
In early SBC distributions, modifying boot parameters required using U-Boot's `saveenv` command over a serial UART console, or running binary editing tools. Non-technical users frequently corrupted the 4-byte CRC32 header or NULL padding, leaving boards in an unbootable state. 

To solve this, Armbian introduced `/boot/armbianEnv.txt`:
```ini
verbosity=1
bootlogo=false
overlay_prefix=sun55i-a527
overlays=flight-stack uio
rootdev=UUID=e2a4...
rootfstype=ext4
extraargs=isolcpus=7
```
Armbian's popular `armbian-config` interactive terminal utility is actually just a menu-driven frontend that writes `KEY=VALUE` strings into `/boot/armbianEnv.txt`.

#### 2. How Armbian's Boot Engine Ingests Configuration
Inside Armbian's official `boot.cmd` script, you find the exact same U-Boot primitive:
```sh
# Armbian boot engine ingestion snippet
load mmc ${devnum}:${distro_bootpart} ${loadaddr} /boot/armbianEnv.txt || load mmc ${devnum}:${distro_bootpart} ${loadaddr} armbianEnv.txt
env import -t ${loadaddr} ${filesize}
```
U-Boot parses the text file directly into its active environment hash table in RAM, then iterates over `${overlays}` applying each `.dtbo` via `fdt apply`.

#### 3. In-Depth Comparison: Armbian Architecture vs Our Cubie A5E Architecture
While our design adopts the proven `env import -t` engine popularized by Armbian, we improve upon it across several critical dimensions:

* **Boot Filesystem Architecture**: Standard Armbian uses a single monolithic `ext4` root partition where `/boot` resides inside the Linux filesystem. Our architecture provides a dedicated 64 MB FAT32 boot partition (`boot.vfat`) alongside the `ext4` rootfs.
* **Cross-Platform Host Editing**: Standard Armbian SD cards cannot be read on Windows or macOS without third-party `ext4` drivers. Our FAT32 boot partition automatically mounts as a standard flash drive on Windows, macOS, and Linux PCs out-of-the-box.
* **Filesystem Failure Isolation**: In a monolithic setup, if a sudden power cut corrupts the `ext4` filesystem during flight or field operation, the boot files and kernel become inaccessible. With our dedicated FAT32 boot partition, the kernel (`Image`), base device tree (`.dtb`), overlays (`.dtbo`), and configuration (`config.txt`) are physically isolated on partition 1, allowing the system to boot or be recovered easily.
* **Configuration Syntax**: Armbian uses proprietary `armbianEnv.txt` variables (`overlays=`, `extraargs=`). Our architecture adopts the familiar Raspberry Pi `config.txt` convention (`dtoverlay=`, `cmdline=`).
* **Ecosystem Compatibility & Multi-Format Ingestion**: Armbian's boot engine is locked strictly to `armbianEnv.txt`. Our universal `boot.cmd` integrates an ordered priority chain: it checks for and imports `config.txt` first, falls back to `armbianEnv.txt` if absent, and finally tries legacy `uEnv.txt`. Rather than attempting to merge multiple disjoint files, it prioritizes and imports the first configuration file it encounters. Developers migrating from Armbian can simply drop their existing `armbianEnv.txt` onto the partition, and the script will automatically import and apply its variables without requiring translation!
* **Overlay Name Resolution**: Armbian requires rigid board-specific prefixing via `overlay_prefix` (e.g. looking strictly for `${overlay_prefix}-${overlay}.dtbo`), which causes custom overlays to fail if naming doesn't follow strict upstream conventions. Our engine uses smart resolution: if the user specifies `flight-stack` or `cubie-a5e-flight-stack`, it searches directly for the exact file or automatically appends `.dtbo`.
* **Kernel Arguments Appending**: Armbian only appends `extraargs`. Our engine unifies community conventions by checking `cmdline=` (Pi-style), `extraargs=` (Armbian), and `extra_bootargs=` (uEnv), appending whichever is defined to `bootargs`.
* **Real-Time & Flight-Critical Tuning**: Armbian focuses on general-purpose server/desktop workloads where low-latency CPU isolation must be manually configured. Our Buildroot architecture integrates an automated real-time init daemon (`/etc/init.d/S15realtime`) that isolates high-performance cores (`isolcpus=3` or `7`), sets RCU affinity, and steers hardware IRQs to low cores automatically when real-time flight overlays are active.

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
    # Expand FDT buffer by 64 KB (0x10000) to accommodate multiple overlays
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

---

### Step-by-Step Execution Analysis

#### 1. Memory Map Allocation & Address Safety (The `kernel_addr_r` 2MB Alignment Trap)
In Allwinner 64-bit systems, system DRAM begins at physical address `0x40000000`. To prevent memory corruption when loading uncompressed kernel images, base device trees, and overlays simultaneously, distinct address slots are allocated:

* **`${kernel_addr_r}` (`0x40200000`)**: Staging buffer for the uncompressed ARM64 kernel Image.
* **`${fdt_addr_r}` (`0x4fa00000`)**: Base Device Tree Blob (`sun55i-a527-cubie-a5e.dtb`), expanded in-place by `fdt resize`.
* **`${fdtoverlay_addr_r}` (`0x4fe00000`)**: Staging buffer for loading `.dtbo` overlay fragments (reused sequentially).
* **`${ramdisk_addr_r}` (`0x4ff00000`)**: Scratchpad buffer for `config.txt` text parsing via `env import -t`.

> [!WARNING]
> **The `kernel_addr_r` Alignment Trap: Why `0x40080000` Can Fail**
>
> You will often see legacy boot scripts define `kernel_addr_r=0x40080000` (512 KB offset from DRAM base). **On 64-bit ARM architectures (ARM64), this can cause alignment faults or boot hangs.**
>
> According to the official Linux kernel ARM64 boot protocol ([`Documentation/arch/arm64/booting.rst`](https://www.kernel.org/doc/Documentation/arch/arm64/booting.rst)):
> * The uncompressed kernel binary `Image` must be placed at a **2MB-aligned** base address within physical DRAM (e.g. `0x40200000` when DRAM starts at `0x40000000`).
> * While U-Boot's `booti` command can often relocate compressed images unpacked via `kernel_comp_addr_r`, loading an uncompressed raw `Image` directly to `0x40080000` violates the 2MB boundary (512 KB is not 2MB aligned).
> * Depending on the kernel's `CONFIG_ARM64_VA_BITS`, page size (4KB vs 64KB), and MMU configuration, executing from an unaligned address forces `booti` to perform an emergency in-memory copy, or can trigger early MMU translation faults and silent boot loops.
>
> Setting `kernel_addr_r=0x40200000` satisfies the 2MB boundary precisely while leaving 2MB of headroom at `0x40000000` for firmware reserved memory (ARM Trusted Firmware / ATF and OP-TEE).

#### 2. The `config.txt` Parsing Gap: Bridging Mainline U-Boot and Raspberry Pi Workflows
```sh
if load mmc 0:1 ${ramdisk_addr_r} config.txt; then
    echo ">>> Found Raspberry Pi-style config.txt! Importing configuration..."
    env import -t ${ramdisk_addr_r} ${filesize}
fi
```

> [!NOTE]
> **Why Mainline SBCs Need `boot.scr` to Parse `config.txt`**
>
> On a Raspberry Pi, `config.txt` is parsed natively by the proprietary VideoCore GPU firmware *before* the ARM core ever executes. The GPU firmware reads overlays, applies them in GPU memory, and hands a pre-flattened DTB to U-Boot or the kernel.
>
> Mainline Allwinner SBCs have **no native `config.txt` parser** in U-Boot or ROM. Mainline U-Boot natively understands compiled binary environment files (`uboot.env`), not arbitrary text files.
>
> Our production `boot.scr` bridges this gap completely:
> 1. It uses `load mmc 0:1 ${ramdisk_addr_r} config.txt` to read the plain-text file from FAT into scratchpad RAM.
> 2. It invokes U-Boot's `env import -t ${ramdisk_addr_r} ${filesize}` to parse the newline-separated `key=value` text entries directly into active U-Boot runtime variables.
> 3. When `config.txt` contains `dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio`, U-Boot dynamically assigns `${dtoverlay}`, which our script seamlessly maps to `${overlays}` to drive the subsequent `fdt apply` loop.
> 4. When `config.txt` contains `cmdline=isolcpus=7`, U-Boot dynamically sets `${cmdline}`, which our script automatically appends to `${bootargs}`.
>
> The script also includes fallback support for Armbian-style `armbianEnv.txt` and legacy `uEnv.txt`.

> [!TIP]
> **The `env import -t` Trailing Newline Requirement**
>
> U-Boot's `env import -t` parser expects text files to be delimited strictly by standard Unix newlines (`\n`). If a variable definition on the final line of `config.txt` lacks a terminating newline (i.e. the user didn't press <kbd>Enter</kbd> at EOF), certain U-Boot parser implementations will silently drop that last line. Always ensure your configuration files end with an empty blank line.

#### 3. Why `fdt resize 0x10000` is Strictly Mandatory (and the Hex Padding Rule)
```sh
fdt addr ${fdt_addr_r}
fdt resize 0x10000
```
This is the single most common point of failure in embedded overlay implementations!

* A compiled base DTB is generated with a fixed header field `totalsize` matching its exact byte length (e.g., 62,914 bytes).
* When U-Boot executes `fdt apply`, `libfdt` attempts to insert new nodes, properties, and strings into the tree.
* **If the buffer is not resized, `libfdt` returns `-FDT_ERR_NOSPACE` (`-3`) and the overlay fails.**

> [!IMPORTANT]
> **U-Boot `fdt resize [extrasize]` Syntax & Hex Rules**
>
> In U-Boot, the syntax is:
> ```text
> fdt resize [extrasize]
> ```
> * **Calling `fdt resize` without arguments**: U-Boot calculates the current tree size and adjusts `totalsize` to fit the current tree plus minimal standard padding (typically 4 KB / 1 memory page).
> * **Calling `fdt resize <extrasize>`**: U-Boot adds `<extrasize>` bytes of additional padding headroom to the existing tree.
> * **Hex vs Decimal Trap**: In the U-Boot command line, **numeric arguments are interpreted in hexadecimal by default**. Writing decimal `65536` can be misinterpreted by U-Boot's parser as `0x65536` (~415 KB) or cause parsing errors on strict builds.
> * Always specify hex padding explicitly: `fdt resize 0x10000` grants exactly **64 KB (`0x10000` bytes)** of extra padding headroom, providing ample capacity for multiple complex overlays and string tables.

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

## 7. Buildroot Integration: Automating the Overlay, Boot Script, and Disk Image Pipeline

A key strength of this architecture is that Buildroot automates the entire compilation, staging, and disk-packaging process. Developers do not need to manually run `dtc`, `mkimage`, `mkenvimage`, or `genimage`—everything is integrated into the Buildroot external tree ([`project-cubie-a5e`](/project-cubie-a5e)).

### The Buildroot Workflow Pipeline

```text
+---------------------------------------------------------------------------------------------------+
|                                  BUILDROOT AUTOMATION PIPELINE                                    |
+---------------------------------------------------------------------------------------------------+
| 1. Out-of-Tree DTS Overlays (project-cubie-a5e/dts-overlay/allwinner/*.dtso)                      |
|    ---> Buildroot Linux package runs dtc -@ ---> ${BINARIES_DIR}/*.dtbo                           |
+---------------------------------------------------------------------------------------------------+
| 2. RootFS Pre-Assembly & Automount (rootfs-overlay & post-build.sh)                               |
|    ---> Copies etc/fstab (/dev/mmcblk0p1 -> /boot) into ${TARGET_DIR}                             |
|    ---> post-build.sh ensures mkdir -p ${TARGET_DIR}/boot exists                                  |
+---------------------------------------------------------------------------------------------------+
| 3. Post-Image Processing (post-image.sh)                                                          |
|    ---> mkimage compiles boot.cmd ---> ${BINARIES_DIR}/boot.scr                                   |
|    ---> mkenvimage compiles uboot-env.txt ---> ${BINARIES_DIR}/uboot.env                         |
|    ---> Copies config.txt and uEnv.txt ---> ${BINARIES_DIR}/                                      |
+---------------------------------------------------------------------------------------------------+
| 4. Final Partition Packaging (genimage.cfg)                                                       |
|    ---> Stitches u-boot-sunxi-with-spl.bin, boot.vfat (with config.txt, dtbos), and rootfs.ext4   |
|    ---> Output: ${BINARIES_DIR}/sdcard.img                                                        |
+---------------------------------------------------------------------------------------------------+
```

### 1. Compiling Out-of-Tree Overlays (`.dtso` -> `.dtbo`)
Buildroot compiles out-of-tree Device Tree Overlays using built-in Linux kernel package hooks. In our defconfig ([`project-cubie-a5e/configs/cubie_a5e_defconfig`](/project-cubie-a5e/configs/cubie_a5e_defconfig)), we configure:

```kconfig
# Base in-tree Device Tree from Linux kernel source:
BR2_LINUX_KERNEL_DTS_SUPPORT=y
BR2_LINUX_KERNEL_INTREE_DTS_NAME="allwinner/sun55i-a527-cubie-a5e"

# Path to custom out-of-tree Device Tree Overlays:
BR2_LINUX_KERNEL_CUSTOM_DTS_DIR="$(BR2_EXTERNAL_CUBIE_A5E_PATH)/dts-overlay"

# Enable overlay compilation (-@ symbols flag):
BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y
```

When Buildroot builds the kernel:
1. It copies all overlay source files (`.dtso`) from `$(BR2_EXTERNAL_CUBIE_A5E_PATH)/dts-overlay/allwinner/` into the kernel build directory.
2. It invokes the Device Tree Compiler (`dtc`) with the `-@` symbols flag to retain symbol fixups.
3. It copies the resulting binary overlays (`cubie-a5e-flight-stack.dtbo`, `cubie-a5e-uio.dtbo`) directly into `${BINARIES_DIR}/`.

### 2. Automating `/boot` Mount in the Root Filesystem (`post-build.sh` & `rootfs-overlay`)
To ensure that `/boot` is ready for the user to edit immediately after boot without manual mount commands:

1. **Rootfs Overlay ([`project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab`](/project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/fstab))**:
   ```fstab
   # /etc/fstab: static file system information.
   /dev/root       /              ext4     rw,noatime        0      1
   /dev/mmcblk0p1  /boot          vfat     defaults          0      2
   proc            /proc          proc     defaults          0      0
   sysfs           /sys           sysfs    defaults          0      0
   ```
   Buildroot copies this file into the target rootfs during filesystem construction.

2. **Post-Build Script ([`project-cubie-a5e/board/radxa/cubie_a5e/post-build.sh`](/project-cubie-a5e/board/radxa/cubie_a5e/post-build.sh))**:
   ```bash
   #!/bin/sh
   TARGET_DIR="$1"

   # Ensure /boot mount point directory exists for FAT boot partition automount
   mkdir -p "${TARGET_DIR}/boot"

   exit 0
   ```
   Configured via `BR2_ROOTFS_POST_BUILD_SCRIPT`, this guarantees the `/boot` mount directory physically exists in `${TARGET_DIR}` before `rootfs.ext4` is generated.

### 3. Staging Boot Artifacts via Post-Image Script (`post-image.sh`)
Once the kernel and root filesystem are built, Buildroot executes the post-image script ([`project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh`](/project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh)):

```bash
#!/bin/sh
BOARD_DIR="$(dirname $0)"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

# 1. Compile boot.cmd into boot.scr using host mkimage (from BR2_PACKAGE_HOST_UBOOT_TOOLS)
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

#### Key Elements of `post-image.sh`:
* **Host Toolchain (`${HOST_DIR}/bin/mkimage` and `mkenvimage`)**: Provided by enabling `BR2_PACKAGE_HOST_UBOOT_TOOLS=y` in Buildroot.
* **Staging `config.txt`**: Because `genimage` only reads files from `--inputpath "${BINARIES_DIR}"`, copying `config.txt` and `uEnv.txt` into `${BINARIES_DIR}` is required so they get included in the FAT filesystem.

### 4. Assembling the Multi-Partition Disk Image (`genimage.cfg`)
Buildroot's host `genimage` tool reads [`project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg`](/project-cubie-a5e/board/radxa/cubie_a5e/genimage.cfg):

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

This constructs a flashable `sdcard.img` with:
* Raw sector offset 8 KB: Mainline U-Boot with SPL (`u-boot-sunxi-with-spl.bin`).
* Offset 4 MB: Partition 1 `boot.vfat` (64 MB FAT32 containing kernel, base DTB, overlays, `config.txt`, `boot.scr`, and `uboot.env`).
* Partition 2: `rootfs.ext4` (Linux ext4 root filesystem).

### 5. Single-Command Build & Flash
To build the complete image:
```bash
# Configure Buildroot with the external tree:
make -C buildroot O=$PWD/bld BR2_EXTERNAL=$PWD/project-cubie-a5e cubie_a5e_defconfig

# Compile kernel, rootfs, overlays, bootloaders, and package disk image:
make -C bld
```

The resulting image is written to `bld/images/sdcard.img` and can be flashed directly to an SD card:
```bash
sudo dd if=bld/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

---

## 8. The Handoff: From U-Boot to the Linux Kernel

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
3. It branches directly to `${kernel_addr_r}` (`0x40200000`), respecting the 2MB ARM64 alignment boundary.

### Early Kernel Ingestion
Inside the Linux kernel:
1. Early assembly (`head.S`) reads register `x0` and verifies the FDT magic number (`0xd00dfeed`).
2. `setup_machine_fdt()` unrolls the tree into the kernel's internal unflattened device tree structure (`struct device_node`).
3. Platform bus drivers (`of_platform_default_populate()`) instantiate drivers matching the merged `compatible` strings.

---

## 9. Runtime Verification in Linux Userspace

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

## 10. Troubleshooting & Common Pitfalls

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
* **Fix**: Update [`project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh`](/project-cubie-a5e/board/radxa/cubie_a5e/post-image.sh) to copy `config.txt` to `${BINARIES_DIR}`:
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
* **Fix**: Execute `fdt resize 0x10000` immediately after `fdt addr ${fdt_addr_r}` to allocate 64 KB of buffer padding headroom.

### 5. `fdt apply` Fails with Error `-1` or `-13` (`-FDT_ERR_NOTFOUND`)
* **Symptom**: Overlay fails to find target nodes with return code `-1` or `-13`.
* **Root Cause**: The base DTB was compiled without `-@` (symbols), omitting the `__symbols__` lookup table required for phandle resolution.
* **Fix**: Ensure `BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y` is enabled in your Buildroot defconfig, or build kernel device trees with `make DTC_FLAGS="-@" dtbs`.

### 6. Kernel Panics with `FDT: bad magic` or Alignment Fault During `booti`
* **Symptom**: Kernel halts immediately during early boot with corrupt FDT magic or early fault.
* **Root Cause**: Memory addresses overlap, or `kernel_addr_r` violates ARM64 2MB alignment.
* **Fix**: Verify memory spacing and alignment:
  - Ensure `kernel_addr_r` is placed at a 2MB-aligned address (`0x40200000`), not an unaligned offset like `0x40080000`.
  - Ensure `${fdt_addr_r}` (`0x4fa00000`) is located well above the kernel memory footprint.

### 7. Changes in `config.txt` Have No Effect (DOS Line Endings & Missing Trailing Newline)
* **Symptom**: Overlays or boot arguments defined in `config.txt` are ignored by U-Boot.
* **Root Cause 1 (CRLF Line Endings on Windows)**: When editing `config.txt` directly on a Windows host PC by inserting the SD card into a card reader (as supported by our FAT32 boot partition), standard Windows text editors like basic Notepad historically save files with DOS/Windows carriage returns (`\r\n`). In U-Boot's `env import -t` parser, the trailing `\r` remains attached to variable values (e.g., `cubie-a5e-uio\r`), causing file load commands to fail looking for non-existent filenames.
* **Root Cause 2 (Missing Trailing Newline)**: If the final line in `config.txt` lacks a terminating newline character, older U-Boot `env import -t` parsers silently discard that final variable.
* **Fix**: 
  - When editing on Windows, use an editor like VS Code, Notepad++, or Sublime Text configured to save with **LF (Unix) line endings** instead of CRLF.
  - Always press <kbd>Enter</kbd> after your final line so the file ends with a clean trailing newline.
  - If troubleshooting on target, convert line endings with `dos2unix /boot/config.txt`.

---

## 11. Summary

By decoupling **low-level bootloader plumbing** (`uboot.env`) from **runtime user configuration** (`config.txt`), we eliminate developer friction while preserving rock-solid boot reliability:

1. **`uboot.env` Stays Untouched**: Serves as the static firmware foundation, initializing serial clocks, DRAM memory maps, and launching `boot.scr`. Developers never need to struggle with binary editors or CRC calculation tools.
2. **`config.txt` Delivers Raspberry Pi Simplicity**: A clean, plain-text configuration file located on the FAT partition. Anyone can enable peripherals, toggle UIO drivers, or isolate CPU cores using standard text editors on Linux, macOS, or Windows.
3. **Buildroot End-to-End Automation**: Buildroot handles the complete workflow out-of-the-box—compiling out-of-tree `.dtso` fragments with `-@`, staging `config.txt` into `${BINARIES_DIR}`, pre-configuring `/boot` mounts in `/etc/fstab`, and packaging a turnkey `sdcard.img` ready to flash.
4. **Deterministic Boot Pipeline**: U-Boot's `env import -t` dynamically bridges the configuration into memory, expands the base Device Tree with `fdt resize 0x10000`, applies overlays via `fdt apply`, and passes an immutable hardware contract to Linux via ARM64 register `x0` at 2MB-aligned `0x40200000`.
