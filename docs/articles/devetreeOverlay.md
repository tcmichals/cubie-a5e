# Dynamic Device Tree Overlays in U-Boot: From `config.txt` to Linux Kernel Handoff

*A Technical Guide to In-Memory Device Tree Merging, Dynamic Configuration Files, and the Bootloader-to-Kernel Pipeline*

* **Source Repository**: [https://github.com/tcmichals/cubie-a5e](https://github.com/tcmichals/cubie-a5e)

---

## 1. Introduction & The Architecture Problem

In modern embedded Linux systems—particularly heterogeneous architectures pairing multi-core ARM application processors with co-processors or dynamic expansion headers—peripheral configurations change depending on the deployment scenario:

* **Scenario A (Avionics / Flight Stack)**: Dedicated hardware UARTs and SPI buses are assigned to a real-time co-processor (e.g., XuanTie E907 RISC-V); onboard sensors (IMU, barometer, compass) are enabled on host I2C buses.
* **Scenario B (Userspace Driver / UIO)**: Hardware mailboxes or custom peripherals are detached from kernel subsystem drivers and rebound to generic Userspace I/O (`generic-uio`) drivers.
* **Scenario C (General-Purpose IO)**: Header pins are exposed as standard `/dev/spidev` and GPIO lines for prototyping.

### The Monolithic DTB Anti-Pattern
Historically, supporting three configurations meant building three separate monolithic Device Tree Blobs (`board-flight.dtb`, `board-uio.dtb`, `board-gpio.dtb`). This creates significant drawbacks:
1. **Combinatorial Explosion**: If you have 3 sensor configurations and 2 IPC modes, you must build and maintain 3 × 2 = 6 full DTBs.
2. **Maintenance Overhead**: Any upstream kernel update to core clocks, power domains, or memory controller nodes must be duplicated across every custom DTB.
3. **Flashing Friction**: Switching configurations requires rewriting partition images or updating static bootloader environments.

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

## 2. Anatomy of a Device Tree Overlay (`.dtso`)

Before diving into the bootloader script, we must understand how Device Tree Overlays are structured and compiled.

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

Here is an example overlay (`cubie-a5e-uio.dtso`) that modifies an existing base node and adds memory regions:

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

## 3. The User Configuration Layer: Raspberry Pi-Style `config.txt`

The most user-friendly embedded systems do not require developers to type commands into a serial console or compile proprietary script formats just to enable an overlay.

By placing a plain-text `config.txt` file on the FAT partition (`/boot/config.txt`), configuration becomes as simple as editing a text file on any Windows, macOS, or Linux machine:

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

---

## 4. Deconstructing the U-Boot Boot Engine (`boot.cmd` -> `boot.scr`)

U-Boot executes a compiled command script (`boot.scr`) created from a plain-text source script (`boot.cmd`) using `mkimage`:
```bash
mkimage -A arm64 -T script -C none -d boot.cmd boot.scr
```

Below is the complete, production-grade `boot.cmd` script used on the Radxa Cubie A5E / A7A platforms. We will analyze every stage line-by-line.

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

#### 1. Memory Map Allocation & Reservation
In Allwinner 64-bit systems, system DRAM begins at physical address `0x40000000`. To prevent memory corruption when loading uncompressed kernel images, base device trees, and overlays simultaneously, distinct address slots are allocated:

* **`${kernel_addr_r}` (`0x40080000`)**: Staging buffer for the uncompressed ARM64 kernel Image (~30 MB allocation headroom).
* **`${fdt_addr_r}` (`0x4fa00000`)**: Base Device Tree Blob (`sun55i-a527-cubie-a5e.dtb`), expanded in-place by `fdt resize`.
* **`${fdtoverlay_addr_r}` (`0x4fe00000`)**: Staging buffer for loading `.dtbo` overlay fragments (reused sequentially).
* **`${ramdisk_addr_r}` (`0x4ff00000`)**: Scratchpad buffer for `config.txt` text parsing via `env import`, and initrd/ramdisk if present.
Each buffer has several megabytes of clearance, eliminating any risk of `kernel` data overwriting the `fdt` or vice versa.

#### 2. Parsing the Configuration File (`env import -t`)
```sh
if load mmc 0:1 ${ramdisk_addr_r} config.txt; then
    echo ">>> Found Raspberry Pi-style config.txt! Importing configuration..."
    env import -t ${ramdisk_addr_r} ${filesize}
fi
```
The U-Boot `env import -t <addr> <size>` command parses text files containing `KEY=VALUE` pairs separated by newlines. 
* When `config.txt` contains `dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio`, U-Boot creates an environment variable `${dtoverlay}` with that string.
* When it contains `cmdline=isolcpus=7`, U-Boot creates `${cmdline}`.
* The script checks for `config.txt` first, and gracefully falls back to legacy `uEnv.txt` if not present.

#### 3. Why `fdt resize` is Strictly Mandatory
```sh
fdt addr ${fdt_addr_r}
fdt resize 65536
```
This is the single most common point of failure in embedded overlay implementations!
* A compiled base DTB is generated with a fixed header field `totalsize` matching its exact byte length (e.g., 52,480 bytes).
* When U-Boot executes `fdt apply`, `libfdt` attempts to insert new nodes, properties, and strings into the tree.
* **If the buffer is not resized, `libfdt` returns `-FDT_ERR_NOSPACE` (`-3`) and the overlay fails.**
* `fdt resize 65536` expands the active device tree buffer by 64 KB (or any specified padding), allocating extra headroom for strings and node descriptors.

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

## 5. The Handoff: From U-Boot to the Linux Kernel

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

## 6. Runtime Verification in Linux Userspace

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

## 7. Troubleshooting & Common Pitfalls

### 1. `fdt apply` fails with error `-3` (`-FDT_ERR_NOSPACE`)
* **Symptom**: Overlay fails to apply with return code `-3`.
* **Root Cause**: The base DTB buffer in RAM was not expanded before applying overlays.
* **Fix**: Execute `fdt resize 65536` immediately after `fdt addr ${fdt_addr_r}` to allocate buffer headroom.

### 2. `fdt apply` fails with error `-13` (`-FDT_ERR_NOTFOUND`)
* **Symptom**: Overlay fails to find target nodes with return code `-13`.
* **Root Cause**: The base DTB was compiled without `-@` (symbols), preventing phandle resolution.
* **Fix**: Ensure `BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y` is enabled in your Buildroot defconfig so `dtc` runs with the `-@` flag.

### 3. Kernel panics with `FDT: bad magic` during `booti`
* **Symptom**: Kernel halts immediately during early boot with corrupt FDT magic.
* **Root Cause**: Memory addresses overlap (e.g., kernel uncompression overwrote the FDT buffer).
* **Fix**: Verify memory spacing. Ensure `${fdt_addr_r}` (`0x4fa00000`) is located well above the kernel memory footprint (`0x40080000`).

### 4. Changes in `config.txt` have no effect
* **Symptom**: Overlays listed in `config.txt` are ignored by U-Boot.
* **Root Cause**: The file was saved with Windows DOS carriage returns (`\r\n`), corrupting variable names.
* **Fix**: Save `config.txt` with standard UNIX line endings (`\n`) and ensure `env import -t ${ramdisk_addr_r} ${filesize}` is executed.

---

## 8. Summary

By replacing hardcoded boot scripts with a **dynamic multi-overlay boot engine** and adopting the **Raspberry Pi `/boot/config.txt` paradigm**, we achieve:

1. **Zero-Friction Hardware Configuration**: Users toggle peripherals, expansion boards, and IPC modes by simply editing a plain-text file on the SD card.
2. **Deterministic Memory Management**: Proper address separation and `fdt resize` guarantee stable, bug-free in-memory overlay merging.
3. **Clean Bootloader-to-Kernel Handoff**: The merged device tree is passed transparently via ARM64 architectural register `x0`, resulting in a seamless transition to Linux.

---


