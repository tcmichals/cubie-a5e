# 🛰️ Radxa Cubie A7A (Allwinner A733 / `sun60iw2`) Mainline Implementation & Tracking Plan

> [!NOTE]
> **Authoritative Technical Documents**:
> - Detailed Platform Architecture: [`CUBIE_A7A_PLATFORM_GUIDE.md`](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/platforms/CUBIE_A7A_PLATFORM_GUIDE.md)
> - Live Hardware Bring-Up Debug Log: [`CUBIE_A7A_DEBUG_LOG.md`](file:///home/tcmichals/projects/cubie/cubie-a5e/docs/platforms/CUBIE_A7A_DEBUG_LOG.md)

This plan tracks the end-to-end integration and verification of **100% Mainline Linux 7.1 PREEMPT_RT and Upstream U-Boot 2026.01** on the **Radxa Cubie A7A (Allwinner A733 / `sun60iw2`)**.

---

## 1. Upstream Firmware Provenance & Repository Matrix

| Layer | Source Repository | Working Branch / Version | Build Configuration | Critical Role |
| :--- | :--- | :--- | :--- | :--- |
| **DRAM Training (`boot0`)** | Official Radxa Firmware | Commit `{4721ad08}` | `boot0_sdcard.bin` (240 KB) | Sector 256 (128 KB); trains LPDDR5 RAM PHY across 4 clock tiers (400–2400 MHz) in SRAM (`0x47000`). |
| **TF-A (BL31)** | [radxa/allwinner-device](https://github.com/radxa/allwinner-device) | `device-a733-v1.4.6` | `bl31.bin` (78.8 KB) | Secure EL3; sets `ICC_SRE_EL3=0x7` & `ICC_SRE_EL2=0x7` to enable GICv3 system registers. |
| **OP-TEE OS** | [radxa/allwinner-device](https://github.com/radxa/allwinner-device) | `device-a733-v1.4.6` | `optee_sun60iw2p1.bin` (406.9 KB) | Secure World OS payload at `0x48600000`. |
| **ARISC (`scp`)** | [radxa/allwinner-device](https://github.com/radxa/allwinner-device) | `device-a733-v1.4.6` | `scp.bin` (117.5 KB) | Power co-processor firmware for AXP PMIC rails and clock management. |
| **U-Boot Mainline** | [dlan17/u-boot](https://github.com/dlan17/u-boot) | `allwinner/A733/boot-2026.01` | `CONFIG_TEXT_BASE=0x4a001000` | Mainline U-Boot Proper (Non-Secure EL2) linked with 4KB page alignment (`b +0x1000`). |
| **Device Tree (`dtb`)** | Project Cubie tree | `sun60i-a733-cubie-a7a.dtb` | `sun60i-a733-cubie-a7a.dts` | Hardware descriptions passed to BL31, OP-TEE, U-Boot, and Linux. |
| **Linux 7.1 Kernel** | Mainline / sunxi | `7.1` `PREEMPT_RT` | `sun60i-a733-cubie-a7a.dtb` | Deterministic flight controller kernel with native GICv3 and `sunxi_rproc`. |

---

## 2. Sector Layout & Memory Architecture

### A. Raw SD Card Disk Geometry (512-Byte Sectors)
- **Sector 0–255 (0 – 128 KB)**: MBR / Partition Table area (`0xAA55`).
- **Sector 256 (128 KB offset)**: `boot0_sdcard.bin` (**240 KB / 245,760 bytes**; Checksum `0xd6c0cbdf`; physical LPDDR5 PHY training in SRAM).
- **Sector 24576 (12.0 MB offset / 12288K)**: `boot_package.fex` (TOC1 container: `u-boot` @ `0x4A000000`, `monitor` @ `0x48000000`, `optee` @ `0x48600000`, `scp`, `dtb`).
- **Sector 65536 (32.0 MB offset)**: Partition 1 `boot.vfat` (Kernel `Image`, DTB, `boot.scr`, `uboot.env`).
- **Sector 196608 (96.0 MB offset)**: Partition 2 `rootfs.ext4` (Root filesystem).

### B. DRAM Address Space
- `0x40000000`: DRAM Base (6 GiB range: `0x40000000` – `0x1C0000000`).
- `0x40080000`: Kernel Load Address (`kernel_addr_r`).
- `0x44000000`: Kernel Decompression / Initrd Scratch Space (`kernel_comp_addr_r`).
- `0x48000000`: TF-A BL31 Load Address (`monitor_base`).
- `0x48600000`: OP-TEE OS Load Address (`optee_base`).
- `0x4A000000`: U-Boot Container Base (4KB Header Info with `b +0x1000`).
- `0x4A001000`: Mainline U-Boot Execution Entry Point (`CONFIG_TEXT_BASE`).
- `0x4FA00000`: Device Tree Load Address (`fdt_addr_r`).
- `0x4FC00000`: U-Boot Script Load Address (`scriptaddr`).

---

## 3. Automated Verification Tooling

To guarantee image integrity before touching hardware, run:
```bash
python3 project-cubie-a5e/board/radxa/cubie_a7a/tools/verify_sdcard_image.py bld.a7a/images/sdcard.img
```

---

## 4. Implementation Checklist & Status

### Phase 1: Linux Kernel 7.1 Drivers & Device Tree
- [x] Ingest CCU and PRCM drivers (`0003-clk-sunxi-ng-add-allwinner-a733-ccu-and-prcm.patch`).
- [x] Ingest Pinctrl driver with 0x80 PIO offset (`0004-pinctrl-sunxi-add-allwinner-a733-pinctrl.patch`).
- [x] Modernize board DTS (`0001-arm64-dts-allwinner-add-sun60i-a733-cubie-a7a.patch`) with native GICv3 and 6 GiB memory node.
- [x] Enable `CONFIG_SUN60I_A733_CCU=y`, `CONFIG_PINCTRL_SUN60I_A733=y`, `CONFIG_MMC_SUNXI=y`, `CONFIG_SERIAL_8250_SUNXI=y` in `linux.config`.

### Phase 2: Buildroot Package & Firmware Integration
- [x] Integrate full 240 KB factory `boot0` with verified `eGON` checksum (`0xd6c0cbdf`).
- [x] Integrate `dragonsecboot` host tool and 4KB AArch64 `header-info.bin` (`0x14000400` = `b +0x1000`).
- [x] Configure Buildroot for Mainline U-Boot (`allwinner/A733/boot-2026.01`) with `CONFIG_TEXT_BASE=0x4a001000`.
- [x] Implement automated 5-item TOC1 packaging in `post-image.sh` (`u-boot`, `monitor`, `optee`, `scp`, `dtb`).
- [x] Integrate automated verification audit gate directly into `post-image.sh`.

### Phase 3: Hardware Verification & Forensic Realignment
- [x] Verify `boot0` dynamic LPDDR5 4-state training banner on hardware UART0 (400, 800, 1200, 2400 MHz).
- [x] Verify physical DRAM sizing: `Actual DRAM SIZE = 6144 M` (6 GB).
- [x] Verify TOC1 package loading from Sector 24576 (12.0 MB).
- [x] Verify TF-A BL31 execution at Secure EL3.
- [x] Verify OP-TEE OS execution in Secure DRAM.
- [x] Verify Mainline U-Boot 2026.01 prompt at `0x4A001000`.
- [x] Verify Linux 7.1 `PREEMPT_RT` earlycon output on UART0 (`0x02500000`).
- [x] Verify GICv3 interrupt controller probing across all 8 CPU cores (2x A78 + 6x A55).
- [x] Realign GMAC0 PHY reset to `PH16` and restore `PH10` (`RGMII0-RXD3`) in pinmux.
- [x] Realign Mailbox CCU gate/reset in `ccu-sun60i-a733.c` to prevent PLL register corruption.
- [x] Realign USB0/1 EHCI gates (`BIT 4`) and resets (`BIT 20`) in CCU.
- [x] Realign PRCM R-CCU `r-ahb` to offset `0x000`.
- [x] Sync RemoteProc with safe fallback lookups for `main_ccu` and `sram`.
