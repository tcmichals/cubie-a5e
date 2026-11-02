# Submission Guide: AIC8800D80 Upstream Mainline Driver Fixes

**Recipient List**:
- Submitter: Yanli Yang <yanli.yang@bedmex.com>
- AICSemi Maintainers: Zhirun Liu <zhirun.liu@aicsemi.com>, Dijia Xu <dijia.xu@aicsemi.com>, Chunqiu Liu <chunqiu.liu@aicsemi.com>
- Wireless Maintainers: Johannes Berg <johannes@sipsolutions.net>, Kalle Valo <kvalo@kernel.org>
- Mailing List: `linux-wireless@vger.kernel.org`

---

## 1. Executive Email Body (Cover Letter)

```text
Subject: Re: [RFC PATCH wireless-next v3 0/4] wifi: aic: add AIC8800D80 SDIO FullMAC driver

Hi Yanli, Zhirun, Dijia, and linux-wireless,

We have completed physical hardware validation of your RFC v3 patch series on
real silicon:

  - Platform: Radxa Cubie A5E (Allwinner T527 SoC, ARM64)
  - Chipset: AIC8800D80 SDIO Wi-Fi 6 (802.11ax / HE)
  - Kernel: Linux 7.1.0 PREEMPT_RT

During hardware bring-up and stress validation, we identified and resolved four
critical issues:

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

4. SDIO V3 Sleep Status & PMU Wakeup Polling:
   Patch 4 fixes handshaking register mismatches in `aicwf_sdio.c`. On AIC8800D80
   (V3 register map), `aicwf_sdio_wakeup()` incorrectly polled `wakeup_reg` (0x02)
   checking bit 0 instead of polling `sleep_reg` (0x01) checking bit 4 (`val & 0x10`,
   PMU awake), and `aicwf_sdio_sleep_allow()` wrote to `wakeup_reg` instead of
   `sleep_reg` (0x10), directly conflicting with `aic8800_bsp`.

With these fixes applied:
  - Firmware binaries (`fw_patch_table`, `fw_adid`, `fw_patch`, `fmacfw`) load in <300ms.
  - The MCU application starts cleanly, returning chip version `06090101`.
  - Interface `wlan0` registers, associates with 802.11ax APs, and acquires DHCP.
  - Continuous ping latency achieves a rock-solid 1.28 ms average with 0% packet loss.

Tested-by: Tim Michals <tcmichals@gmail.com>

Best regards,
Tim Michals
```

---

## 2. How to Send Patches via Git

Run the following commands from your terminal:

```bash
# 1. Verify patch formatting
~/projects/cubie/linux-cubie/scripts/checkpatch.pl docs/upstream_patches/*.patch

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
  docs/upstream_patches/0003-wifi-aic-fix-sdio-phase-timing-and-wakeup-sequence.patch \
  docs/upstream_patches/0004-wifi-aic-fix-sdio-v3-sleep-status-and-pmu-wakeup-po.patch

# 3. Send email to mailing list (run only when ready)
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
  docs/upstream_patches/0003-wifi-aic-fix-sdio-phase-timing-and-wakeup-sequence.patch \
  docs/upstream_patches/0004-wifi-aic-fix-sdio-v3-sleep-status-and-pmu-wakeup-po.patch
```
```
