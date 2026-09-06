# ==============================================================================
# Yuzuki / Pine64 Avaota A1 Dynamic Multi-Overlay Boot Script (boot.cmd -> boot.scr)
# Supports Raspberry Pi-style config.txt, Armbian armbianEnv.txt, & uEnv.txt
# ==============================================================================

echo "=== Initializing Avaota A1 Dynamic Boot Sequence ==="

# 1. Base boot arguments (UART console, rootfs, panic handling)
setenv bootargs "console=ttyS0,115200 earlycon root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8"

# 2. Standard Memory Map Addresses (Allwinner 64-bit DRAM base 0x40000000)
if test -z "${kernel_addr_r}";     then setenv kernel_addr_r     0x40200000; fi
if test -z "${fdt_addr_r}";        then setenv fdt_addr_r        0x4fa00000; fi
if test -z "${fdtoverlay_addr_r}"; then setenv fdtoverlay_addr_r 0x4fe00000; fi
if test -z "${ramdisk_addr_r}";    then setenv ramdisk_addr_r    0x4ff00000; fi

# 3. Default base DTB for Avaota A1
setenv base_dtb sun55i-t527-avaota-a1.dtb

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
    fdt resize 0x10000
else
    echo "ERROR: Failed to load base DTB ${base_dtb}!"
    reset
fi

# 8. Dynamically iterate and apply each Device Tree Overlay in ${overlays}
if test -n "${overlays}"; then
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
fi

# 9. Load Linux kernel Image and boot
echo ">>> Loading Linux Kernel Image..."
if load mmc 0:1 ${kernel_addr_r} Image; then
    echo ">>> Booting Linux Kernel on Avaota A1..."
    booti ${kernel_addr_r} - ${fdt_addr_r}
else
    echo "ERROR: Failed to load Linux Kernel Image!"
    reset
fi
