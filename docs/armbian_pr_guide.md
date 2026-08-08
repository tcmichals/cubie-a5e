# Armbian Pull Request Guide: AIC8800 Upstream Driver Support

## Summary of Changes
1. **Replaces Outdated DKMS Blob**: Armbian previously fetched legacy `radxa-pkg/aic8800` DKMS `.deb` binaries from 2022 that fail on modern kernels (Linux 6.x/7.x).
2. **Upstream Driver Architecture**: Integrates the official `wireless-next` RFC driver (`aic8800_bsp.ko` + `aic8800_fdrv.ko`) patched with Linux 6.x / 7.x kernel API compatibility and stack protection fixes.
3. **Unlocks Modern Kernels**: Removes the artificial `KERNEL_MAJOR_MINOR ge 7.1` restriction in `extensions/radxa-aic8800.sh`.

---

## Armbian PR Pull Request Blueprint

### Title:
`fix(extensions/radxa-aic8800): update AIC8800 Wi-Fi driver for Linux 6.x/7.x kernels`

### Description:
```markdown
### Description
The current `radxa-aic8800` extension relies on obsolete `radxa-pkg/aic8800` DKMS releases that fail to build on Linux 6.x/7.x kernels, causing Armbian builds for Radxa boards (Cubie A5E, Rock 4/5) to skip Wi-Fi module installation.

This PR updates the extension to build the updated AIC8800 out-of-tree driver based on the upstream `wireless-next` architecture (`aic8800_bsp.ko` + `aic8800_fdrv.ko`).

### Key Fixes Included:
- **Stack Protector Panic**: Fixes a 4-byte buffer overflow in `aicbt_patch_info_unpack()` (`aic_bsp_driver.c`).
- **SDIO Bus Sharing**: Configured with `CONFIG_AIC8800_FDRV_NO_REG_SDIO=y` for seamless BSP/WLAN driver bus sharing.
- **Kernel 6.x/7.x Compatibility**: Fixed `netif_rx`, `cfg80211_probe_status`, and `remain_on_channel` callback signatures.

### Testing
- Tested on **Radxa Cubie A5E** (Allwinner T527 SoC, ARM64).
- Firmware upload (`fw_patch_table`, `fw_adid`, `fw_patch`, `fmacfw`) completes in <300ms.
- Interface `wlan0` registers, acquires DHCP (`192.168.1.15`), and passes network traffic.
```
