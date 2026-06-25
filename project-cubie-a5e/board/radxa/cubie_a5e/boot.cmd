# Set up kernel command execution strings
setenv bootargs console=ttyS0,115200 root=/dev/mmcblk0p2 rootwait panic=10

# Load the base device tree into memory slots
load mmc 0:1 ${fdt_addr_r} sun55i-a527-cubie-a5e.dtb

# Load your custom flight overlay mapping file
load mmc 0:1 ${ramdisk_addr_r} cubie-a5e-flight-stack.dtbo

# Instruct U-Boot to overlay the blocks dynamically in memory
fdt addr ${fdt_addr_r}
fdt resize 8192
fdt apply ${ramdisk_addr_r}

# Load the main uncompressed Linux 7.1 kernel binary and execute initialization
load mmc 0:1 ${kernel_addr_r} Image
booti ${kernel_addr_r} - ${fdt_addr_r}

