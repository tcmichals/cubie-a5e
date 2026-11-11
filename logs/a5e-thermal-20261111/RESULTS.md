# A5E thermal check, 2026-11-11 (4 cores, VPU off)

All times are PC time (CST). The board clock reads one hour behind the PC.
Access: serial console `/dev/ttyUSB0` (banner `cubie-a5e-flight`, root, empty password).
ssh to 192.168.3.3 did not answer ("No route to host"; ARP FAILED). Nothing was written
to the board other than mounting debugfs (devmem used for reads only). No reboot, no unbind, no clock changes.

Files: `temps.csv` (50 samples, 15 s apart), `ppu-pwsr.txt`, `extra-checks.txt` (clocks, regulators,
cpuidle, top), `serial-console.log` (raw console bytes), `a5e_serial.py`, `sample_temps.py`.

## Board state

| Item | Value |
|---|---|
| Kernel | `Linux cubie-a5e-flight 7.1.0 #2 SMP PREEMPT_RT Sat Nov 7 10:32:50 CST 2026 aarch64` |
| cmdline | `console=ttyS0,115200 earlycon root=/dev/mmcblk0p2 rootwait rw panic=10 loglevel=8` |
| CPUs | present 0-7, online 0-3, `nproc` = 4. cpu4-7 were already offline: dmesg at 5.88-5.92 s shows `psci: CPU4..7 killed`. No change made. |
| Uptime / load | up 16 min at the first sample, loadavg 0.00 0.00 0.00 throughout. `top`: 97 % idle, nothing above 2 % (only `top` itself). |
| VPU/VE | Off. `ve` clock enable count 0, `mbus-ve` 0, no cedrus/video-codec driver, no `/dev/video*` or `/dev/media*`. |
| GPU | `gpu` clock enable count 0 (432 MHz rate, gated). `vdd-gpu-sys` 0.92 V enabled. |
| NPU | `npu` clock enable count 0. |
| DRAM | `pll-ddr0` 2400 MHz, `dram` and `mbus` 600 MHz, on. |

## Sensor source

Real kernel sensors. The kernel has the sun8i-thermal A523 THS0/THS1 driver (same code as
`patches-a523-ths-v5`), two `ths` IRQs (GIC 94, 71, ~3880 interrupts each), and SID
calibration cells that read nonzero (factory temperature decodes to 25.9 °C, per-sensor
offsets of 6-22 register counts; no "not calibrated" warning in dmesg).

| zone | type | trips |
|---|---|---|
| thermal_zone0 | cpu4-thermal | 70 °C passive, 90 °C passive, 110 °C critical (hyst 0) |
| thermal_zone1 | cpu0-thermal | 70 °C passive, 90 °C passive, 110 °C critical (hyst 0) |
| thermal_zone2 | gpu-thermal | none (dmesg: "No trip points found for thermal-sensor@2009400 id=2") |
| thermal_zone3 | ddr-thermal | 110 °C critical only |

Extra: hwmon0 = AXP717 PMIC die temperature (iio_hwmon, `axp717-adc` in_temp4).
All zones use the step_wise governor.

## Measurements (idle only)

The 4-core load test was **not run**: the board idled at 84.8-87.5 °C, already above the
85 °C abort limit. The coordinator then cancelled the load test and asked for 10 min of
idle sampling. The owner switched the fan **on at about 01:18:15**; the samples before
that are fan-off, the later ones fan-on.

| zone | idle, fan off (01:16-01:18:11), min / max / mean °C | idle, fan on, settled (01:22-01:29:24), min / max / mean °C |
|---|---|---|
| cpu4-thermal | 86.3 / 87.5 / 87.2 | 47.3 / 48.7 / 47.8 |
| cpu0-thermal | 86.4 / 87.5 / 87.0 | 46.7 / 48.1 / 47.2 |
| gpu-thermal | 84.8 / 85.7 / 85.3 | 46.0 / 46.7 / 46.4 |
| ddr-thermal | 85.6 / 87.1 / 86.6 | 46.3 / 47.3 / 46.8 |
| AXP717 PMIC | 53.6 / 54.6 / 53.8 | 61.8 / 62.1 / 62.0 |

After the fan came on, the SoC fell 87 -> 78 -> 71 -> 66 -> 62 °C in successive 15 s
samples and settled at ~47 °C within 4 minutes. The PMIC reading went *up* by 8 °C at the
same time (cause not investigated; the fan may draw from a nearby supply). Peak seen:
87.5 °C (cpu4 and cpu0, fan off, idle). The 95 °C stop limit was never reached.
No load peak and no cool-down-after-load numbers exist, because no load was run.

## Cooling and throttling

- **No cpufreq.** `/sys/devices/system/cpu/cpufreq` is empty and there is no `cpu*/cpufreq`.
- **Only one cooling device**: `cooling_device0` = `7122000.npu` (devfreq, state 0/6). The
  DT cooling maps of cpu0-thermal and cpu4-thermal name `&cpu0..`/`&cpu4..` as cooling
  devices, but those never register without cpufreq, so the 70 °C and 90 °C passive trips
  do nothing for the CPUs.
- **Critical trip at 110 °C** on the cpu and ddr zones: this triggers an orderly kernel
  shutdown, i.e. the board turns off in flight. gpu-thermal has no trips.
- **Fan is not under Linux control.** No pwm-fan/gpio-fan device, no `pwm*` in hwmon,
  `/sys/class/pwm` empty, no fan cooling device. The owner switched it on by hand.
- Not checked: hardware THS over-temperature reset (the THS shutdown registers were not read).

## cpufreq question (can cpufreq lower the idle temperature?)

Read-only checks:

1. **Clocks.** `clk_summary` has no `pll-cpu`, `cpu`, `cluster`, `c0`/`c1` or `dsu` entry:
   the A523 CPU PLL / cluster clock controller is not described in this kernel (sunxi-ng
   has only `ccu-sun55i-a523.c`, `-r.c`, `-mcu.c`). The CPU rate is whatever U-Boot set;
   U-Boot `.config` has `CONFIG_SYS_CLK_FREQ=792000000` (792 MHz, config value, not a
   silicon readback). Other clocks: `gpu` 432 MHz enable 0, `ve` 432 MHz enable 0,
   `npu` 480 MHz enable 0, `pll-ddr0` 2400 MHz on, `dram` 600 MHz on, `mbus` 600 MHz on
   (enable 4).
2. **CPU rails.** `vdd-cpul` 0.90 V enabled (1 user), `vdd-cpub` 0.90 V enabled (1 user),
   `vdd-cpus` 0.90 V enabled, `vdd-dnr` 0.90 V, `vdd-gpu-sys` 0.92 V, `vdd-dram` 1.10 V,
   `dcdc2` 0.90 V / `dcdc4` 1.00 V enabled with 0 users. The DT has no OPP table and no
   `cpu-supply`, so nothing in Linux would change the CPU voltage.
3. **PSCI / cpuidle.** PSCI v1.1, SMCCC v1.5. `cpuidle` driver `none` (governor menu), no
   `cpu0/cpuidle`: idle CPUs only execute WFI, no PSCI CPU_SUSPEND / cluster retention state.
4. **Offline cores.** `psci: CPU4..CPU7 killed (polled 0 ms)` at 5.88-5.92 s: PSCI CPU_OFF
   succeeded. Per-core PPU power state, read with `devmem` (read only; address
   `0x07050000 + (core+1)*0x1000 + 0x1008` = PPU_PWSR, low 4 bits 8 = ON, 0 = OFF, from
   the A523 TF-A `sunxi_native_pm_a55.c`, jernejsk a523-v4, as given by the coordinator;
   raw output in `ppu-pwsr.txt`):

   | core | address | value | low 4 bits |
   |---|---|---|---|
   | cpu0-3 | 0x07052008 .. 0x07055008 | 0x00000108 | 8 = ON |
   | cpu4-7 | 0x07056008 .. 0x07059008 | 0x00000100 | 0 = OFF |

   So cpu4-7 are power-gated per core by their PPUs. The `vdd-cpub` rail itself stays
   enabled at 0.9 V (cluster/DSU-level gating not checked).
   The coordinator reports this BL31 has no PSCI CPU_SUSPEND (no `pwr_domain_suspend` /
   `validate_power_state`); confirmed on the board: `cpuidle/current_driver` = `none`,
   dmesg has only "cpuidle: using governor menu", no PSCI idle driver.

Answer: **not with this kernel**, and probably not much at idle. cpufreq cannot run (no CPU
clock driver, no OPP table, no cpu-supply). The CPUs already sit at 0.90 V and
(per U-Boot config) 792 MHz, with load 0 and WFI-only idle. The measured idle drop from the
fan (87 -> 47 °C) shows the board's problem is heat removal, not CPU frequency. cpu4-7 are
already power-gated (PPU OFF). What could still lower idle power is a PSCI CPU_SUSPEND
state in BL31 plus a cpuidle driver (absent), or gating the big-cluster rail. cpufreq would
matter for **load** (throttling above 70/90 °C), which is the missing safety mechanism.

## Verdict

**Not safe** for flight as configured, with 4 cores and the VPU off:

- Without the fan the board sits at **~87 °C at idle** (load 0). That is above the 70 °C
  and 85 °C marks with no flight work running; any load would add to it, and the only
  protection is the 110 °C critical trip, which shuts the board down.
- **No throttling**: no cpufreq, so the 70/90 °C passive trips cannot slow the CPUs.
- **Fan not controlled by Linux**: if the fan stops or is not switched on, nothing
  reacts until 110 °C.
- With the fan on, idle is ~47 °C. Whether that stays safe under a 4-core flight load is
  **unknown**: the load test was not run in either fan state.

Next measurement needed: the 4-core load run with the fan on (the owner's call), abort at
85 °C.
