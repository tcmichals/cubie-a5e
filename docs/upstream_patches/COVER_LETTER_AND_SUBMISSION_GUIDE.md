# Submission Guide: AIC8800D80 Upstream Mainline Driver Fixes

**Recipient List**:
- Submitter: Yanli Yang <yanli.yang@bedmex.com>
- AICSemi Maintainers: Zhirun Liu <zhirun.liu@aicsemi.com>, Dijia Xu <dijia.xu@aicsemi.com>, Chunqiu Liu <chunqiu.liu@aicsemi.com>
- Wireless Maintainers: Johannes Berg <johannes@sipsolutions.net>, Kalle Valo <kvalo@kernel.org>
- Mailing List: `linux-wireless@vger.kernel.org`

---

## 1. Executive Email Body (Cover Letter)

```text
Subject: Re: [RFC PATCH wireless-next v2 0/4] wifi: aic: add AIC8800 SDIO FullMAC driver

Hi Yanli, Zhirun, Dijia, and linux-wireless,

We have completed physical hardware validation of your RFC v2 patch series on
real silicon:

  - Platform: Radxa Cubie A5E (Allwinner T527 SoC, ARM64)
  - Chipset: AIC8800D80 SDIO Wi-Fi 6 (802.11ax / HE)
  - Kernel: Linux 7.1.0 PREEMPT_RT

During hardware bring-up, we identified and resolved three critical issues:

1. Kernel Stack Overflow Panic:
   `aicbt_patch_info_unpack()` in `aic_bsp_driver.c` calculated copy size as
   `info_len * sizeof(uint32_t) * 2`. When reading `fw_patch_table_8800d80_u02.bin`,
   this size exceeded the stack-allocated `struct aicbt_patch_info_t patch_info`
   by 4+ bytes, triggering a kernel stack protector panic (`-fstack-protector`).
   Patch 1 adds bounds checking to `memcpy` in `aicbt_patch_info_unpack()`.

2. Linux Kernel 6.x/7.x cfg80211 API Compatibility:
   Patch 2 provides compatibility updates for `netif_rx`, `cfg80211_probe_status`,
   and `remain_on_channel` callback signatures.

3. SDIO Phase Timing & Chip Wakeup Sequence:
   Patch 3 prevents internal IOPAD delay registers (0xF0, 0xF8, 0xF1) from
   overriding hardware timings on standard SDR/HighSpeed 25MHz/50MHz modes
   (which caused MMC CRC errors on Allwinner sun55i/sun60i), adds explicit
   chip wakeup during probe, and adds safe fallback if 0x40500000 pre-boot read
   is not supported by BootROM.

With these fixes applied and `CONFIG_AIC8800_FDRV_NO_REG_SDIO=y` enabled:
  - Firmware binaries (`fw_patch_table`, `fw_adid`, `fw_patch`, `fmacfw`) load in <300ms.
  - The MCU application starts cleanly, returning chip version `06090101`.
  - Interface `wlan0` registers, acquires DHCP (`192.168.1.15`), and passes ping
    traffic (0% packet loss).

Tested-by: Tim Michals <tcmichals@yahoo.com>

Best regards,
Tim Michals
```

---

## 2. How to Send Patches via Git

Run the following commands from your terminal:

```bash
# 1. Verify patch formatting
git checkpatch docs/upstream_patches/*.patch

# 2. Dry-run send-email
git send-email \
  --to="yanli.yang@bedmex.com" \
  --cc="zhirun.liu@aicsemi.com" \
  --cc="dijia.xu@aicsemi.com" \
  --cc="chunqiu.liu@aicsemi.com" \
  --cc="johannes@sipsolutions.net" \
  --cc="kvalo@kernel.org" \
  --cc="linux-wireless@vger.kernel.org" \
  --dry-run \
  docs/upstream_patches/0001-wifi-aic-fix-stack-buffer-overflow-in-aicbt_patch_i.patch \
  docs/upstream_patches/0002-wifi-aic-update-cfg80211-API-compatibility-for-mode.patch \
  docs/upstream_patches/0003-wifi-aic-fix-sdio-phase-timing-and-wakeup-sequence.patch

# 3. Send email to mailing list
git send-email \
  --to="yanli.yang@bedmex.com" \
  --cc="zhirun.liu@aicsemi.com" \
  --cc="dijia.xu@aicsemi.com" \
  --cc="chunqiu.liu@aicsemi.com" \
  --cc="johannes@sipsolutions.net" \
  --cc="kvalo@kernel.org" \
  --cc="linux-wireless@vger.kernel.org" \
  docs/upstream_patches/0001-wifi-aic-fix-stack-buffer-overflow-in-aicbt_patch_i.patch \
  docs/upstream_patches/0002-wifi-aic-update-cfg80211-API-compatibility-for-mode.patch \
  docs/upstream_patches/0003-wifi-aic-fix-sdio-phase-timing-and-wakeup-sequence.patch
```
