# A7A board test: A733 CCU v6 (2026-11-11)

Board: Radxa Cubie A7A (`cubie-a7a-flight`), serial `/dev/ttyUSB1`, eth0 `192.168.3.4` (1 Gbps to the PC's enp2s0).

## Kernel under test

- Source: `linux-cubie` `a7a_remoteproc_camera_usb` at `1b0179cd1a18` (v6 A733 CCU + sunxi-clk-no-p prerequisite + our commits, key-field commit reverted). Built in-tree with the board's running config (`/proc/config.gz` of kernel #23) plus `VIDEO_SUNXI=y`, `VIDEO_SUNXI_CEDRUS=m`, `LOCALVERSION_AUTO=n`, `LOCALVERSION=`.
- `Linux version 7.1.0 ... #23 SMP PREEMPT_RT Wed Nov 11 18:30:32 CST 2026`; `kernel.release` = `7.1.0`; vermagic `7.1.0 SMP preempt_rt mod_unload aarch64` = the board's modules (`cfg80211.ko`). The source changes since the board kernel touch only `drivers/clk/sunxi-ng`, Cedrus and DT, so the board's `/lib/modules/7.1.0` was kept unchanged. `sunxi-cedrus.ko` is only in `/root/cedrus-test/` (loaded by hand, not autoloaded).
- Installed in `/boot` (md5):
  - `Image` `5183b18e79f1378d32087c1fd5d7a4ff`
  - `sun60i-a733-cubie-a7a.dtb` `040529167241c91877ecda1336099acc` (as committed, `osc24M` = 24 MHz; = `downloads/a733-ccu-v6/out/`)
  - `sun60i-a733-cubie-a7a-26m.dtb` `621ad613682ba5318cdecf78e1de2ab5` (same DTB, only `/clocks/osc24M clock-frequency` = 26000000; used from U-Boot by hand)
  - `cubie-a7a-rproc.dtbo` `e3f5192f1b01c1b278d86133542acf8f`, `cubie-a7a-ov5647.dtbo` `14d49d93daa407b60c071bfd77906f76`, `cubie-a7a-imx415.dtbo` `a24980fa211fb895d35f4669149e7601` (rebuilt from the current dtso; equal to `downloads/a733-ccu-v6/out/`). `fdtoverlay`: rproc+ov5647 and imx415 apply to both DTBs.
- Copies: `~/projects/cubie/board-backups/a7a/20261111-v6/installed-v6/`; build config `linux-cubie-build-v6.config` there.

## Backups (old kernel #23, 2026-11-10 20:21)

- Board: `/boot/bak-k23/` and `/root/bak-k23/` (`Image` `e4e0150d…`, `sun60i-a733-cubie-a7a.dtb` `e689a702…`, `cubie-a7a-rproc.dtbo` `5ce5bccb…`, `cubie-a7a-ov5647.dtbo` `44ecb4e2…`, `boot.scr`, `config.txt`). The old dtbo must go with the old DTB (old reset ID 12 is `RST_BUS_R_CPUCFG` on the new binding).
- PC: `~/projects/cubie/board-backups/a7a/20261111-v6/` (same files + `uEnv.txt`, `uboot.env`, running config).
- Restore from U-Boot (115200, press a key at `Hit any key to stop autoboot`): paste the lines of `uboot-restore-k23.txt` in this directory (loads `bak-k23/…` and boots). Permanent restore from Linux: `cp /boot/bak-k23/{Image,sun60i-a733-cubie-a7a.dtb,cubie-a7a-rproc.dtbo,cubie-a7a-ov5647.dtbo} /boot/ && sync`.

## Main finding: the A7A DCXO is 26 MHz, the DT says 24 MHz

- `devmem 0x02002000` (PLL_REF) = `0xF8675F00`: enable/lock set, N [15:8] = 0x5F → ×96, M [22:16] = 0x67 → ÷104, P bit 1 = 0. The boot chain set 26 MHz × 96 / 104 = **24 MHz**. Schematic: `DCXO-SYS 26MHz_16PF_10PPM`.
- v6 models PLL_REF as an NKMP clock of `hosc` (upstream gets `hosc` from the RTC's DCXO detection; our DT has no RTC node and a fixed `osc24M` of 24 MHz, `f371e78bddac`). So Linux computes every CCU rate 12/13 too low.
- Boot 1, DTB as committed (`boot1-v6-24m-*.txt`, `serial-boot1-v6-24m.log`): pll-ref 22 153 846, pll-periph0-2x 1 107 692 300, 600M = 553 846 150, 400M = 369 230 766, mmc0 30 769 230, csi 553 846 150, ahb 184 615 384, apb-uart/bus-uart0 22 153 846, gmac0-phy 23 076 923. **The serial console is garbage after `Starting kernel`** at 115200; reading the port at **125000 baud** (115200 × 26/24) shows a clean login prompt. U-Boot output is fine. Hub port 1-1.4 (AIC8800) failed to enumerate (`device not accepting address`, `error -71`, `-110`). eth0 and ssh worked.
- Boot 2, same DTB with `osc24M` = 26 MHz: clean console at 115200, all rates right (below). One-line fix for the A7A DT: `clock-frequency = <26000000>;` on `osc24M` in `sun60i-a733-cubie-a7a.dts` (better: an RTC node providing `hosc`, as upstream intends). Not committed (report only). The A7Z DT has the same 24 MHz value; its crystal was not checked.
- The R-CCU parents `r-apb0`/`r-apb1`/`r-riscv`/timers directly on `hosc`, so with the fix they read 26 MHz. Whether the PRCM really runs from the raw DCXO or from the 24 MHz PLL_REF output is not verified.

## Checklist (boot 2: v6 Image + 26 MHz DTB + new overlays, `clk_ignore_unused` on)

| # | Item | Result | Evidence |
|---|------|--------|----------|
| 1 | PLL_REF / clk_summary | PASS (with 26 MHz DTB); FAIL with the committed DTB | PLL_REF `0xF8675F00` = 26→24 MHz. hosc 26 000 000, pll-ref 24 000 000, sys-24M 24 MHz, periph0-4x 2.4 GHz, -2x 1.2 GHz, 800M/600M/480M/400M/300M/200M/160M/150M exact, mmc0 50 MHz, apb-uart/bus-uart0 24 MHz, ahb 200 MHz, apb0 100 MHz, apb1 24 MHz (`boot2-v6-26m-clk_summary.txt`) |
| 2 | SD boot, UART, dmesg | PASS (26 MHz DTB) | `uname -a` above; mmc0 reparent works (rootfs on mmcblk0p2); dmesg errors are the known ones: `usb0_reset`, DWC3 power-domain deferral, `failed to get MIPI D-PHY` (deferral), E902 firmware -2 before rootfs, aic_load_fw -1 |
| 3 | USB / wifi / Ethernet | PASS except wifi (pre-existing) | xHCI/DWC3 probes after deferral (GSNPSID `0x33313130`), EHCI ×2, OHCI ×2, hub `1a40:0101`, AIC8800 `a69c:8d81` enumerates; usb2-u2-ref on 24 MHz, usb2-u3-utmi on 60 MHz, usb01-ref on. Wifi: `aic_load_fw 1-1.4:1.x probe failed -1` (AICWFDBG `Read FW mem`), wlan0 up without IPv4: same as kernel #23 earlier today. eth0 1 Gbps full, `192.168.3.4`, mbus-gmac0 on 600 MHz, bus-gmac0 on, gmac0-phy 25 MHz (enable count 0: only the probe write keeps it on) |
| 4 | THS | PASS | 5 zones 45.1-45.3 °C idle; `ths` (GIC 71) 440 → 450 in 3 s; bus-ths0 on |
| 5 | Msgbox + E902 RPMsg | PASS with stalls (regression vs #23 not proven) | `3004000.mailbox -> sun55i-msgbox`; `testPingRpmsg.elf` → `creating channel rpmsg-ping-channel addr 0x400`. `ping_rpmsg -n 1000` ×4: 1000/1000, 1000/1000, 1000/1000, 999/1000 (1 timeout); min 106.8-117.3 µs, p50 128.4, p90 134.4, p99 137.9-150.7 µs, **max 0.66-0.96 s** (one stall per run). `-s 16 -n 3000`: min 41.5 µs, p99 72.1 µs, max 0.97 s. 10 s continuous (`-n 0`, killed): ~4400 pings, **5 timeouts (1 s)**. Kernel #23 this morning: 1000 pings, max 171.8 µs. Stop/start ×5: all `offline → running`, 200/200 pings each; mailbox IRQ 12 430. No WARN (`boot2-rpmsg.txt`, `boot2-e902-cycles.txt`) |
| 6 | Camera ov5647 | PASS | 300 frames 1920×1080 `pGAA`, steady 30.00 fps; `0x02003840` = `0x04000000` idle, `0x84000000` streaming (gate on, mux 4, M 0); clk csi 600 MHz, mbus-csi 600 MHz on, bus-csi 200 MHz; `0x020025E4` `0x00040A06` → `0x00040B06` (mbus-csi bit 8 set by RMW while streaming) (`boot2-camera.txt`) |
| 7 | VE encoder | PASS (probe), warning | `insmod sunxi-cedrus.ko` → `1c10000.video-codec -> cedrus`, `/dev/video1` `cedrus-enc` (OUTPUT NV12, CAPTURE H264); ve-enc0 400 MHz. `sunxi-pck-600 7060000.power-controller: failed to turn domain "VE_ENC" off: -110`. No encoder tool on the image, nothing encoded (`boot2-ve.txt`) |
| 8 | Warm reboot | **FAIL** | Boot 1→2 `reboot`s (E902 offline) reset fine. Third `reboot` (26 MHz DTB, E902 running `testPingRpmsg.elf`, Cedrus loaded with VE_ENC stuck on): shutdown completes, `Requesting system reboot`, then nothing. No reset after 4+ minutes, no ping, no SysRq on the serial break. Stuck below Linux (PSCI/BL31 or the PWROK reset path, see CUBIE_A7A_DEBUG_LOG.md "Reboot / watchdog — NOT fixed"). Cause not isolated: E902 running vs VE_ENC domain vs neither. The `clk_ignore_unused`-off boot was not reached (`serial-boot3-reboot-26m.log`, `serial-boot3-sysrq.log`) |
| 9 | Power-off | NOT RUN | board hung at item 8 |

## Board state at the end

**Hung after `Requesting system reboot`; needs a power cycle by the owner.** The board lock was in `/run` and is gone with the reset. After power-on, `boot.scr` boots the **new v6 Image with the committed 24 MHz DTB**: Linux console at 125000 baud (garbage at 115200), ssh on `192.168.3.4` works. Then either:

- keep v6 and get a usable console: `cp /boot/sun60i-a733-cubie-a7a-26m.dtb /boot/sun60i-a733-cubie-a7a.dtb && sync` (over ssh), or
- go back to kernel #23: U-Boot commands in `uboot-restore-k23.txt`, or over ssh `cp /boot/bak-k23/{Image,sun60i-a733-cubie-a7a.dtb,cubie-a7a-rproc.dtbo,cubie-a7a-ov5647.dtbo} /boot/ && sync`.

Still to test after the power cycle: warm reboot with the E902 offline and Cedrus not loaded (to split the two suspects), the `clk_ignore_unused`-off boot (`uboot-v6-26m-no-clk-ignore.txt`; gmac0-phy has enable count 0 and may be cut), ahb-store while mmc0 is busy, `poweroff`, and the RPMsg 1 s stalls on kernel #23 with `ping_rpmsg -n 1000` ×4 and `-n 0` for 10 s (same firmware), to tell whether the stalls are new with v6.

## Files

- `bootcap.py`, `ser.py`: serial helpers (password from `PW`).
- `uboot-*.txt`: U-Boot command lists (restore #23, v6 + 26 MHz, v6 without `clk_ignore_unused`).
- `baseline-k23.txt`, `boot1-v6-24m*.txt`, `boot2-*.txt`, `serial-*.log`, `build.log`.
