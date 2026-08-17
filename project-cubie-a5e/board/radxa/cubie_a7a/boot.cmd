setenv bootargs console=ttyS0,115200 earlycon root=/dev/mmcblk0p2 rootwait panic=10 iomem=relaxed isolcpus=7 nohz_full=7 rcu_nocbs=7

# Load the base device tree into memory slots
load mmc 0:1 ${fdt_addr_r} sun60i-a733-cubie-a7a.dtb

# Load your custom flight overlay mapping file
load mmc 0:1 ${ramdisk_addr_r} cubie-a7a-flight-stack.dtbo

# Instruct U-Boot to overlay the blocks dynamically in memory
fdt addr ${fdt_addr_r}
fdt resize 65536
fdt apply ${ramdisk_addr_r}

# Load the main uncompressed Linux 7.1 kernel binary and execute initialization
load mmc 0:1 ${kernel_addr_r} Image
booti ${kernel_addr_r} - ${fdt_addr_r}
