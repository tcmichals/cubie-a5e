setenv bootargs console=ttyS0,115200 earlycon=uart8250,mmio32,0x02500000 root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8
setenv kernel_addr_r 0x40200000
setenv fdt_addr_r 0x4fa00000

# Load the base device tree into memory slots
load mmc 0:1 ${fdt_addr_r} sun60i-a733-cubie-a7z.dtb
fdt addr ${fdt_addr_r}
fdt resize 65536

# Load the main uncompressed Linux 7.1 kernel binary and execute initialization
load mmc 0:1 ${kernel_addr_r} Image
booti ${kernel_addr_r} - ${fdt_addr_r}
