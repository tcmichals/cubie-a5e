# Radxa 7.1 Vendor Driver Debug & Porting Log

## Strategy Overview
We have pivoted directly to instrumenting and building the **Radxa 7.1 vendor driver** (`/tmp/radxa_ref/aic8800/aic8800_fdrv/`). Buildroot has been configured to target this tree using `AIC8800_DRIVER_SITE = /tmp/radxa_ref/aic8800`.

This document tracks:
1. **Preprocessor Build Flags & `#ifdef` Configurations**
2. **Line-by-Line Execution Trace Comparisons**
3. **Firmware & NVRAM Dependencies**
4. **Actionable Debug Steps & Test Results**

---

## 1. Buildroot Configuration & Preprocessor Flags

The Buildroot package makefile `project-cubie-a5e/package/aic8800-driver/aic8800-driver.mk` passes the following macro flags:
- `CONFIG_AIC8800_WLAN_SUPPORT=m`
- `CONFIG_SDIO_SUPPORT=y`
- `CONFIG_USB_SUPPORT=n`

### Instrumented Config Banners (`rwnx_main.c`)
At module load, the driver prints out the active state of all preprocessor macros:
```text
=== RADXA 7.1 DRIVER PROBE START ===
[RADXA_71_CONFIG] CONFIG_SDIO_SUPPORT=1
[RADXA_71_CONFIG] CONFIG_OOB=0 CONFIG_GPIO_WAKEUP=0 CONFIG_SDIO_PWRCTRL=0
[RADXA_71_CONFIG] CONFIG_PREALLOC_RX_SKB=1 CONFIG_PREALLOC_TXQ=1 CONFIG_USE_5G=1
```

---

## 2. Execution Milestone Checklist

| Milestone | Target Function / File | Expected Log Output | Status |
|---|---|---|---|
| **1. Module Load & Feature Check** | `rwnx_mod_init` (`rwnx_main.c`) | `[RADXA_71_CONFIG] CONFIG_SDIO_SUPPORT=1` | Pending |
| **2. SDIO Bus Probe & V3 Setup** | `aicwf_sdio_probe` (`aicwf_sdio.c`) | `aicsdio: probe start (func 1, vendor=0xc8a1, device=0x0082)` | Pending |
| **3. IPC Memory Read** | `system_config_8800d80` (`aicwf_compat_8800d80.c`) | `[RADXA_71_TX] CMD: id=1024 len=12 crc8=0xd5` followed by `memdata=0xfb078820` | Pending |
| **4. NVRAM Userconfig Parsing** | `rwnx_plat_userconfig_load_8800d80` | `### Load file done: aic_userconfig_8800d80.txt, size=2724` | Pending |
| **5. LMAC Stack Start (`cmd 123`)** | `rwnx_send_set_stack_start_req` | `[RADXA_71] Sending MM_SET_STACK_START_REQ (cmd 123)...` followed by `[RADXA_71] MM_SET_STACK_START_REQ SUCCESS!` | Pending |
| **6. RF & Power Calibration** | `aicwf_set_rf_config_8800d80` | `[RADXA_71_TX] CMD: id=119 len=103 crc8=0x5e` | Pending |
| **7. Netdev Creation (`wlan0`)** | `rwnx_cfg80211_init` / `wiphy_register` | `wlan0: Registered netdev` | Pending |

---

## 3. Log Inspection Guide for AI & User

When reviewing `dmesg` outputs from the board:
1. `grep RADXA_71`: Shows all high-level execution steps and build configuration.
2. `grep RADXA_71_TX`: Shows every command packet sent over SDIO along with its `id`, `len`, and `crc8`.
3. `grep intstatus`: Shows SDIO interrupt handler status (`0x01` = success IRQ received, `0x00` = hardware silent).

---

## 4. Immediate Commands

### Rebuild Command on Host
```bash
make aic8800-driver-dirclean && make aic8800-driver-rebuild && make
```

### Verification Command on Board
```bash
dmesg | grep RADXA_71
```
