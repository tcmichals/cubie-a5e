# A5E board test 2026-11-11: cpufreq kernel + reset under load

Board: Radxa Cubie A5E (T527), `cubie-a5e-flight`, wlan0 192.168.1.4 (eth0 192.168.3.3 also up).
Serial: `/dev/ttyUSB0` (identified by echoing a marker to the board's `/dev/ttyS0`), one reader,
full log `serial-20261111.log` with `=== PC <time> ... ===` markers per step.
Board lock `/run/abstractx-board.lock` taken for every change, released for the AbstractX runner.
Fan on (owner's setting), not touched.

## Part A: kernel

- Source `linux-v4-71` `cubie-linux-7.1` @ `7fe4eb19f9f0` (v4 + Przywara A523 CPU CCU v3 + own
  frequency-only OPP dtsi), tree not edited.
- Built in `O=~/projects/cubie/kobj-a5e-test` (removed after the test), gcc 16.2 from `bld.a5e/host`.
  Config = board `/proc/config.gz` + `project-cubie-a5e/board/radxa/cubie_a5e/linux.config` (merge_config)
  + olddefconfig (`config-7.1.0-cpufreq`). Confirmed `SUN55I_A523_CPU_CCU=y`, `CPUFREQ_DT=y`,
  `CPU_FREQ_THERMAL=y`, `SOFTLOCKUP_DETECTOR=y`, `HARDLOCKUP_DETECTOR=y` (buddy), `DETECT_HUNG_TASK=y`,
  `WATCHDOG_SYSFS=y`, `LOCALVERSION_AUTO` off. Build rc 0; only warning: existing `apb1_hws` (A733).
- **Release `7.1.0-cpufreq`, not `7.1.0`.** The board kernel was built with gcc 15.1 and without
  `DETECT_HUNG_TASK` (which adds `task_struct` fields); vermagic does not catch either, so modules
  built here must not land in the old kernel's `/lib/modules/7.1.0`. New modules (767 in-tree +
  `aic8800_bsp`/`aic8800_fdrv` rebuilt out of tree from a copy of `aic8800-rfcv3` with the package's
  make options) installed in `/lib/modules/7.1.0-cpufreq`; host `depmod` (board has none), index
  files copied. `/lib/modules/7.1.0` untouched, so the old Image still finds its own modules.
- DTB built with `DTC_FLAGS=-@` (the board's DTB has `__symbols__` for overlays; without `-@` it is
  27.7 KB vs 35.6 KB). Node/property diff vs the board DTB: only OPP tables, CPU `clocks`,
  `operating-points-v2`, `dynamic-power-coefficient`, `cpu_ccu`, `map1` cooling maps.
  `fdtoverlay` with `cubie-a5e-flight-stack-e906.dtbo`: OK.
- Installed: `/boot/Image` md5 `a1bc9d4d2e1f8896c28693281e24fd0f` (29,125,120 B),
  `/boot/sun55i-a527-cubie-a5e.dtb` md5 `d15650d8d5b4e30ae3a123fe5734e975` (37,388 B).
  Copy: `~/projects/cubie/board-backups/a5e/20261111/new-7.1.0-cpufreq/` (Image, DTB, modules tgz, System.map).
- Backup of the previous kernel: `/boot/backup-20261111/` (Image md5 `7f115d26453e117c16cc935a78880fd2`,
  DTB `aee824e2f8cb14b69188ff7b94ee1d44`, all dtbo, boot.scr, config.txt, uEnv.txt), `/root/backup-20261111/`
  (Image + DTB), PC `~/projects/cubie/board-backups/a5e/20261111/`.
- `config.txt` as the Copilot session left it: `dtoverlay=cubie-a5e-flight-stack-e906`;
  `riscv-firmware.elf -> e906_repl_app.elf`. Kept.

### Restore the previous kernel

From Linux: `cp /boot/backup-20261111/Image /boot/backup-20261111/sun55i-a527-cubie-a5e.dtb /boot/ && sync && reboot`.

From U-Boot (serial, press a key at "Hit any key to stop autoboot"):

```
load mmc 0:1 0x40200000 backup-20261111/Image
load mmc 0:1 0x4fa00000 backup-20261111/sun55i-a527-cubie-a5e.dtb
fdt addr 0x4fa00000; fdt resize 0x10000
load mmc 0:1 0x4fe00000 cubie-a5e-flight-stack-e906.dtbo; fdt apply 0x4fe00000
setenv bootargs "console=ttyS0,115200 earlycon root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8"
booti 0x40200000 - 0x4fa00000
```

Old kernel = release `7.1.0` → `/lib/modules/7.1.0` (unchanged). `/lib/modules/7.1.0-cpufreq` can stay.

Boot 1: login prompt, `uname -r` = `7.1.0-cpufreq`, wlan0 + eth0 up, aic8800 loaded, E906 running,
KUnit `sun55i_msgbox` 37/37 and `sunxi_rproc` 29/29 pass, no WARN/BUG (`dmesg-boot1-7.1.0-cpufreq.txt`).

## Part B: cpufreq

| Check | Result |
|---|---|
| Speed bin, SID `0x03006248` | `0x00000049` → bits[19:12] = 0 → combi = bits[11:4] = `0x04` → **vf0300** |
| CPU CCU probe | OK, no `-EEXIST` |
| Boot rate read by the CCU | **768 MHz** on cpu-l, cpu-b and DSU (PLL N field `0x20`) |
| cpufreq at probe | `CPU0/CPU4: Running at unlisted initial frequency: 768000 kHz, changing to: 792000 kHz` → PLL L/B reprogrammed to N = `0x21` |
| `clk_summary` after boot | `cpu-l` 792 MHz, `cpu-b` 792 MHz, `cpu-dsu` **768 MHz** (unchanged throughout) |
| `cpufreq/` | `policy0` (cpu0-3), `policy4` (cpu4-7; read with cpu4 briefly online, then offlined again) |
| `scaling_available_frequencies` | `408000 600000 792000` on both |
| driver / governor | `cpufreq-dt`; policy0 `schedutil`; policy4 reported `performance` |
| cooling devices | `cpufreq-cpu0`, `cpufreq-cpu4` (max_state 2), plus `7122000.npu` |
| trips cpu0/cpu4 zones | 70 °C passive, 90 °C passive, 110 °C critical |
| transitions (scaling_max_freq 408/600/792) | PLL_L `0xF8802201` (N 34, M1 2 = 408), `0xF8801900` (N 25 = 600), `0xF8802100` (N 33 = 792); DSU stays `0xF8802000`; `cpuinfo_cur_freq` matches |

**U-Boot runs the CPUs at 768 MHz, not 792 MHz.** T527 User Manual V0.94 §2.2.9.1 (CPU_L_PLL_CTRL_REG,
`tina-source/manuals-text/T527_User_Manual_V0.94.txt:3440-3484`): `CPU_L_PLL = InputFreq*N/(M0*M1)`,
`N = PLL_N`, default `0x14` = 480 MHz. Linux (Przywara v3, offset 0) and the vendor BSP
(`ccu-sun55iw3-displl.c:48`, offset 0) agree. Mainline U-Boot 2026.01 `clock_set_pll()` writes
`CCM_PLL1_CTRL_N(n) = ((n) - 1) << 8` (`arch/arm/include/asm/arch-sunxi/clock_sun50i_h6.h:52`,
shared with H6, where N is offset 1) for the A523 too, so `CONFIG_SYS_CLK_FREQ=792000000` gives
24 MHz × 32 = 768 MHz on all three PLLs. Earlier project notes saying "U-Boot 792 MHz" are wrong.
With this kernel cpu-l/cpu-b now really run 792 MHz at 0.90 V (+3 %); DSU stays at 768 MHz.

**schedutil does not lower the frequency at idle on this PREEMPT_RT kernel:** 0 transitions over
several minutes at idle, 100 % at 792 MHz (`idle-schedutil.txt`, `transition-test.txt`). Not
investigated further (runnable RT kthreads such as `ktimers`/`rcuc` make schedutil pick the maximum).
The thermal cooling device works through the frequency limit, so this does not block throttling.

Load, 4 × `yes`, 5 min, fan on (`load-4core-yes-5min.txt`): idle 46-48 °C, peak **51.1 °C**, back to
48 °C 30 s after. The 70 °C trip was never reached: **throttling not triggered** (`cur_state` 0
throughout, no 408 MHz residency from the thermal path). Fanless throttle test still open.

cyclictest (`-m -S -p 90 -i 1000`): idle 30 s max 37/42/36/54 µs; under 4 × `yes` 60 s max 29/36/30/24 µs.

remoteproc/msgbox: E906 boots `riscv-firmware.elf` (`e906_repl_app.elf`), channels `abstractx-repl/tlp/trace`;
3 stop/start cycles OK (`rproc-cycles.txt`). `ping_rpmsg` not run: this firmware has no
`rpmsg-ping-channel` (needs the testPingRpmsg firmware and overlay). The v4 sweep was not run.

## Part C: reset under load

Watchdog state (`watchdog-sysfs-pre.txt`, `wdt-regs-e906-riscvfw.txt`):
- `/sys/class/watchdog/watchdog0`: `sunxi-wdt`, `state=inactive`, `timeout=16`, `timeleft=0`, `bootstatus=0`.
- **The CPUX watchdog is not running in Linux.** `sunxi_wdt_probe()` calls `sunxi_wdt_stop()`
  (`drivers/watchdog/sunxi_wdt.c`), U-Boot prints `WDT: Not starting watchdog@2050000`, and
  `WDT_MODE` (`0x02050014`, sun55i layout) reads 0. `watchdogd` ran 2 time slices (45 µs) in 6 min.
  The earlier "watchdogd starved at FIFO 50" hypothesis cannot explain these resets.
- All four SoC watchdogs (CPUX `0x02050000`, CPUS `0x07020400`, DSP `0x07100400`, RISCV `0x07132000`,
  manual §2.18.5) read `WDT_MODE` 0 and `WDT_IRQ_STAT` 0, with `riscv-firmware.elf` and with `abstractx.elf` under load.
- Lockup detectors: `watchdog=1`, `watchdog_thresh=10` (report only, no panic), hung task 120 s.

Runs (AbstractX `run_e906_bench_suite.py`, evidence `AbstractX/docs/verification/evidence/2026-11/part4_bench_2026-11-12T*`):

| Run | Row | CPU | Result | Uptime before → after |
|---|---|---|---|---|
| 1 | `freertos_wait_cyclic_load` | 792 | pass | 13 → 15 min |
| 2 | `abstractx_bench_load` | 792 | **reset** (after E906 start; no timestamp) | 15 min → 3 min |
| 3 | `abstractx_bench_load` + diagnostics | 792 | **reset** 8.7 s after E906 start | 6 min → 3 min |
| 4 | `abstractx_bench_load` + diagnostics | 408 cap | pass | 3 → 4 min |
| 5 | `abstractx_bench_load` | 408 cap | pass | 4 → 4 min |
| 6 | `abstractx_bench_load` | 408 cap | pass | 4 → 5 min |

Console before each reset (`serial-20261111.log` lines 702-714, 1401-1422): the E906 boots
`abstractx.elf` and creates its channels, then `U-Boot SPL` follows. **No soft/hard lockup, RCU stall,
hung task, oops, panic or shutdown message.**

Diagnostics for run 3 (runtime only, no kernel change; reverted afterwards): `hb.sh` at SCHED_FIFO 99
printing to `/dev/kmsg` every ~2 s (uptime, `watchdogd` schedstat, the four `WDT_MODE`/`WDT_IRQ_STAT`,
runnable RT tasks); printk thread `pr/legacy` raised to FIFO 98 (on PREEMPT_RT the legacy console is
printed from this SCHED_OTHER kthread, so RT starvation could otherwise silence the console);
`watchdog_thresh=4` (soft lockup at 8 s); `rcu_cpu_stall_timeout=5`. Result: heartbeats on time up to
the last one (`hbrt 16 up=377.96`, E906 started at 369.30), all watchdogs still MODE 0 / IRQ_STAT 0,
no RT task hogging a CPU (only `ktimers`/`rcuc` at FIFO 1 runnable), then the reset within ≤ 2 s.

**Conclusion:** the reset is abrupt, not a scheduler starvation (the FIFO 99 task and the console
kept running) and not an armed SoC watchdog. **No starving task exists to identify.** It happened 2/2
at 792 MHz and 0/3 with the CPUs capped at 408 MHz (same 0.90 V; small sample, the fault is
intermittent, the old kernel at 768 MHz reset too). That points at power (supply/PMIC under the
CPU + `stress-ng --vm` + E906 load) or a voltage/timing margin issue, ahead of a kernel cause. Next:
read the AXP717 power-off/reset-reason registers after a reset (datasheet needed), measure the
5 V input and `vdd-cpul`/`vdd-sys` under load, repeat the 408 MHz cap more times, try 600 MHz, and
add pstore/ramoops.

## Board state at the end

Running `7.1.0-cpufreq #1` (booted fine; no regression found in boot, Wi-Fi, Ethernet, aic8800,
remoteproc, KUnit, cyclictest). `scaling_max_freq` 792000, `watchdog_thresh` 10, RCU stall 21 s,
`pr/legacy` SCHED_OTHER, heartbeat stopped, E906 running `riscv-firmware.elf`; `config.txt`
unchanged; `/root/hb.sh` left on the board. Board lock released.
