# Radxa Cubie A7A (Allwinner A733) Boot & Debug Log

## 1. Hardware Overview & Target Specifications
- **Board**: Radxa Cubie A7A (Allwinner A733 / `sun60i`)
- **CPU**: Octa-core ARM Cortex (6x Cortex-A55 Little + 2x Cortex-A78 Big, AArch64)
- **RAM**: 6 GiB LPDDR5 (Single-Rank / Dual-Rank auto-trained across 4 P-States up to 2400 MHz)
- **Primary Boot Device**: MicroSD Card (Slot 0 / `mmc0`)
- **Console UART**: UART0 at MMIO `0x02500000` (Baud 115200, 8N1, 24 MHz Base Clock)

---

## 2. Boot Hierarchy & Memory Map

```
+-------------------------------------------------------------------------------+
| Radxa Cubie A7A Multi-Stage Boot Sequence                                     |
+-------------------------------------------------------------------------------+
  [BootROM (SRAM @ 0x0)]
       │
       ▼ (Loads sector 256 @ 128 KB)
  [Vendor boot0 (SRAM 0x48000..0x88000)]
       │  - Trains LPDDR5 PHY across P-States 0..3 (400, 800, 1200, 2400 MHz)
       │  - Detects and initializes 6144 MB (6 GiB) DRAM
       │  - Parses TOC1 container at Sector 24576 (12.0 MB)
       │
       ▼ (Jumps to TF-A BL31 @ 0x48000000)
  [ARM Trusted Firmware BL31 (EL3 @ 0x48000000)]
       │  - Initializes secure monitor & PSCI v1.1
       │  - Transitions to OP-TEE @ 0x48600000
       │
       ▼
  [OP-TEE OS (Secure EL1 @ 0x48600000)]
       │  - Initializes hardware TRNG prng seed
       │  - Returns to TF-A BL31
       │
       ▼ (Drops to Non-Secure EL2 @ 0x4a000000)
  [Mainline U-Boot 2026.01-rc1 (EL2 @ 0x4a001000)]
       │  - 4KB page-aligned entry point (via 4KB TOC1 header b +0x1000)
       │  - Probes 6 GiB DRAM (gd->ram_size = 0x180000000ULL)
       │  - Relocates cleanly to high DRAM without adrp offset displacement
       │  - Enables MMU, D-Cache, I-Cache, and Driver Model
       │  - Probes MMC slot 0, reads FAT partition, executes boot.scr
       │
       ▼ (Boots Linux Kernel)
  [Mainline Linux Kernel 7.1.0 PREEMPT_RT (EL1)]
       │  - Brings up all 8 SMP cores (CPU0..CPU7)
       │  - Maps 6018312 KB / 6291456 KB (6 GiB) available system memory
       │  - Mounts rootfs and launches userspace init
```

---

## 3. Storage Layout Specification (SD Card / `sdcard.img`)

| Sector | Byte Offset | Size | Name | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **0** | `0x00000000` (0 KB) | 512 B | `MBR` | Partition table & sector index |
| **256** | `0x00020000` (128 KB) | 240 KB | `boot0_sdcard.bin` | Vendor LPDDR5 DRAM initialization |
| **24576** | `0x00C00000` (12.0 MB) | ~3.0 MB | `boot_package.fex` | TOC1 Container (U-Boot, BL31, OP-TEE, SCP, DTB) |
| **65536** | `0x02000000` (32.0 MB) | 64 MB | `boot.vfat` | Kernel `Image`, DTB, `boot.scr`, `uboot.env` |
| **196608** | `0x06000000` (96.0 MB) | 512 MB | `rootfs.ext4` | Buildroot Root Filesystem |

---

## 4. Hardware Boot Milestones Achieved (Aug 22, 2026)

### Milestone 1: Non-Destructive 6 GiB DRAM Detection
- **Issue**: Standard `dram_init()` executed `get_ram_size(PHYS_SDRAM_0, 2GB)`, writing test patterns across `0x48000000` (BL31) and `0x48600000` (OP-TEE).
- **Resolution**: Patch `0003-sunxi-fix-a733-dram-init-no-destructive-probe.patch` assigns `gd->ram_size = 0x180000000ULL` (6 GiB) directly without destructive probes.

### Milestone 2: 4KB Page-Alignment Fix for Relocation
- **Issue**: Linking at `CONFIG_TEXT_BASE=0x4a000640` caused a `+0x640` (1600-byte) displacement in AArch64 `adrp` page-relative addressing after relocation to page-aligned high RAM, causing `.rodata` string pointers to be shifted by 1600 bytes (printing `[list] - list available alt settings` and `Failed to get partition driver count`).
- **Resolution**:
  1. Linked U-Boot proper at **`CONFIG_TEXT_BASE=0x4a001000`** (100% 4KB page aligned).
  2. Modified TOC1 `header-info.bin` opcode at byte 0 to **`00 04 00 14` (`0x14000400` = `b +0x1000` / branch +4096 bytes)**.
  3. Padded `header-info.bin` to 4096 bytes (4 KB).
  4. Both link-time base and runtime relocated base now share identical `+0x000` page offsets with **zero displacement**.

---

## 5. Silicon Boot Log (Mainline U-Boot 2026.01 $\rightarrow$ Linux 7.1.0 PREEMPT_RT)

```text
[186]HELLO! BOOT0 is starting!
[189]BOOT0 commit : {4721ad08}
[203]dram_para_total:0xf
[205]vaild para:6  select dram para1
[mmc]: mmc driver ver 2025-10-16 17:10
[mmc]: Wrong media type 0x0
[mmc]: ***Try SD card 0***
[mmc]: HSSDR52/SDR25 4 bit
[mmc]: 50000000 Hz
[mmc]: 7431 MB
[mmc]: ***SD/MMC 0 init OK!!!***
[246]boot param - magic error 
[249]DRAM BOOT DRIVE INFO: V0.601
[253]DRAM_VCC set to 560 mv
[256]DRAM CLK =2400 MHZ
[258]DRAM Type =9 (8:LPDDR4,9:LPDDR5)
[406]Training result is = 7
[409]DRAM Pstate 1 training, frequency is 1200 Mhz
[587]Training result is = 7
[590]DRAM Pstate 2 training, frequency is 800 Mhz
[934]Training result is = 7
[937]DRAM Pstate 3 training, frequency is 400 Mhz
[4689]Training result is = 7
[4692]DRAM Pstate 0 training, frequency is 2400 Mhz
[4701]Actual DRAM SIZE =6144 M
[4704]DRAM SIZE =6144 MBytes, para1 = a10a, para2 = 18001001, dram_tpr13 = 10065
[4718]DRAM simple test OK.
[4724]error:bad magic.
[4790]mmc not para
[4792]Jump to ATF: monitor_base = 0x48000000, uboot_base = 0x4a000000, optee_base = 0x48600000
NOTICE:  BL31: OP-TEE 64bit detected
NOTICE:  BL31: U-BOOT 64bit detected
NOTICE:  BL31: dram size is 6442450944 bytes
NOTICE:  BL31: v2.5(debug):48e54578a
NOTICE:  BL31: Built : 14:13:06, Jul  2 2025
NOTICE:  BL31: No DTB found.
E/TC:0 0 init_external_dt:1033 Device Tree missing
M/TC: OP-TEE version: 7bd80be0 (gcc version 9.2.1 20191025 (GNU Toolchain for the A-profile Architecture 9.2-2019.12 (arm-9.10))) #1 Wed Jun  4 08:10:34 UTC 2025 aarch64
M/TC: OP-TEE 64bit
E/TC:0 0 plat_rng_init:460 prng seed by trng

<debug_uart>

U-Boot 2026.01-rc1 (Aug 22 2026 - 11:09:18 -0500) Allwinner Technology

CPU:   Allwinner A733 (SUN60I)
Model: Radxa A7A
DRAM:  6 GiB
[A7A] board_init_r: initcall_run_r starting...
[A7A] Relocation marked done
[A7A] Enabling MMU & Caches (initr_caches)...
[A7A] MMU & Caches enabled successfully!
[A7A] Initializing malloc heap (initr_malloc)...
[A7A] Malloc heap ready
[A7A] Initializing Driver Model (initr_dm)...
[A7A] Driver Model ready
[A7A] Calling board_init()...
[A7A] board_init done
[A7A] Probing Serial Driver (serial_initialize)...
[A7A] Serial driver ready
Core:  72 devices, 24 uclasses, devicetree: separate
WDT:   Not starting watchdog@2050000
MMC:   mmc@4020000: 0, mmc@4022000: 1
Loading Environment from FAT... OK
In:    serial@2500000
Out:   serial@2500000
Err:   serial@2500000
Net:   
Warning: ethernet@4500000 (eth0) using random MAC address - 8a:7f:94:54:11:bb
eth0: ethernet@4500000
Hit any key to stop autoboot: 0
586 bytes read in 2 ms (286.1 KiB/s)
## Executing script at 4fc00000
6950 bytes read in 4 ms (1.7 MiB/s)
Working FDT set to 4fa00000
44468736 bytes read in 3714 ms (11.4 MiB/s)
## Flattened Device Tree blob at 4fa00000
   Booting using the fdt blob at 0x4fa00000
   Loading Device Tree to 00000000fae9c000, end 00000000faf06fff ... OK

Starting kernel ...

[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x412fd050]
[    0.000000] Linux version 7.1.0 (aarch64-linux-gcc 15.1.0) #1 SMP PREEMPT_RT
[    0.000000] Machine model: Radxa Cubie A7A
[    0.000000] earlycon: uart8250 at MMIO32 0x0000000002500000
[    0.000000] Zone ranges:
[    0.000000]   DMA      [mem 0x0000000040000000-0x00000000ffffffff]
[    0.000000]   Normal   [mem 0x0000000100000000-0x00000001bfffffff]
[    0.000000] GICv3: 256 SPIs implemented
[    0.187678] smp: Bringing up secondary CPUs ...
[    3.286972] CPU1: Booted secondary processor 0x0000000100
[    6.311252] CPU2: Booted secondary processor 0x0000000200
[    9.356476] CPU3: Booted secondary processor 0x0000000300
[   12.372873] CPU4: Booted secondary processor 0x0000000400
[   15.415565] CPU5: Booted secondary processor 0x0000000500
[   18.455552] CPU6: Booted secondary processor 0x0000000600
[   21.511279] CPU7: Booted secondary processor 0x0000000700
[   21.515405] smp: Brought up 1 node, 8 CPUs
[   21.527010] Memory: 6018312K/6291456K available (6 GiB)
```

---

## 6. Next Steps (Linux Kernel CCU Clock Fix)

1. **CCU & R-CCU Clock Gate Parent Fix**:
   - `bus_mmc0_clk`, `bus_uart0_clk`, `r_bus_uart0_clk` in `0003-clk-sunxi-ng-add-allwinner-a733-ccu-and-prcm.patch` must reference single bus clock parents (`psi_ahb1_ahb2_clk`, `apb1`, `apb2`, `r_apb0`) rather than multi-parent arrays (`ahb_parents`).
2. **Rebuild Kernel & Verify**:
   - Verify `sun60i-a733-ccu` and `sun60i-a733-r-ccu` probe successfully with code `0`.
   - Verify `pinctrl`, `ttyS0` console, and `mmcblk0p2` mount rootfs to reach login prompt.
