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

### Milestone 3: Kernel Boot CPU Hang Resolution (Aug 23, 2026)
- **Issue**: Kernel boot hung at `[ 8.324s ]` when bringing up CPU 7 with `isolcpus=7` in `bootargs`.
- **Root Cause**: Early kernel RCU grace period synchronization threads stalled waiting for response on isolated core 7 before userspace task isolation was established.
- **Resolution**: Removed hardcoded `isolcpus=7` from kernel command line; implemented dynamic userspace task isolation via `cpuset` and `taskset -c 7` in user space.

### Milestone 4: TrustZone Secure SRAM Conflict Resolution
- **Issue**: Kernel crashed at `[ 0.595s ]` during `sunxi_sram.c` driver probe.
- **Root Cause**: `syscon@3000000` contained child nodes mapping Secure SRAM (`sram_a` / `sram_c`). In ARM TrustZone (TF-A BL31), non-secure EL1 read/write accesses to secure memory regions trigger synchronous hardware aborts.
- **Resolution**: Configured `syscon@3000000` purely as a system control regmap provider for GMAC EMAC clock delays without exposing TrustZone-reserved SRAM children to Linux.

### Milestone 5: Subsys Initcall Unclocked Bus Stalls
- **Issue**: Boot froze at `[ 0.624s ]` directly after `NET: Registered PF_NETLINK/PF_ROUTE`.
- **Root Cause**: `snps,designware-i2c` in `drivers/i2c/busses/i2c-designware-platdrv.c` executed `i2c_dw_configure()` at line 175, reading hardware identification registers at MMIO `0x07083000` *before* the PRCM bus clock was prepared/enabled at line 186. Accessing unclocked peripheral bus registers stalled the SoC memory fabric.
- **Resolution**: Restored `allwinner,sun60i-a733-i2c`, `allwinner,sun6i-a31-i2c` binding which guarantees proper clock and reset sequencing prior to register access.

### Milestone 6: 8-Core Heterogeneous SMP & Real-Time RT Verification
- **Achievement**: Booted **Mainline Linux 7.1.0 `PREEMPT_RT`** into ext4 rootfs in **3.9 seconds**.
- **Silicon Verification**:
  * 6x Cortex-A55 efficiency cores (Part `0xd05`) + 2x Cortex-A78 performance cores (Part `0xd0b`) all online.
  * GICv3 PPI 27 architectural timer generating independent 1,000 Hz RT interrupts across all 8 cores.
  * XuanTie E907 RISC-V co-processor registered in Linux sysfs (`/sys/class/remoteproc/remoteproc0`).
  * Hardware power regulators verified via `gpioinfo` (`PL2` USB0 VBUS, `PM0` Wi-Fi Power, `PM5` USB Hub Power).

### Milestone 7: Gigabit Ethernet & USB PHY / Wi-Fi 6 Subsystem Integration
- **Ethernet**: Added `gmac0: ethernet@4500000` with `syscon@3000000` regmap and Motorcomm `MAE0621A` PHY reset on `PH16`; compiled `CONFIG_DWMAC_SUN8I=y` and `CONFIG_MOTORCOMM_PHY=y` built-in.
- **USB & Wi-Fi**: Added `usbphy: phy@4100400` (`sun20i-d1-usb-phy`) with `CONFIG_PHY_SUN4I_USB=y` to drive the analog transceivers for `ehci1`, the FE1.1S 4-port hub, and the onboard **AIC8800 Wi-Fi 6 chip** (`0xA69C:0x8800`).
- **RISC-V Firmware**: Packaged compiled XuanTie E907 binary into `/lib/firmware/riscv-firmware.elf`.

### Milestone 8: Peripheral Timing & Reset Collision Resolutions (Aug 23, 2026)

#### 1. Phylink RGMII Internal Delay Timing (`-EINVAL`)
- **Symptom**: `dwmac-sun8i 4500000.ethernet eth0: validation of rgmii with support ... failed: -EINVAL` / `cannot attach to PHY`.
- **Root Cause**: The Motorcomm `MAE0621A-Q3C` Gigabit PHY handles the required 2.0ns clock phase delay internally. In Linux 7.1 `phylink`, specifying `phy-mode = "rgmii"` requires the MAC controller to provide external delay configuration via `syscon`. Since no SoC MAC delay registers were defined, `phylink_validate_phy()` rejected the uncompensated clock mode with `-EINVAL`.
- **Resolution**: Updated Device Tree to `phy-mode = "rgmii-id"`. This instructs `phylink` to delegate the 2ns RX/TX delay generation to the Motorcomm PHY hardware, enabling `eth0` to attach cleanly with MAC address assignment and IRQ 155.

#### 2. USB Reset Line Exclusive Lock Collision (`-EBUSY` / Error 16)
- **Symptom**: `ehci-platform 4101000.usb: probe with driver ehci-platform failed with error -16`.
- **Root Cause**: On modern Allwinner silicon (A733 / A523), the CCU does not provide separate `RST_USB_PHY` registers; the entire USB subsystem (PHY transceiver + Host DMA engine) is gated by the master bus reset `RST_BUS_USB0` / `RST_BUS_USB1`. In Linux, `phy-sun4i-usb.c` requests exclusive reset locks via `devm_reset_control_get()`. When `usbphy` claimed `RST_BUS_USB0`, subsequent probe by `ehci-platform` was blocked by the reset framework with `-EBUSY` (Error 16).
- **Resolution**: Removed the duplicate reset requests from `usbphy` in the DTS. The primary Host Controller (`ehci0`/`ehci1`) retains exclusive ownership of `RST_BUS_USB0`/`RST_BUS_USB1`, deasserting bus and transceiver reset simultaneously during controller initialization without driver contention.

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

## 6. Forensic Discoveries & Silicon Register Realignment (Datasheet V0.93)

During kernel initialization on hardware, the system experienced intermittent stalls at `clk: Disabling unused clocks` and `Waiting for root device /dev/mmcblk0p2`. Forensic comparison between the decompiled vendor tree ([`vendor-a733-reference/`](file:///home/tcmichals/projects/cubie/vendor-a733-reference/)) and the **Allwinner A733 Datasheet V0.93** revealed crucial base address and interrupt mismatches in early mainline patches:

### Key Hardware Realignment Findings:
1. **Main CCU Base Shift**:
   - Upstream patch assumed base `0x02001000`.
   - Real silicon base is **`0x02002000`** (`0x2000` bytes).
   - *Impact*: Clock disable operations were writing to unmapped registers or shifting offsets by 4 KB, causing bus lockups.
2. **PRCM R-PIO Base Shift**:
   - Upstream patch assumed base `0x07022000` (from older H616).
   - Real silicon base is **`0x07025000`** (`0x410` bytes).
   - *Impact*: GPIO requests for PMIC and power enables (e.g. `wifi_power_en`, `usb0-vbus`) were failing.
3. **Interrupt Vector Alignment (GIC-600)**:
   - Main PIO bank IRQs: **`GIC_SPI 69` to `87`** (previously offset by 6).
   - R-PIO IRQs: **`GIC_SPI 198` (PL) and `200` (PM)** (previously 122/124).
   - PMIC I2C (`r_i2c0`): **`GIC_SPI 203`** (previously 115).
   - Mailbox (`msgbox`): **`GIC_SPI 211`** (previously unmapped).
   - GMAC0/1: **`GIC_SPI 141` / `142`**.
   - USB 2.0 Host 0/1: **`GIC_SPI 157`–`160`**.
   - MMC0/2: **`GIC_SPI 161` / `163`**.
4. **Boot Stalling on CPU Isolation**:
   - `isolcpus=7 nohz_full=7 rcu_nocbs=7` in default `bootargs` caused RCU grace period stalls on non-isolated cores during PREEMPT_RT kernel boot.
   - *Resolution*: Removed `isolcpus` from default boot arguments.

---

### Milestone 9: USB Multi-PHY Shared Reset & GMAC0 Motorcomm Realignment (Aug 24, 2026)
- **USB Multi-PHY Probe Collision**:
  * `phy-sun4i-usb.c` used exclusive `devm_reset_control_get()`. When reset lines were removed from DTS to avoid `-EBUSY`, probe terminated at `i=0` on `-ENOENT`, dropping PHY 1 (Host 1 / Hub / AIC8800 Wi-Fi 6).
  * *Resolution*: Converted to `devm_reset_control_get_shared()` in patch `0005-phy-allwinner-sun4i-usb-use-shared-reset-control.patch` and restored `resets` in DTS, allowing all USB controllers and PHYs to probe cleanly without lock collisions.
- **GMAC0 Motorcomm PHY & Interrupt Vector Alignment**:
  * Realigned GMAC0 interrupt to `GIC_SPI 172` (`0xac`), PHY node to address `1` with Motorcomm `MAE0621A` binding (`ethernet-phy-id7b74.4412`, `rgmii-id`), and added DWMAC AXI burst limit / MTL TX/RX queue configuration nodes (`snps,axi-config`, `snps,mtl-rx-config`, `snps,mtl-tx-config`).
- **MMC0 & Host Controller Vector Verification**:
### Milestone 10: Radxa BSP Source Scan & CCU Register Alignment (Aug 24, 2026)
- **Radxa BSP Source Repository**:
  * Cloned and initialized `radxa-pkg-linux-a733` (`allwinner-bsp` and `device-a733`).
- **CCU Hardware Register Corrections**:
  * **USB2 (DWC3 3.1) Bus & Reset**: Discovered in `ccu-sun60iw2.c` that USB2 register is at **`0x135c`** (`BIT(0)` for `CLK_BUS_USB2` gate and `BIT(16)` for `RST_USB_2`), whereas earlier mainline patches mistakenly used non-existent `0x1314`.
  * **SPI Module Clocks**: Corrected `CLK_SPI0` to **`0x0F00`** and `CLK_SPI1` to **`0x0F08`** (earlier patches used `0x940`/`0x944` from H616).
  * **GMAC PHY Timing Registers**: Confirmed in `bsp/drivers/gmac/sunxi-gmac.c` that the dedicated GMAC0 PHY register is at `0x04508000` with identical bitfields (`tx_delay` bits 10..12, `rx_delay` bits 5..9, `EPIT` bit 2, `INT_GMII` bits 1:0) to mainline `dwmac-sun55i.c`.

### Milestone 11: Forensic Realignment Against Datasheet V0.93, Schematic V1.10 & BSP (Aug 24, 2026)
- **Ethernet (GMAC0) PHY Reset GPIO & RX Pinmux Conflict**:
  * **Datasheet V0.93 Table 4-37 & Schematic Sheet 7 / Sheet 21**:
    - `PH10` is ball `1AC2` -> Function 5 is `RGMII0-RXD3` (part of the 4-bit RX data bus).
    - `PH16` is ball `AU2` -> Schematic shows `GMAC1_RSTn_L` connects to `PH16` through resistor `R185` (0Ω).
  * **Defect**: Mainline DTS misconfigured `reset-gpios = <&pio 7 10 GPIO_ACTIVE_LOW>` (driving data line RXD3 low) and excluded `PH10` from `gmac0_pins`, while leaving the Motorcomm PHY reset line (`PH16`) floating.
  * **Resolution**: Added `"PH10"` back into `gmac0_pins` (all 16 pins `PH0`–`PH15`) and updated `reset-gpios = <&pio 7 16 GPIO_ACTIVE_LOW>;` (`PH16`).
- **Fatal Mailbox (MSGBOX0) CCU Null Pointer & CPU PLL Corruption**:
  * **Vendor Silicon (`ccu-sun60iw2.c`)**:
    - Mailbox 0 Gate: `0x0744, BIT(0)` (`CLK_MSGBOX0`)
    - Mailbox 0 Reset: `0x0744, BIT(16)` (`RST_BUS_MSGBOX0`)
  * **Defect**: `CLK_MSGBOX0` and `RST_BUS_MSGBOX0` were omitted from `ccu-sun60i-a733.c`. Probing `mailbox@3004000` caused `devm_reset_control_get()` to write uninitialized `{0x0000, 0}` to CCU offset `0x0000` (`CLK_PLL_CPUX`), corrupting core CPU clock frequency and hanging the SoC.
  * **Resolution**: Registered `bus_msgbox0_clk` at `0x0744 BIT(0)` and `[RST_BUS_MSGBOX0] = { 0x0744, BIT(16) }`.
- **USB 0 / USB 1 EHCI Host Gate & Reset Activation**:
  * **Vendor Silicon (`ccu-sun60iw2.c`)**:
    - USB0: Gate `0x1304 BIT(4)|BIT(0)` (EHCI/OHCI), Reset `0x1304 BIT(20)|BIT(16)`, PHY Reset `0x1300 BIT(30)`.
    - USB1: Gate `0x130c BIT(4)|BIT(0)` (EHCI/OHCI), Reset `0x130c BIT(20)|BIT(16)`, PHY Reset `0x1308 BIT(30)`.
  * **Defect**: Earlier mainline CCU driver only enabled OHCI (`BIT 0` / `BIT 16`), leaving EHCI host DMA engines unclocked and in reset.
  * **Resolution**: Updated `bus_usb0_clk`/`bus_usb1_clk` to `BIT(4)|BIT(0)` and `RST_BUS_USB0`/`RST_BUS_USB1` to `BIT(20)|BIT(16)`.
- **GMAC0 AXI DMA Reset Deassertion**:
  * Added `BIT(17)` (`RST_BUS_GMAC0_AXI`) to `RST_BUS_GMAC0` and `RST_BUS_GMAC1` at `0x141c`/`0x142c` (`BIT(17)|BIT(16)`).
- **PRCM R-CCU `CLK_R_AHB` Register Realignment**:
  * **Silicon Register (`ccu-sun60iw2-r.c`)**: `r-ahb` is at offset **`0x0000`** (`0x004` is non-existent).
  * **Resolution**: Realigned `r_ahb_clk` in `ccu-sun60i-a733-r.c` to offset `0x000`.
- **RemoteProc Robust Fallback & Write-Combining Mapping**:
  * Synced `sunxi_rproc.c` with safe fallback resource resolution for `main_ccu` and `sram`, preventing probe aborts with `-EINVAL`.

### Milestone 12: PRCM R-CCU Flexible Array Fix & A733 Clock Hierarchy Realignment (Aug 24, 2026)
- **Kernel Panic at `sunxi_ccu_probe+0xb0` (`hw->init->name`)**:
  * **Symptom**: `Internal error: Oops: 0000000096000004 [#1] SMP` during `sun60i_a733_r_ccu_probe`.
  * **Root Cause**: `struct clk_hw_onecell_data` contains a C99 flexible array member `struct clk_hw *hws[]`. Declaring `static struct clk_hw_onecell_data sun60i_a733_r_hw_clks` allocated memory only for the designated entries, leaving the array undersized for `.num = 13`. Iterating to `i = 8` read beyond the struct into `sun60i_a733_r_ccu_resets[]`, dereferencing an invalid pointer at `ldr x27, [x0]` (`hw->init->name`).
  * **Resolution**: Wrapped `sun60i_a733_r_hw_clks` in a fixed-size struct `struct { unsigned int num; struct clk_hw *hws[CLK_R_NUMBER]; }` matching standard `sunxi-ng` patterns.
- **A733 PRCM Clock Hierarchy & Reset Realignment**:
  * Real A733 silicon (`ccu-sun60iw2-r.c`) has `r-ahb` (`0x0000`), `r-apbs0` (`0x000c`), `r-apbs1` (`0x0010`).
  * Mapped `r-bus-twi0/1/2` and `r-bus-uart0/1` to parent `r-apbs1`, `r-bus-rtc` and `r-bus-cpucfg` to `r-apbs0`.
  * Added `include/dt-bindings/reset/sun60i-a733-r-ccu.h` with `RST_BUS_R_TWI0` (8), `RST_BUS_R_UART0` (5), `RST_BUS_RTC` (10) and bound `r_i2c0` in `sun60i-a733-cubie-a7a.dts`.

### Milestone 13: Full Subsystem Realignment Against Vendor `A7A_kernel` (Aug 31, 2026)
- **Wi-Fi 6 (AIC8800 USB) Chip Enable Regulator Activation**:
  * **Symptom**: AIC8800 Wi-Fi 6 device failed to enumerate on USB 2.0 (`0xA69C:0x8800`).
  * **Root Cause**: Board DTS supplied power enable `PM0` (USB_WIFI_PWR) but omitted `wifi_chip_en` on `PM1` (WIFI_REG_ON). The AIC8800 was held in constant hardware reset.
  * **Resolution**: Added `reg_wifi_chip_en` regulator with `gpio = <&r_pio 1 1 GPIO_ACTIVE_HIGH>;` (`PM1`), `regulator-always-on`, `regulator-boot-on` in `sun60i-a733-cubie-a7a.dts`.
- **RISC-V (XuanTie E907) Remoteproc Dual VMA / Memory Map Realignment**:
  * **Defect**:
    1. DTS node mapped only `0x07102000` and lacked the PRCM CFG register (`0x07010000`), ITCM (`0x07110000`), DTCM (`0x07120000`), SRAM C (`0x07130000`), and CCU clocks/resets.
    2. Driver `da_to_va()` failed to translate native core VMAs `0x00000000` (ITCM) and `0x00080000` (DTCM) when loading ELF segments into memory.
  * **Resolution**:
    1. Updated `sunxi_rproc.c` `da_to_va()` to support dual addressing (core VMA `0x00000000` / `0x00080000` and host physical `0x07110000` / `0x07120000` / `0x07130000`).
    2. Updated DTS `remoteproc@7010000` with `"cfg"`, `"itcm"`, `"dtcm"`, `"sram"`, clocks (`CLK_RISCV_24M`, `CLK_RISCV_CFG`, `CLK_RISCV`), resets (`RST_BUS_RISCV_CFG`), and mailbox (`&msgbox 0`).

---

## 8. Vendor BSP vs. Mainline Linux 7.1 Cross-Verification Matrix

The table below documents the full line-by-line cross-reference comparing the vendor BSP (`A7A_kernel/linux-a733`) against our mainline Linux 7.1 port:

| Subsystem / Node | Radxa Vendor BSP (`device-a733`/`bsp`) | Mainline Linux 7.1 Port (`sun60i-a733-cubie-a7a.dts`) | Verification Status | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Ethernet PHY Reset** | `reset-gpios = <&pio PH 16 GPIO_ACTIVE_LOW>` | `reset-gpios = <&pio 7 16 GPIO_ACTIVE_LOW>` | **100% MATCH** | `PH16` is `GMAC1_RSTn_L` driving Motorcomm PHY reset line. |
| **Ethernet Pinmux** | `PH0`..`PH15` (`gmac0_pins_default`) | `PH0`..`PH15` mux function 5 (`gmac0_pins`) | **100% MATCH** | `PH10` preserved as `RGMII0-RXD3` data bus line. |
| **USB 2.0 Host 0 VBUS**| `gpio = <&r_pio PL 2 GPIO_ACTIVE_HIGH>` | `gpio = <&r_pio 0 2 GPIO_ACTIVE_HIGH>` | **100% MATCH** | `PL2` USB VBUS power switch. |
| **USB 2.0 Host 1 VBUS**| `gpio = <&r_pio PM 5 GPIO_ACTIVE_HIGH>` | `gpio = <&r_pio 1 5 GPIO_ACTIVE_HIGH>` | **100% MATCH** | `PM5` USB host power switch. |
| **Wi-Fi Power Enable** | `gpio = <&r_pio PM 0 GPIO_ACTIVE_HIGH>` | `gpio = <&r_pio 1 0 GPIO_ACTIVE_HIGH>` | **100% MATCH** | `PM0` (USB_WIFI_PWR). |
| **Wi-Fi Chip Enable**  | `gpio = <&r_pio PM 1 GPIO_ACTIVE_HIGH>` | `gpio = <&r_pio 1 1 GPIO_ACTIVE_HIGH>` | **100% MATCH** | `PM1` (WIFI_REG_ON). |
| **Main CCU Base**      | `0x02002000` (8 KB) | `0x02002000` (8 KB) | **100% MATCH** | Sized array wrapper prevents buffer overrun. |
| **PRCM R-CCU Base**    | `0x07010000` (1 KB) | `0x07010000` (1 KB) | **100% MATCH** | Hierarchy: `r-ahb` (0x0) -> `r-apbs0` (0xc) / `r-apbs1` (0x10). |
| **Main PIO Base**      | `0x02000000` (11 banks: `PB`..`PK`) | `0x02000000` (`SUNXI_PINCTRL_ELEVEN_BANKS`) | **100% MATCH** | Bank counts `{0, 15, 17, 24, 16, 7, 15, 20, 17, 28, 24}`. |
| **R-PIO Base**         | `0x07025000` (2 banks: `PL`, `PM`) | `0x07025000` (`PL_BASE`, 13 & 10 pins) | **100% MATCH** | Bank 0 = PL (13 pins), Bank 1 = PM (10 pins). |
| **GIC-600 Interrupts** | Dist: `0x03400000`, Redist: `0x03460000` | Native `arm,gic-v3` | **100% MATCH** | 256 SPIs, 8 PPIs. |
| **UART0 Console**      | `0x02500000`, GIC SPI 2 | `0x02500000`, GIC SPI 2 | **100% MATCH** | `reg-shift = <2>`, `reg-io-width = <4>`. |
| **PMIC I2C (R_I2C0)**  | `0x07083000`, GIC SPI 203, `s_twi0` | `0x07083000`, GIC SPI 203, `CLK_R_TWI0` | **100% MATCH** | Clock 18, Reset 8. |
| **Mailbox 0**          | `0x03004000`, GIC SPI 211 | `0x03004000`, GIC SPI 211 | **100% MATCH** | CCU Gate `0x0744 BIT(0)`, Reset `BIT(16)`. |
| **RISC-V Coprocessor** | PRCM `0x07010000`, ITCM `0x07110000` | `remoteproc@7010000` | **100% MATCH** | E907 core with TCM/SRAM and mailbox IPC. |
| **USB AHB Interconnect**| `0x05C0` (`AHB_GATE_SW_CFG`) bit 9   | `sun60i_a733_ccu_probe()` un-gate | **100% MATCH** | Key `0x10000FF` enables USB/PHY MMIO bus decoder. |
| **USB PHY Resets**     | `0x1300 BIT(30)` / `0x1308 BIT(30)`   | `RST_USB_PHY0` / `RST_USB_PHY1` | **100% MATCH** | Independent from EHCI/OHCI bus resets (`0x1304`/`0x130c`). |

### Milestone 14: USB Subsystem AHB Master Interconnect & PHY Reset Realignment (Aug 31, 2026)
- **USB Controller & PHY MMIO Bus Stall Fix**:
  * **Symptom**: Kernel hung at `[ 1.743520] phy phy-4100400.phy.0: Changing dr_mode to 1` when `ehci0` / `phy-sun4i-usb` attempted to access PHY/PMU registers (`0x04100400` / `0x04101800`).
  * **Root Cause**:
    1. On Allwinner A733 (`sun60iw2`), the CCU contains security interconnect gate registers at `0x05C0` (`AHB_GATE_SW_CFG`), `0x05E0` (`MBUS_MAT_CLK_GATING_REG`), and `0x05E4` (`MBUS_GATE_ENABLE_REG`). Bit 9 of `0x05C0` (`usb-sys-ahb-gate` with key `0x10000FF`) was gated, blocking all CPU transactions to the USB subsystem MMIO space and causing an AXI bus freeze.
    2. USB PHY transceivers have dedicated hardware resets at `0x1300 BIT(30)` (`RST_USB_0_PHY_RSTN`) and `0x1308 BIT(30)` (`RST_USB_1_PHY_RSTN`), separate from the controller bus resets at `0x1304` / `0x130c`.
  * **Resolution**:
    1. In `ccu-sun60i-a733.c` (`sun60i_a733_ccu_probe`), added automatic un-gating for `0x05C0` (`0x110003FF`), `0x05E0`, and `0x05E4`.
    2. Added `RST_USB_PHY0` (42) and `RST_USB_PHY1` (43) to `sun60i-a733-ccu.h` and bound them to `usbphy: phy@4100400`.
    3. Added `keep_bootcon` to `bootargs` in `boot.cmd` to keep serial diagnostics visible across all driver handovers.

### Milestone 15: Ethernet Link and Remoteproc Trace Investigation (Sep 1, 2026)
- **Patch-series validation**: Ran `make -C bld.a7a linux-dirclean && make -C bld.a7a linux` successfully on Sep 1. Buildroot recreated the Linux 7.1 package tree, applied the complete eight-patch external series from `project-cubie-a5e/patches/linux/`, built the kernel, and installed `bld.a7a/images/sun60i-a733-cubie-a7a.dtb`. The required reproducible procedure is documented in `docs/buildroot/A7A_KERNEL_PATCH_VALIDATION.md`.
- **Scope**: USB remains disabled in the A7A board DTS while Ethernet and remoteproc are isolated. No USB node is to be enabled as part of this investigation.
- **Reference boundary**: `A7A_kernel/linux-a733` is the legacy vendor kernel, not the Linux 7.1 mainline port. Its DTS and drivers are evidence for board routing, register semantics, and known-good hardware sequencing only; vendor-only compatible strings and properties must not be copied into the mainline DTS unless the corresponding mainline driver supports them.
- **Schematic provenance**: The official [Cubie A7A V1.10 schematic](https://dl.radxa.com/cubie/a7a/docs/hw/radxa_cubie_a7a_v1.10_schematic.pdf) is archived locally as searchable text at `docs/extracted_vendor/radxa_cubie_a7a_v1.10_schematic.txt`. Its sheet 18 has been decoded in `CUBIE_A7A_ETHERNET_SCHEMATIC_REFERENCE.md`: it confirms `PH0`–`PH15` as RGMII0, `PH16` as active-low PHY reset (`GMAC1_RSTn_L` through `R185`), MAE0621A-Q3C PHY U10, and LPJG0926HENL integrated-magnetics RJ45 J2.
- **Ethernet observation**:
  * The MAC driver probes and reaches MDIO successfully: `dwmac-sun55i 4500000.ethernet eth0` identifies PHY address 1 as `MAE0621A/B-Q3C(I) Gigabit Ethernet`.
  * Assigning `192.168.1.33` directly with `ifconfig eth0 192.168.1.33 up` does not report carrier or a `Link is Up` transition. The interface counters remain at zero and a static ping to `192.168.1.2` receives no packets. This conclusively excludes DHCP as the root cause: failure is at PHY physical-link/autonegotiation, before IP or MAC DMA traffic.
  * No Ethernet link LEDs illuminate with the connected cable. LED state is generated by the external PHY's copper-link logic, independent of IP configuration and MAC DMA. Together with working MDIO, this narrows the fault to reset/power sequencing, copper/magnetics/cable path, or required PHY-specific initialization.
  * The current mainline DTS is not yet a minimal standard binding: it uses the A733-specific GMAC compatible and glue driver and repeats `CLK_BUS_GMAC0` for `stmmaceth` and `mbus`. In contrast, the vendor base node describes distinct GMAC bus, MBUS, 25 MHz PHY-reference, and PTP clocks and distinct AXI/MAC resets. Vendor board configuration uses plain `rgmii`, the same PHY ID/address, `PH16` active-low reset, `tx-delay = <12>`, and `rx-delay = <10>`.
  * The current A733 glue driver does program the dedicated GMAC210 clock-gate register and translates `tx-internal-delay-ps` / `rx-internal-delay-ps`; it does not consume `aw,soc-phy-clk-en`. Adding that vendor-only property to the mainline DTS would therefore be ineffective without a corresponding, hardware-verified driver implementation.
  * The CCU port writes GMAC register `0x1410` to enable a SoC 25 MHz output, but sheet 18 shows the board PHY uses dedicated crystal Y5 (25 MHz) directly on U10 `XIN`/`XOUT`. The only `EPHY-CLK-25M` connection is R116, marked unpopulated (`NC/0R`). The raw CCU write is therefore not a viable fix for absent PHY link or LEDs on this board.
  * Next hardware evidence required before changing delay mode or DMA configuration: `ethtool eth0`, `ethtool --phy-statistics eth0` (if supported), PHY registers 0/1 via `phytool` or MDIO debug access, and kernel messages around cable insertion/removal. This distinguishes reset/power/autonegotiation from RGMII pinmux/timing. DHCP and DMA are out of scope until carrier is present.
- **Remoteproc trace observation**:
  * Reading `trace0` causes an ARM64 synchronous external abort in `rproc_trace_read()` while `__pi_strnlen()` dereferences the trace buffer. Do not read this debugfs file on target until the mapping is verified; the abort taints the running kernel.
  * `exampleRiscv` declares `trace0` at core device address `0x0000e000` with length `0x1000`. Its linker script places this in the top 8 KiB of the firmware's `0x00000000` RISC-V SRAM address space.
  * The current board DTS exposes only `cfg` and an `itcm` region at host address `0x07110000`. `sunxi_rproc_da_to_va()` therefore translates trace DA `0x0000e000` through that `itcm` mapping. The external abort proves that this resulting host access is not safe on the A733; it does **not** prove that a DTCM mapping is missing, because this firmware trace is not in DTCM.
  * Required next step: verify the E907 Linux-visible physical alias for the firmware's `0x00000000` SRAM against the vendor memory map/hardware, then align the DTS resource name/base/length and firmware linker script. Keep the generic debugfs NULL handling intact; it cannot protect against a non-NULL mapping whose physical access aborts.

### Design Checkpoint: Shared A5E/A7A Remoteproc Driver (Sep 1, 2026)
- **Goal**: One Linux-submission-quality `sunxi_rproc` driver serves both A5E and A7A. Differences belong in compatible-specific Devicetree data, not board-name checks or raw hardware writes in the driver.
- **Memory policy**: Clearing E907-owned memory before each ELF load is valid only after its Linux-accessible, non-secure physical aperture is proven. Never blanket-clear a Linux/shared ownership region. Firmware clears its own BSS; Linux must not clear A733 ITCM or DTCM while their host mapping remains unverified.
- **Trace policy**: The attempted A733 DTCM mapping was rejected by target hardware: Linux writes to `0x07120000` SError. Use a common trace DA only after each SoC's CPU-accessible physical mapping is explicitly described and tested. A reserved-memory/carveout region is the safe fallback when direct TCM access is unavailable.
- **History policy**: Preserve and review Git history for remoteproc patch `0002` and DTS patches `0001` (A7A) / `0005` (A5E) before replacing any prior layout. The project-root `TODO.md` is the current restart checklist.

### Remoteproc TCM Host-Access Results and Image Audit (Sep 1, 2026)
- **ITCM read failure**: Firmware trace at ITCM device address `0x0000e000` caused an ARM64 external abort in `rproc_trace_read()` / `__pi_strnlen()`.
- **DTCM loader failure**: Declaring DTCM as host physical `0x07120000` and linking loadable firmware data there caused an asynchronous SError in `rproc_elf_load_segments()` / `__pi_memcpy_generic`, before the E907 started.
- **Root cause**: `sunxi_rproc` mapped TCM as `__iomem` but returned `is_iomem = false`; remoteproc therefore issued generic `memcpy()`/`memset()` to an I/O mapping. This was a driver mapping-semantics defect, not proof that the DTCM physical window is inaccessible.
- **Correction**: ITCM/DTCM use `devm_ioremap_wc()` and `sunxi_rproc_da_to_va()` reports `is_iomem = true` for either window. Remoteproc now invokes `memcpy_toio()`/`memset_io()`, matching the in-tree ZynqMP R5 driver’s TCM mapping model. A7A DTS declares distinct 64 KiB ITCM (`0x07110000`) and DTCM (`0x07120000`) windows again.
- **Trace boundary**: Generic remoteproc debugfs trace uses `strnlen()` and cannot consume an I/O-mapped TCM address. The firmware resource table has no `RSC_TRACE` entry for this test image. Live trace remains disabled until a normal Linux-readable reserved-memory carveout is defined.
- **Package correctness finding**: A plain `riscv-firmware-rebuild` retained a stale installed ELF. `make -C bld.a7a riscv-firmware-dirclean riscv-firmware` forced source resynchronization. This clean package step is required after every local firmware-source change.
- **Validated image structure**: The target includes `/usr/sbin/ethtool` and `/usr/bin/phytool`. A full `make -C bld.a7a` completed; `verify_sdcard_image.py` passed MBR, boot0 checksum, FAT boot partition, and ext4 rootfs checks for the 592 MiB `sdcard.img`.
- **Next hardware test**: Boot this image and start remoteproc. It must load the ITCM/DTCM ELF segments without SError. There should be no `trace0` debugfs file in this test image; that is intentional.

### Remoteproc Debugfs TCM Safety Fix (Sep 1, 2026)
- **Problem**: A stale firmware with an `RSC_TRACE` entry still created `trace0`; generic remoteproc debugfs called `rproc_da_to_va()` without requesting I/O-memory semantics, then passed the returned TCM pointer to `strnlen()`, causing an ARM64 external abort.
- **Fix**: `sunxi_rproc_da_to_va()` now returns TCM addresses only to callers that supply the `is_iomem` result pointer. The ELF loader supplies it and therefore continues through `memcpy_toio()`/`memset_io()`; debugfs supplies `NULL`, receives no pointer, and takes its existing `Trace trace0 not available` path.
- **Build validation**: Synchronized the permanent remoteproc patch, clean-rebuilt Linux through `bld.a7a`, repacked the image, and passed `verify_sdcard_image.py`.
- **Remaining target proof**: Read `trace0` once from the new image. Expected result is `Trace trace0 not available` with no kernel abort.

### Legacy A733 Remoteproc Mapping Audit (Sep 1, 2026)
- **Vendor implementation evidence**: `A7A_kernel/linux-a733/bsp/drivers/remoteproc/sunxi_rproc.c` translates firmware device addresses through DTS `memory-mappings` triples (DA, length, PA), maps each declared physical carveout with `ioremap_wc()`, and loads only through those mappings. It does not use a universal hard-coded TCM physical address.
- **A733 limitation**: The legacy A733 DTS contains no E907 remoteproc node or `memory-mappings` table. It provides no evidence that `0x07120000` is a Linux-accessible DTCM alias.
- **Implementation consequence**: The submission-quality driver must use explicitly described, non-secure memory resources. Until an A733 CPU-visible TCM aperture is verified, Linux must neither clear TCM nor load ELF segments into it.

### Ethernet Carrier Established; Check for Link Flap (Sep 1, 2026)
- **New target evidence**: `dwmac-sun55i` successfully attached the MAE0621A/B-Q3C(I) PHY and reported `eth0: Link is Up - 1Gbps/Full - flow control off`.
- **Conclusion**: PHY power/reset, copper link, autonegotiation, RGMII link mode, and basic MAC/PHY integration are working. The prior no-link-LED/no-carrier condition is not present in this boot.
- **Repeated lines**: Identical pairs with the exact same kernel timestamp are duplicate console output, not separate link events. If new `Link is Up` or `Link is Down` messages appear with different timestamps, capture them: that is link flapping and must be diagnosed separately.
- **Next Ethernet test**: Run `ethtool eth0`, collect interface counters, then transfer/ping traffic while watching `dmesg -w`. Do not change PHY or DMA configuration unless the link transitions or traffic counters demonstrate a fault.

### Ethernet TX DMA Watchdog Failure (Sep 1, 2026)
- **New target evidence**: `ethtool` reports 1000 Mb/s full-duplex carrier and PHY address 1; PHY basic control is `0x0404` (autonegotiation enabled, not power-down or isolate). However, `ifconfig` reports zero RX packets and only 1314 TX bytes, followed by repeating `NETDEV WATCHDOG: transmit queue 0 timed out` after 5–6 seconds.
- **Conclusion**: This is not a PHY or link failure. TX descriptors are not completing in the GMAC/DMA path. Every watchdog reset re-probes the MAC/PHY and produces the recurring `Link is Up` messages.
- **Binding defect to correct**: Current DTS supplies `CLK_BUS_GMAC0` twice as `stmmaceth` and `mbus` and supplies one combined reset. Vendor hardware describes separate GMAC core, MBUS, PHY-reference, and PTP clocks plus separate GMAC AXI and MAC resets. The A733 CCU port must expose and bind these standard `stmmac` resources before PHY timing or DMA tuning is changed.
- **Console cleanup**: MAE0621 probe, version, self-check, and remove banners were changed from unconditional `printk()` calls to `phydev_dbg()`. This removes reset-cycle noise without hiding standard `Link is Up`/`Link is Down`, `NETDEV WATCHDOG`, or `Reset adapter` messages; those remain required diagnostics for the active TX DMA failure.

### Ethernet/Remoteproc Target Retest (Sep 1, 2026)
- **Remoteproc success**: The E907 remoteproc starts successfully with separate ITCM/DTCM ELF segments after `sunxi_rproc` switched TCM mappings to `ioremap_wc()` and reports them as I/O memory. The earlier SError from generic `memcpy()` during ELF load is resolved.
- **Ethernet persistence**: The GMAC core-clock parent and `rgmii-id` corrections are active: the MAC now reports `IEEE 1588-2008 Advanced Timestamp supported` and `registered PTP clock`, then maintains 1 Gbps full-duplex link. Nevertheless, TX queue 0 still times out every 5–6 seconds and resets the adapter. The repeated link messages are reset/re-probe output, not PHY polling.
- **Next Ethernet focus**: Audit GMAC DMA reset, AXI/MBUS path, and descriptor configuration against the vendor GMAC driver. Do not hide watchdog output or alter PHY configuration.

### Remoteproc Test-Artifact Mismatch (Sep 1, 2026)
- **New target evidence**: The target still contains `/sys/kernel/debug/remoteproc/remoteproc0/trace0` and aborts in `rproc_trace_read()`. The current TCM I/O-mapping test firmware deliberately has a resource table with `num = 0`, which must not create `trace0`.
- **Conclusion**: This target boot used an older firmware artifact containing `RSC_TRACE`; it cannot validate the current remoteproc driver test. Verify the flashed rootfs `riscv-firmware.elf` hash/section table against `bld.a7a/target/lib/firmware/riscv-firmware.elf` before retesting. The current test criterion is E907 start without SError; live trace remains disabled.

### U-Boot MDIO Address Scan (Sep 1, 2026)
- **Test**: In the running U-Boot, `mdio read ethernet@4500000 0-1f 2-3` read PHY-ID registers 2 and 3 at all 32 Clause 22 addresses.
- **Result**: Every read returned `0x0000`. This is not a PHY-address mismatch; the controller is not receiving valid PHY turnaround/data, or this U-Boot implementation reports zero when its MDIO transaction fails.
- **Scope**: This result characterizes the current U-Boot MDIO path only. It does not invalidate later Linux evidence where PHY address 1 was identified as MAE0621A and reached 1 Gb/s link.
- **Next discriminator**: Inspect U-Boot's live control DT for GMAC enablement, PH0–PH15 pinmux, PH16 active-low reset, clocks, and supplies. The legacy vendor source has a disabled base GMAC node and placeholder pin groups, so source defaults alone cannot describe the running mainline U-Boot configuration.
- **Live control DT**: `fdtcontroladdr` points at `0xfbef18d0`. Its GMAC0 node is enabled, uses PHY address 1, declares PH16 active-low reset with 10 ms assert / 150 ms deassert timing, and references pinctrl phandle `0x0f`. That pin group contains PH0–PH15 with function `gmac0`, mux value 5, and 30 mA drive strength. A missing `/phy-3v3` pathname does not prove a missing supply: the GMAC node references supply phandle `0x14`, and the project DTS models this board rail as fixed, always-on, with no enable GPIO.
- **Remaining U-Boot checks**: Read PH16's current GPIO state and CCU GMAC gate/reset register `0x0200341c`. If reset is released and the gate/reset bits are enabled, measure U10 `PHY_RESETn`, `VCC3V3_PHY`, `VCCIO_PHY`, and `VDD10_PHY`; further speculative DTS changes are not justified.
- **GPIO/register result**: `gpio status PH16` reports output high and owned by `ethernet-phy@1.reset-gpios`, so the live control DT has released the active-low PHY reset. CCU GMAC register `0x0200341c` reads `0x00000001`: the bus gate is enabled but MAC/AXI reset-release bits 16/17 are clear. GMAC syscfg `0x04508000` reads `0x00000000`.
- **Interpretation boundary**: These reads were taken after `mdio` commands, not after a U-Boot network operation. Driver-model MDIO registration/probe may occur before the Ethernet driver's `.start()` callback performs full reset and syscfg programming. Trigger the normal U-Boot Ethernet start path with a ping, then repeat the two register reads and PHY-ID read before concluding that the U-Boot CCU/reset implementation is defective. Do not manually write CCU registers yet.
- **Normal-start result**: After setting U-Boot IP `192.168.1.33`, two pings to live peer `192.168.1.2` succeeded; a ping to intentionally absent host `192.168.1.3` correctly failed. This proves the complete U-Boot Ethernet data path, including PHY power/reset, MDIO initialization, RGMII pinmux/timing, MAC DMA, physical board routing, magnetics, cable, and peer connectivity.
- **Corrected conclusion**: The all-zero MDIO scan occurred before the Ethernet `.start()` path and represented an uninitialized controller, not a dead PHY or board fault. Linux's repeating TX watchdog is now isolated to Linux A733 GMAC clock/reset/syscfg/DMA setup. Capture post-start U-Boot register and PHY-ID values as the known-good comparison point before any Linux DTS/CCU edit.
- **U-Boot teardown behavior**: After a successful ping command returns, `0x0200341c` again reads `0x00000001`, `0x04508000` reads zero, and standalone MDIO ID reads return zero. U-Boot stops/tears down the Ethernet device after the network command, so these are not active datapath register values. The successful packet exchange remains the valid hardware proof.
- **Linux split-IRQ discrepancy**: Vendor `dwmac210_variant` sets `SUNXI_DWMAC_MULTI_MSI`, requests `tx0_irq`/`rx0_irq`, and enables stmmac multi-MSI. Linux 7.1 `dwmac-sun55i` sets only `STMMAC_FLAG_SPH_DISABLE`; generic stmmac therefore requests only `macirq` and never installs TX/RX queue handlers. U-Boot is polling-based, so its success does not test these interrupt lines. This source difference directly matches Linux TX descriptors timing out while the polled U-Boot datapath works.
- **Binding consequence**: Mainline stmmac recognizes queue names `tx-queue-0` and `rx-queue-0`, not vendor names `tx0_irq` and `rx0_irq`. Renaming DTS entries alone is insufficient because `dwmac-sun55i` must also enable `STMMAC_FLAG_MULTI_MSI_EN` for the queue handlers to be requested. Capture Linux `/proc/interrupts` around traffic before applying this minimal, hardware-specific glue change.
- **Target IRQ proof**: During repeated TX watchdog failures, `/proc/interrupts` showed only `eth0` Linux IRQ 412 (GIC hardware IRQ 204) and its count remained zero on all CPUs. Every stmmac IRQ statistic, including `tx_normal_irq_n`, `rx_normal_irq_n`, `q0_tx_irq_n`, and `q0_rx_irq_n`, remained zero. This confirms that Linux was not servicing GMAC TX/RX completion interrupts.
- **Why the split ISRs were initially missed**: The old A733 DTS did list four hardware interrupts, but used vendor names `tx0_irq`/`rx0_irq`. Generic Linux 7.1 stmmac only parses `tx-queue-0`/`rx-queue-0`, and `dwmac-sun55i` initially did not set `STMMAC_FLAG_MULTI_MSI_EN`. Consequently only the common `macirq` was requested. The requirement is implemented in the separate legacy BSP glue (`bsp/drivers/stmmac/dwmac-sunxi.c`), where `dwmac210_variant` sets `SUNXI_DWMAC_MULTI_MSI`, explicitly parses the vendor queue names, and selects the multi-MSI request path. Earlier comparisons focused on DTS, clocks, resets, syscfg, and the generic stmmac tree rather than this BSP-only variant flag.
- **RJ45 LED behavior**: Schematic V1.10 connects PHY `LED1` to the green RJ45 LED and PHY `LED2` to yellow; PHY `LED0` is not connected to the jack. The legacy kernel registers `phy_mae0621a_led_fixup()` for PHY ID `0x7b744412`: green indicates link at 10/100/1000 Mbps and activity, while yellow indicates steady link at 10/100/1000 Mbps with its EEE indication disabled. The LEDs therefore do not distinguish negotiated speed by color in the vendor configuration.
- **Stale firmware root cause**: An obsolete binary was checked into the A7A rootfs overlay at `board/radxa/cubie_a7a/rootfs-overlay/lib/firmware/riscv-firmware.elf`. The firmware package installed the newly built ELF first, but Buildroot `target-finalize` subsequently copied the overlay and silently restored the old ITCM trace resource (`DA 0x0000e000`, length `0x1000`). The A7A overlay copy was removed so the package is now the single authoritative installer.
- **Corrected image proof**: After a clean firmware package build and explicit `rootfs-ext2 target-post-image`, the build-tree, target-tree, and packed-rootfs firmware ELFs all have SHA-256 `927e9b7647a80fa92e9e52a813dcabb30ee8c7aa2a90d1142d8c3a9f477a587e`. Their resource table advertises reserved DDR trace DA `0x4e000000`, length `0x8000`. The rebuilt `sdcard.img` still requires target validation.

### Pinctrl Bank Stride Fix, Status LEDs, and VBUS GPIOs (Sep 2, 2026)
- **Root Cause Identified**: The Allwinner A733 PIO controller uses hardware layout `HW_TYPE_10` with a `0x80` byte memory stride per GPIO bank, `pull_regs_offset = 0x30`, and `dlevel_field_width = 4`. The initial mainline port used the legacy `0x24` stride, causing all bank offsets beyond PA (PB..PN) to miscalculate and silent MMIO read/write failures.
- **Mainline Implementation**: Added `SUNXI_PINCTRL_A733_LAYOUT` (`BIT(11)`) to `drivers/pinctrl/sunxi/pinctrl-sunxi.h` and `pinctrl-sunxi.c`, dynamically allocating `0x80` bank sizes and correct pull offsets.
- **Hardware Verification**: Target boot confirmed 100% functionality. Blue heartbeat LED (`PJ27`) and Green power LED (`PJ26`) successfully registered in sysfs and toggled via `/sys/class/leds/radxa:green:power/brightness`.

### CCU 24MHz Clock Fallback & RSB PMIC Configuration (Sep 2, 2026)
- **Clock Bug Resolved**: In both Main CCU (`ccu-sun60i-a733.c`) and R-CCU (`ccu-sun60i-a733-r.c`), `osc24M` and parent arrays used only `{ .fw_name = "hosc" }` without string fallback `.name = "osc24M"`. Linux CCU failed to resolve the parent rate, leaving `r-ahb`, `r-apbs1`, `r-twi0`, and USB reference clocks reporting `0 Hz`.
- **Mainline Fix**: Added explicit `.name = "osc24M"` fallback to all 24MHz parents across Main CCU and R-CCU.
- **RSB Verification**: In DTS, switched `r_i2c0` at `0x07083000` to `allwinner,sun8i-a23-rsb` (`r_rsb`) with child `axp8191: pmic@3a3` to match U-Boot's RSB mode initialization.

### XuanTie E902 Core Architecture & Lack of TCMs Breakthrough (Sep 3, 2026)
- **Definitive Core Identification**:
  - The Allwinner A733 coprocessor is confirmed as the **XuanTie E902** (RV32EMC, up to 200 MHz), as documented by Linux-Sunxi.
  - The E902 silicon **DOES NOT HAVE Tightly Coupled Memories (NO ITCM, NO DTCM)**.
  - This definitively explains the earlier Sep 1 aborts where accessing `0x07110000` (ITCM) and `0x07120000` (DTCM) caused asynchronous SErrors and external aborts. Those addresses do not exist on the A733!
- **Memory Architecture**:
  - On the A733, the E902 executes 100% out of **Shared SRAM A2 (`0x00040000`–`0x00073FFF`, 208 KB)**.
  - The firmware ELF links to `ORIGIN = 0x00044000` in SRAM A2.
  - The A733 Device Tree does not declare TCM memory windows.
- **Compiler / ABI Contract**:
  - Because the E902 is RV32E, it possesses only **16 general-purpose integer registers** (`x0`–`x15`) and **NO FPU**.
  - Firmware MUST be compiled targeting:
    `-march=rv32emc_zicsr -mabi=ilp32e -mcmodel=medany`
  - Compiling with standard `ilp32` or `ilp32d` emits instructions touching registers `x16`–`x31` or hardware float opcodes, which trigger hardware illegal instruction exceptions on the E902.
- **No On-Chip OpenOCD / DMEM Support**:
  - The RISC-V hardware Debug Module is not exposed over the non-secure ARM bus interconnect.
  - Interactive GDB/OpenOCD debugging over `/dev/mem` is unsupported. Developers operate "blind" and must rely on RemoteProc trace buffers (`trace0`), S_UART0 serial logging, and SRAM memory probing.

### XuanTie E906 vs E902 Register Map & Lifecycle Sequencing Rules (Sep 3, 2026)
- **E906 CFG Register Map (`0x07130000`) on T527**:
  * `0x0000`: `E906_VER_REG`
  * `0x0010`: `E906_RF1P_CFG_REG`
  * `0x0040`: `E906_TS_TMODE_SEL_REG`
  * `0x0204`: `E906_STA_ADD_REG` (Start Vector / Boot Address)
  * `0x0220`: `E906_WAKEUP_EN_REG`
  * `0x0224`–`0x0234`: `E906_WAKEUP_MASK0..4_REG`
  * `0x0248`: `E906_WORK_MODE_REG`
- **Bus Error Prevention & Symmetrical Sequencing Rules**:
  * **Start Rule**: `cfg_clk` (`CLK_BUS_RV_CFG`) must be enabled and `cfg_rst` (`RST_BUS_RV_CFG`) deasserted BEFORE writing `E906_STA_ADD_REG` (`0x0204`). Touching unclocked registers triggers an immediate AXI/AHB SLVERR/DECERR (Synchronous External Abort).
  * **Stop Rule**: `mod_rst` (`RST_BUS_RV`) must be asserted BEFORE gating `mod_clk` (`CLK_BUS_RV`). Gating the clock on an active core mid-burst freezes the bus transaction and deadlocks the SoC bus.
  * **A733 Contrast**: The `0x07130000` block does not exist on A733 silicon. The A733 E902 is clocked and reset purely via `r_ccu` (`0x07010000`), executing directly from SRAM A2 (`0x00040000`) with no `0x0204` register needed.







