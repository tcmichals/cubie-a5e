setenv bootargs console=ttyS0,115200 earlycon=uart8250,mmio32,0x02500000 root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8 keep_bootcon clk_ignore_unused
setenv kernel_addr_r 0x40200000
setenv fdt_addr_r 0x4fa00000

# Ground-truth dump of CCU interconnect/USB gate state left by the boot chain
echo === CCU gate state (AHB 05C0 / MBUS 05E0,05E4 / MSI2 05A4 / DCAP 1A00 / USB 1300..135C) ===
md.l 0x020025c0 1
md.l 0x020025e0 2
md.l 0x020025a4 1
md.l 0x02003a00 1
md.l 0x02003300 4
md.l 0x0200335c 1

# Load the base device tree into memory slots
load mmc 0:1 ${fdt_addr_r} sun60i-a733-cubie-a7a.dtb
fdt addr ${fdt_addr_r}
fdt resize 65536

# Load the main uncompressed Linux 7.1 kernel binary and execute initialization
load mmc 0:1 ${kernel_addr_r} Image
booti ${kernel_addr_r} - ${fdt_addr_r}
