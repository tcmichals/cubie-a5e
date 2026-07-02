# Allwinner A527 Boot Process and Partition Layout

This document details the low-level boot sequence of the Cubie-A5E (Allwinner A527) processor, explains the architectural choices made for the SD card partition layout in our Buildroot configuration, and documents solutions to common boot failures.

## 1. The Boot Sequence

When the Cubie-A5E board is powered on, the processor goes through several distinct stages to boot the Linux kernel. Understanding this flow is critical when debugging boot issues.

### Stage 1: Boot ROM (BROM)
Built directly into the Allwinner silicon, the BROM is the very first code to execute. It scans the connected storage devices (SD card, eMMC, SPI flash) looking for a valid boot signature. For SD cards, it specifically looks for a header named `eGON.BT0` exactly at the **8KB** offset (sector 16).

### Stage 2: Secondary Program Loader (SPL)
The BROM loads the SPL (which we provide via `u-boot-sunxi-with-spl.bin`) from that 8KB offset into the processor's tiny internal SRAM. The SPL is a stripped-down version of U-Boot. Its primary job is to initialize the main DDR RAM, because the BROM doesn't know how to talk to external memory.

### Stage 3: TF-A and U-Boot Proper
Once the DDR RAM is initialized, the SPL loads the Trusted Firmware-A (TF-A) and the full U-Boot binary (often bundled together using a `TOC0` header). TF-A handles secure hardware initialization (like setting up TrustZone). Afterwards, execution is handed off to U-Boot Proper.

### Stage 4: U-Boot Script (`boot.scr`)
U-Boot Proper initializes the SD card filesystem drivers, mounts the first valid partition, and looks for a boot script (usually `boot.scr`). It executes this script, which tells it exactly where to find the Linux kernel, device tree, and overlays.

### Stage 5: Linux Kernel
U-Boot loads the Linux `Image` and the `.dtb` into RAM, applies any `.dtbo` overlays, and transfers control to the Linux kernel, passing along boot arguments (like `root=/dev/mmcblk0p2`). Linux then mounts the root filesystem (rootfs) and starts the init system.

---

## 2. Partition Architecture: VFAT vs EXT4

Our Buildroot configuration (`genimage.cfg`) splits the SD card into two partitions:
1. `boot` (FAT32/vfat) - Contains the Linux Kernel, DTB, and U-Boot script.
2. `rootfs` (EXT4) - Contains the Linux root filesystem.

In contrast, other distributions like Armbian often use a single unified `ext4` partition containing both `/boot` and `/`. 

### Why we use a separate VFAT partition
While both approaches work, using a dedicated `vfat` partition offers several significant advantages for embedded development:
- **Universal OS Compatibility:** If you plug the SD card into a Windows or macOS machine, the `vfat` partition mounts instantly as a standard thumb drive. This allows developers and end-users to easily modify `boot.scr`, change kernel parameters, or swap out the device tree without needing a Linux environment to read an `ext4` filesystem.
- **U-Boot Reliability:** While U-Boot can be configured to read `ext4`, reading `vfat` is U-Boot's most native and extensively tested filesystem. It requires fewer resources and is less prone to failing if the filesystem wasn't unmounted cleanly.
- **Corruption Isolation:** Embedded systems often suffer unexpected power loss. Because the `vfat` boot partition is typically mounted read-only (or rarely written to) by Linux, it is completely immune to the filesystem corruption that can occasionally happen on the `ext4` rootfs during sudden power cuts. The board is guaranteed to at least reach U-Boot and the kernel.

---

## 3. Critical Configuration Constraints

When packaging the final `sdcard.img` with `genimage`, two critical parameters must be respected to avoid breaking the boot sequence.

### Bootloader Offset (4MB Buffer)
The Allwinner bootloader blob (`u-boot-sunxi-with-spl.bin`) is large (~772KB). If the first partition is placed too close to the beginning of the SD card (e.g., at `1MB`), the bootloader headers and the partition table will overwrite each other.

**Solution:** The first partition must be pushed back to leave enough raw, unpartitioned space for U-Boot. We use a **4MB offset** (sector 8192) to guarantee a safe buffer.
```cfg
    partition boot {
        partition-type = 0xC
        bootable = "true"
        image = "boot.vfat"
        offset = 4M  # <-- CRITICAL
    }
```

### Device Tree RAM Buffer
When U-Boot applies dynamic overlays (like `cubie-a5e-flight-stack.dtbo`), it needs enough working memory (RAM) to stitch the blocks together. By default, `fdt resize 8192` only allocates 8KB, which will cause U-Boot to crash.

**Solution:** Always allocate at least 64KB (`65536` bytes) in `boot.cmd` before applying overlays:
```bash
fdt addr ${fdt_addr_r}
fdt resize 65536  # <-- CRITICAL
fdt apply ${ramdisk_addr_r}
```
