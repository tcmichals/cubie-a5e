# ==============================================================================
# Radxa Cubie A7A Dynamic Multi-Overlay Boot Script (boot.cmd -> boot.scr)
# Supports Raspberry Pi-style config.txt & standard uEnv.txt
# ==============================================================================

echo "=== Initializing Radxa Cubie A7A Dynamic Boot Sequence ==="

setenv bootargs "console=ttyS0,115200 earlycon=uart8250,mmio32,0x02500000 root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8 keep_bootcon clk_ignore_unused"

# Standard Memory Map Addresses
if test -z "${kernel_addr_r}";     then setenv kernel_addr_r     0x40200000; fi
if test -z "${fdt_addr_r}";        then setenv fdt_addr_r        0x4fa00000; fi
if test -z "${fdtoverlay_addr_r}"; then setenv fdtoverlay_addr_r 0x4fe00000; fi
if test -z "${ramdisk_addr_r}";    then setenv ramdisk_addr_r    0x4ff00000; fi

setenv base_dtb sun60i-a733-cubie-a7a.dtb
setenv overlays "cubie-a7a-flight-stack"

# Ground-truth dump of CCU interconnect/USB gate state left by the boot chain
echo === CCU gate state (AHB 05C0 / MBUS 05E0,05E4 / MSI2 05A4 / DCAP 1A00 / USB 1300..135C) ===
md.l 0x020025c0 1
md.l 0x020025e0 2
md.l 0x020025a4 1
md.l 0x02003a00 1
md.l 0x02003300 4
md.l 0x0200335c 1

# Check for Raspberry Pi-style config.txt first, then armbianEnv.txt, then uEnv.txt
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

if test -n "${dtoverlay}"; then
    setenv overlays "${dtoverlay}"
fi

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

# Load base Device Tree into memory
echo ">>> Loading Base Device Tree: ${base_dtb}..."
if load mmc 0:1 ${fdt_addr_r} ${base_dtb}; then
    fdt addr ${fdt_addr_r}
    fdt resize 65536
else
    echo "ERROR: Failed to load base DTB ${base_dtb}!"
    reset
fi

# Dynamically iterate and apply each Device Tree Overlay in ${overlays}
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

# Load Linux kernel Image and boot
echo ">>> Loading Linux Kernel Image..."
if load mmc 0:1 ${kernel_addr_r} Image; then
    echo ">>> Booting Linux Kernel with Dynamic Overlays..."
    booti ${kernel_addr_r} - ${fdt_addr_r}
else
    echo "ERROR: Failed to load Linux Kernel Image!"
    reset
fi
