# Cubie A7A Bring-Up TODO

> **Restart here.** Last updated: 2026-09-01.
>
> Active scope: Ethernet and E907 remoteproc in parallel. USB remains disabled. Do not change serial transport for these tasks.

## Mandatory patch gate

- [x] Clean Buildroot Linux validation passed: `make -C bld.a7a linux-dirclean && make -C bld.a7a linux` reapplied all A7A external kernel patches to clean Linux 7.1 and built the A7A DTB.
- [ ] Before every patch commit, rerun that clean-tree gate.
- [ ] Permanent kernel changes must be synchronized into `project-cubie-a5e/patches/linux/`; never commit a fix that exists only in `bld.a7a/build/linux-7.1/`.

See `docs/buildroot/A7A_KERNEL_PATCH_VALIDATION.md`.

## Ethernet: no physical carrier

- [x] MDIO finds U10 Maxio MAE0621A-Q3C at PHY address 1.
- [x] U-Boot pre-start MDIO reads returned `0x0000` at every address because the Ethernet controller had not entered its normal start path; do not use pre-start `mdio` results as PHY evidence.
- [x] U-Boot target proof: two pings to live peer `192.168.1.2` succeeded, while absent peer `.3` correctly failed. PHY power/reset, MDIO, RGMII, MAC DMA, board routing, magnetics, and cable are functional. The Ethernet fault is isolated to Linux configuration.
- [x] Running U-Boot control DT has GMAC enabled, PH0–PH15 muxed to function 5, PHY address 1, and PH16 active-low reset released high. Treat the old vendor U-Boot source as register/routing evidence only.
- [x] Post-ping U-Boot reads return teardown state (`0x0200341c = 1`, syscfg and standalone MDIO reads zero); they are not active-state references. The successful ping is the valid datapath proof.
- [ ] In Linux, capture `/proc/interrupts` before and after traffic. Vendor GMAC210 explicitly enables split multi-MSI and requests TX0/RX0 IRQs; current `dwmac-sun55i` does not enable multi-MSI, so SPI 173/174 are not requested.
- [ ] If target IRQ counts confirm only inactive `macirq`, review the minimal GMAC210 variant fix: standard queue IRQ names in DTS plus `STMMAC_FLAG_MULTI_MSI_EN` in A733 glue. This cannot be solved in DTS alone.
- [x] A later target boot reports `Link is Up - 1Gbps/Full`; PHY power/reset, copper link, autonegotiation, and basic RGMII MAC/PHY integration are confirmed.
- [x] TX DMA failure confirmed: `NETDEV WATCHDOG: transmit queue 0 timed out` repeats every 5–6 seconds with zero RX and only 1314 TX bytes. Adapter reset/re-probe causes the repeated `Link is Up` messages.
- [x] Demoted MAE0621 probe/version/self-check/remove banners from unconditional `printk()` to `phydev_dbg()`; normal console output now retains only real link transitions and MAC watchdog/reset diagnostics.
- [x] GMAC core-clock parent and `rgmii-id` correction are active: PTP clock now registers and link remains 1 Gbps/full duplex, but TX DMA watchdog timeouts persist.
- [x] Added A733 CCU/DTS resource wiring only: `CLK_GMAC_PTP`, plus distinct GMAC AXI and MAC reset IDs. The A7A DTS now supplies `ptp_ref` and standard `stmmaceth`/`ahb` reset names; MAC and PHY driver code is unchanged.
- [x] Schematic: PHY has its own 25 MHz crystal Y5. The SoC `EPHY-CLK-25M` route is unpopulated (R116), so the CCU output cannot fix PHY link.
- [x] Added `ethtool` and `phytool` to the A7A external defconfig and verified them in the rebuilt target at `/usr/sbin/ethtool` and `/usr/bin/phytool`.
- [x] Built and structurally audited the diagnostic A7A SD image.
- [ ] On target, capture `ethtool eth0`, interface counters, and ping/traffic results while watching `dmesg -w`.
- [ ] If `Link is Up`/`Link is Down` messages recur with different timestamps, capture PHY control/status and autonegotiation registers across the transition to diagnose link flap.
- [ ] Build and target-test the DTS/CCU-only GMAC resource update; verify TX completion before making any other Ethernet change.

See `docs/platforms/CUBIE_A7A_ETHERNET_SCHEMATIC_REFERENCE.md` and `docs/platforms/CUBIE_A7A_DEBUG_LOG.md`.

## Remoteproc: unsafe trace mapping

### Upstream-quality acceptance gate

- [ ] Add a reviewed Devicetree binding that defines the A733 E907 compatible, required clocks/resets/mailbox, and memory-resource contract.
- [ ] Define one common E907 firmware device-address map for A5E and A7A. Use core-local DTCM for portable trace storage; DTS translates its common core address to each SoC's physical DTCM window.
- [ ] Use only the documented binding contract in `sunxi_rproc.c`; no board-name conditionals, raw register writes, or legacy vendor-driver behavior.
- [ ] Represent Linux-readable trace/shared memory as explicitly CPU-accessible shared SRAM or reserved memory; never expose a TCM mapping to generic debugfs unless host reads are verified safe.
- [ ] Keep ELF load, start/stop, mailbox kick, error unwind, and `.remove()` lifetimes aligned with current remoteproc subsystem conventions.
- [ ] Run clean Buildroot patch application/build and target lifecycle/trace tests before submission.
- [ ] Preserve the complete Git history for `patches/linux/0002-remoteproc-sunxi-add-allwinner-riscv-remoteproc.patch`, A7A DTS patch `0001`, and A5E DTS patch `0005` in the engineering record before replacing a prior memory layout.

- [x] Reproduced two faults: ITCM trace at `0x0000e000` aborts on generic debugfs read, and an ELF DTCM segment at `0x00080000` caused an SError in `rproc_elf_load_segments()`.
- [x] Found and fixed the DTCM loader bug: `sunxi_rproc` returned `is_iomem = false` for `__iomem` TCM mappings, so remoteproc used generic `memcpy()`/`memset()` instead of `memcpy_toio()`/`memset_io()`.
- [x] TCM mappings now use `devm_ioremap_wc()` and report `is_iomem = true`, matching the in-tree ZynqMP R5 TCM model. A7A DTS again declares separate 64 KiB ITCM and DTCM resources.
- [x] Hardened debugfs: TCM addresses return `NULL` when `da_to_va()` is called without an I/O-memory result pointer, preventing `rproc_trace_read()` from calling `strnlen()` on an I/O-mapped TCM buffer.
- [x] Legacy-kernel audit: the vendor remoteproc driver uses DTS `memory-mappings` device-address/length/physical-address triples and `ioremap_wc()`; it does not prove a fixed A733 DTCM alias. The vendor A733 DTS has no E907 remoteproc mapping table.
- [x] Rebuilt the firmware package, kernel, and audited SD image with explicit DTCM ELF segments and correct I/O-copy semantics.
- [x] Target remoteproc starts successfully with ITCM/DTCM segments after the I/O-memory mapping fix; no SError during firmware load.
- [x] Target still exposes `trace0`, proving it was booted with an older firmware resource table containing `RSC_TRACE`; the current TCM-loader test firmware intentionally has an empty resource table and must be verified in the flashed rootfs before retesting remoteproc.
- [x] Target-verified debugfs safety: `cat trace0` returns `Trace not available` with no kernel abort.
- [x] Keep generic `trace0` disabled in the current test firmware: its implementation uses `strnlen()` and cannot safely consume an I/O-mapped TCM address.
- [ ] Design a normal-memory reserved carveout for Linux-readable live trace before reintroducing an `RSC_TRACE` entry.
- [ ] Add explicit 64 KiB ITCM and DTCM resources to the A5E remoteproc DTS and validate the common driver on A5E.

**Local-source package rule:** after modifying `riscv-firmware/`, run `make -C bld.a7a riscv-firmware-dirclean riscv-firmware` before repacking. A plain rebuild can retain pre-existing local package outputs when timestamps are preserved during rsync.

- [x] Removed stale A7A overlay copy `board/radxa/cubie_a7a/rootfs-overlay/lib/firmware/riscv-firmware.elf`; it overwrote the package-built ELF during `target-finalize`.
- [x] Repacked image verifies trace DA `0x4e000000`, length `0x8000`; build, target, and packed ELF hashes all equal `927e9b7647a80fa92e9e52a813dcabb30ee8c7aa2a90d1142d8c3a9f477a587e`.
- [ ] Flash corrected `bld.a7a/images/sdcard.img`, start remoteproc, and target-test `trace0`.

## USB and power: hold

- [x] Archive full schematic as `docs/extracted_vendor/radxa_cubie_a7a_v1.10_schematic.txt`.
- [x] Keep USB PHY, EHCI, OHCI, and DWC3 disabled.
- [x] Keep VBUS enable GPIO regulators PL2 / PM5 non-`always-on` while USB consumers remain disabled.
- [x] Audit confirms USB0/USB1 critical PHY clocks, controller resets, MSI-Lite2 interconnect gate, and PL2/PM5 VBUS control are represented. This is not currently the same incomplete-resource failure identified for GMAC.
- [ ] Before enabling USB later, change DWC3 `ref` from duplicate `CLK_BUS_USB2` to `CLK_USB_REF`; retain `CLK_BUS_USB2` as `bus_early`.
- [ ] Do not enable USB until Ethernet and remoteproc test cycles complete.

## Engineering record

Update `docs/platforms/CUBIE_A7A_DEBUG_LOG.md` after every target test and when any TODO item changes state. Detailed task history is also maintained at `docs/platforms/CUBIE_A7A_BRINGUP_TODO.md`.
