# AIC8800 Wi-Fi Driver Porting & Development Notes

This document consolidates the debugging history, SDIO bring-up process, and future architectural plans for the AIC8800 Wi-Fi driver on the Radxa Cubie A5E flight controller running Linux 7.1.

## 1. SDIO Hardware Bring-up History

### Device Tree and MMC Regulator Fixes
- **Issue**: SDIO was not probing correctly.
- **Fix**: The AIC8800 requires an explicit reset GPIO sequence (`wifi-pwrseq` on `PIO 1 1`), and a dedicated `3v3-wifi` regulator on `PIO 0 7`. We updated the overlay to target `mmc1`, marked it as `non-removable`, set `bus-width = <4>`, and added the `mmc-pwrseq` and `reg_3v3_wifi` regulator definitions extracted from Armbian patches.

### `mmc-pwrseq-simple` Driver Bug in Linux 7.1
- **Issue**: The `mmc-pwrseq-simple` driver failed to initialize the reset GPIO.
- **Fix**: Found a bug in Linux 7.1's new `reset-gpio` fallback framework crashing on Allwinner's 3-cell GPIO configuration. Patched `drivers/mmc/core/pwrseq_simple.c` to bypass the buggy fallback, successfully enabling hardware enumeration (`mmc1:390b:1`).

### SDIO Clock Negotiation Bug
- **Issue**: Driver probe crashed with `error -34` (OUT_OF_RANGE).
- **Fix**: Discovered a hardcoded hack in `aicwf_sdio_func_init` forcing the clock to 60MHz manually. Removed this hack, allowing the kernel to negotiate the safe 40MHz clock defined in the overlay.

## 2. Porting to Linux 7.1 (The Radxa Driver)

After fixing the SDIO hardware layer, we discovered the generic `shenmintao` driver lacked the correct initialization sequence for the `AIC8800D80` chip variant used on this board. We officially migrated to the `aic8800-radxa` driver.

### Automated Patching (`patch_radxa.sh`)
To resolve the `aic8800-radxa` compilation failures on Linux 7.1, we implemented an automated Python-based patching script (`patch_radxa.sh`) that dynamically generates `0001-kernel-7.1-cfg80211-ops.patch` during the Buildroot package build phase. 

### API Changes Handled:
1. **`cfg80211_ops` Signatures**: Injected wrapper functions in `rwnx_main.c` to handle API changes (e.g., `struct net_device*` to `struct wireless_dev*` for `add_key`, `add_station`, etc.).
2. **Spurious Frames**: Used custom macros in `rwnx_defs.h` to supply a default `0` frequency argument to `cfg80211_rx_spurious_frame`.
3. **Timers & Wakeup**: Mapped deprecated `del_timer` to `timer_delete` and updated `wakeup_source_create` to `wakeup_source_register`.
4. **TDLS Action Union Refactor**: Linux 7.1 refactored `struct ieee80211_mgmt`, eliminating the nested `.u.` union. We implemented a text replacement in `patch_radxa.sh` using an `ACTION_U` macro to seamlessly support both kernel versions.

## 3. Future Architectural Upstreaming Plan (Shenmintao Target)

We have decided that rather than endlessly patching the messy Radxa driver, we will pivot to the upstream-focused `shenmintao` repository and build a unified, multi-bus Wi-Fi driver.

### Proposed Architecture Reorganization:

**1. Native Source Migration (Shenmintao Base)**
- Clone the clean `shenmintao` repository locally.
- Update Buildroot to build from this local source (`SITE_METHOD = local`), severing ties with remote repositories so we have full control over the code.

**2. Porting Missing Hardware Initialization**
- Surgically extract the proprietary `AIC8800D80` SDIO initialization sequence (`aicwf_sdiov3_func_init`) from the working Radxa backup and port it into the `shenmintao` codebase.

**3. Unified Hardware Abstraction Layer (HAL)**
Define a common `struct aicwf_bus_ops` containing function pointers for all bus-specific operations:
```c
struct aicwf_bus_ops {
    int (*start)(struct rwnx_hw *rwnx_hw);
    void (*stop)(struct rwnx_hw *rwnx_hw);
    int (*txdata)(struct rwnx_hw *rwnx_hw, struct sk_buff *skb);
    int (*download_fw)(struct rwnx_hw *rwnx_hw, u32 **buffer, u32 size);
    // ...
};
```

**4. Code Cleanup Steps**
- Replace all instances of `rwnx_hw->usbdev->chipid` with a generic `rwnx_hw->chipid`.
- Systematically remove `#ifdef AICWF_USB_SUPPORT` and `#ifdef AICWF_SDIO_SUPPORT` blocks from core networking paths, replacing them with HAL abstractions.
- Retain and refactor BOTH the SDIO (`aicwf_sdio.c`) and USB (`aicwf_usb.c`) code paths to dynamically register their respective operations into the unified HAL `bus_ops` struct. This preserves the multi-interface flexibility of the AIC8800 silicon while eliminating compile-time `#ifdef` pollution.

## 4. Final Breakthrough (Firmware Loading)

Even after compiling against Linux 7.1 and successfully probing the SDIO bus, the driver initially failed to initialize the Wi-Fi MAC because it could not find its firmware blobs.

- **Issue**: The Radxa driver hardcoded `CONFIG_AIC_FW_PATH` to `"/vendor/etc/firmware"`, which is standard for Android devices. Our Buildroot `aic8800-firmware` package installed the blobs to the standard Linux path (`/lib/firmware/aic8800D80`).
- **Fix**: We updated `patch_radxa.sh` to rewrite `aic8800_bsp/Makefile` so `CONFIG_AIC_FW_PATH` points to `"/lib/firmware/aic8800D80"`.
- **Result**: The driver successfully located `fw_patch_table_8800d80_u02.bin`, loaded it, initialized the MAC, and brought up `wlan0` with a valid MAC address (`00:9B:08:EE:97:C9`), allowing `wpa_supplicant` to successfully attach!

## 5. Backup & Restore
Before embarking on the major architectural HAL refactoring, we created a snapshot of the fully patched and functional driver source tree.

**Backup Location:**
`/home/tcmichals/projects/cubie/cubie-a5e/aic8800-radxa-working-backup.tar.gz`

**To Restore:**
If the HAL refactoring breaks the driver and we need to revert to this perfectly functioning snapshot, run:
```bash
cd /home/tcmichals/projects/cubie/bld/build/aic8800-radxa-main/src/SDIO/driver_fw/driver
rm -rf aic8800
tar -xzvf /home/tcmichals/projects/cubie/cubie-a5e/aic8800-radxa-working-backup.tar.gz
```
