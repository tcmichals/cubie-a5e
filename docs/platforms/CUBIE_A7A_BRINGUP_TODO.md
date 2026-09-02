# Cubie A7A Bring-Up TODO

> **Last updated**: 2026-09-01  
> **Active scope**: Ethernet and E907 remoteproc in parallel. USB controllers remain disabled. Do not introduce serial-path changes as part of this work.

## Rules of engagement

- Permanent Linux changes belong in `project-cubie-a5e/patches/linux/`; `bld.a7a/build/linux-7.1/` is the generated working tree.
- Rebuild firmware through Buildroot in `bld.a7a`; see `../buildroot/A7A_KERNEL_PATCH_VALIDATION.md`.
- Before committing a kernel patch series, run `make -C bld.a7a linux-dirclean && make -C bld.a7a linux`.
- Record evidence and outcomes in `CUBIE_A7A_DEBUG_LOG.md`.

## Ethernet — active

- [x] Confirm MAC/MDIO probe reaches MAE0621A-Q3C at PHY address 1.
- [x] Exclude DHCP, IP addressing, MAC DMA, and RGMII data traffic as the first failure: static address produces no carrier, packet counters remain zero, and RJ45 LEDs remain dark.
- [x] Decode schematic sheet 18. PHY U10 has its own Y5 25 MHz crystal; the SoC `EPHY-CLK-25M` route is through unpopulated R116 and cannot be the board PHY reference source.
- [x] Preserve board evidence in `CUBIE_A7A_ETHERNET_SCHEMATIC_REFERENCE.md`.
- [x] Add `ethtool` and `phytool` to the persisted A7A external defconfig and the active `bld.a7a/.config`.
- [ ] Rebuild/package an A7A image containing `ethtool` and `phytool`.
- [ ] On target, capture PHY basic control/status and autonegotiation state with cable inserted and removed.
- [ ] On target, measure U10 `PHY_RESETn`, `VCC3V3_PHY`, `VCCIO_PHY`, and `VDD10_PHY`.
- [ ] Only after those results, decide whether the remaining issue is PHY reset/power/init, cable/magnetics, or RGMII mode/timing.

## Remoteproc — active

- [x] Confirm `cat trace0` causes an ARM64 synchronous external abort in `rproc_trace_read()` / `__pi_strnlen()`.
- [x] Identify the unsafe mapping: `exampleRiscv` declares its trace at E907 ITCM device address `0x0000e000`; current A7A DTS maps it through ITCM host window `0x07110000`.
- [x] Identify the existing shared-memory trace design: `common/arch_riscv/resource_table.c` uses SRAM-C address `0x07138100`, inside the documented `0x07130000` 320 KiB window.
- [ ] Move `exampleRiscv` trace output from ITCM to shared SRAM-C using `SRAM_C_BASE + IPC_TRACE_BUFFER_OFFSET` and `IPC_TRACE_BUFFER_SIZE`.
- [ ] Add the `sram` resource (`0x07130000`, length `0x50000`) to the A7A remoteproc DTS and `reg-names`.
- [ ] Synchronize the DTS into `0001-arm64-dts-allwinner-add-sun60i-a733-cubie-a7a.patch`.
- [ ] Rebuild `riscv-firmware` and clean-rebuild Linux through `bld.a7a`.
- [ ] On target, start remoteproc and read `trace0`; success is readable trace output with no abort.

## USB and power — hold

- [x] Archive the complete V1.10 schematic text and decode USB/power sheets 4, 13–15, and 18.
- [x] Keep USB PHY, EHCI, OHCI, and DWC3 nodes disabled.
- [x] Keep PL2 (`USB0-DRVVBUS`) and PM5 (`USB_HOST_EN`) VBUS regulators non-`always-on` while their consumers are disabled.
- [ ] Do not enable USB until Ethernet and remoteproc test cycles complete.

## Last validated state

- [x] `make -C bld.a7a linux-dirclean && make -C bld.a7a linux` completed successfully on 2026-09-01, applying the entire external Linux patch series to clean Linux 7.1 and installing the A7A DTB.
- [ ] The proposed shared-SRAM remoteproc trace change has **not** yet been applied or validated.
- [ ] The `ethtool`/`phytool` configuration addition has **not** yet been rebuilt into an image.
