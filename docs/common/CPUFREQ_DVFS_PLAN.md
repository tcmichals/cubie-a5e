# CPU frequency / DVFS plan: Cubie A5E (A527, sun55i) and Cubie A7A (A733, sun60i)

Research note, 2026-11-11. Goal: thermal throttling that does not need a fan on two flight
controllers. Measured starting point (A5E, `logs/a5e-thermal-20261111/RESULTS.md`, TODO.md:133):
fanless idle 86.3-87.5 °C with 4 cores online, ~47 °C with the fan; no cpufreq, no cpuidle,
only the 110 °C critical trip acts.

Rules followed: AGENTS.md, upstream-remoteproc/DESIGN_DECISIONS.md (no edits in `bld.*`,
fixes on `cubie-linux-7.1` first, no list traffic without the owner). Branch decision already
in TODO.md:141 (owner, 2026-11-11): A5E work goes on `linux-v4-71` `cubie-linux-7.1` as separate
commits on top of v4; OPP tables and idle states in a new `sun55i-a523-cpu-opp.dtsi` included
from the board DTS; the CPU-CCU node in `sun55i-a523.dtsi` only after v4 is sent. A7A work on
`linux-cubie` `a7a_remoteproc_camera_usb` (AGENTS.md).

Source abbreviations used below:

| Tag | Path |
|---|---|
| `AW2501` | `~/projects/cubie/old-kernels/linux-aw2501` (T527/A523 BSP, Linux 5.15) |
| `AW733` | `~/projects/cubie/old-kernels/linux-a733` (A733 BSP, Linux 5.15) |
| `V71` | `~/projects/cubie/linux-v4-71` (`cubie-linux-7.1`, A5E) |
| `LC` | `~/projects/cubie/linux-cubie` (`a7a_remoteproc_camera_usb`, A7A) |
| `A7A-DTB` | `~/projects/cubie/vendor-a733-reference/dtbs/sun60i-a733-cubie-a7a.dts` (decompiled Radxa DTB; the decompiler printed some `opp-microvolt` cells as strings, values below were decoded from the `.dtb`) |
| `UB` | `~/projects/cubie/bld.a5e/build/uboot-2026.01` (read only) |
| `TFA-A5E` | `~/projects/cubie/bld.a5e/build/arm-trusted-firmware-a523-v4` (jernejsk `a523-v4`, what the A5E boots; read only) |
| `TFA-733` | `~/projects/cubie/tf-a-dlan17-A733` (dlan17 `A733`, base of the owner's fork) |
| `A733-UM` | `~/projects/cubie/tina-source/manuals-text/A733_User_Manual_V1.00.txt` |
| `T527-UM` | `~/projects/cubie/tina-source/manuals-text/T527_User_Manual_V0.94.txt` |
| `A7A-SCH` | `~/projects/cubie/vendor-a733-reference/radxa_cubie_a7a_v1.10_schematic.txt` |

---

## 1. CPU clocks

### A5E (A523/A527/T527)

- Separate **CPU_PLL_CFG block at `0x08817000`** (T527-UM:1842, 2225; `CPUA_CLK_REG` `0x0060`
  T527-UM:2348, 4090). Not in the main CCU, not behind an SCP/ARISC, not SCMI.
- Layout (vendor driver `AW2501 bsp/drivers/clk/sunxi-ng/ccu-sun55iw3-displl.c`):
  `PLL_CPU0..3` at `+0x00/+0x04/+0x08/+0x0c` (:24-27), SSC regs `+0x54..+0x5c` (:29-31),
  update bit 26, lock bit 28 (:53-55). `pll-cpu1` = little cluster (cpu0-3), `pll-cpu3` = big
  cluster (cpu4-7), both NKMP with N 20-108 and the P divider in `CPUA` `+0x60` / `CPUB` `+0x64`
  bits 16-17 (:45-97); `pll-cpu2` = DSU (:63-79). Muxes `cpua`/`cpub` bits 24-26 (:103-111),
  DSU mux `+0x6c` (:101). Vendor DT: `cpupll_ccu: clock@8817000`, `allwinner,sun55iw3-cpupll`
  (`AW2501 bsp/configs/linux-5.15/sun55iw3p1.dtsi:1416`); cpu0-3 `clocks = <&cpupll_ccu
  CLK_PLL_CPU1>` (:157), cpu4-7 `CLK_PLL_CPU3` (:206), DSU `CLK_PLL_CPU2` (:776).
- U-Boot SPL programs the same registers: `clock_a523_set_cpu_plls()` writes CPUA `+0x04`,
  DSU `+0x08`, CPUB `+0x0c` to one N (`UB arch/arm/mach-sunxi/clock_sun50i_h6.c:143-177`,
  offsets `UB arch/arm/include/asm/arch-sunxi/clock_sun50i_h6.h:35-41`, base
  `cpu_sunxi_ncat2.h:51`), with `CONFIG_SYS_CLK_FREQ=792000000` (`UB .config:287`, default
  `UB arch/arm/mach-sunxi/Kconfig:853`). So all three PLLs run at 792 MHz (config value, not
  read back; RESULTS.md says the same).
- **Mainline: no CPU clock driver** in `V71` or `LC` (`drivers/clk/sunxi-ng/` has only
  `ccu-sun55i-a523{,-r,-mcu}.c`), and none in linux-next `next-20261105`
  (`~/projects/cubie/linux-v4-next`, checked: no `ccu-sun55i-a523-cpu.c`).
- **Posted upstream:** Andre Przywara, "arm64: allwinner: a523: Enable CPU clocks".
  v1 2025-09 (5 patches): cover <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058409.html>,
  binding <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058540.html>,
  driver <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058413.html>,
  DT <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058402.html>
  (`cpu_ccu: clock-controller@8817000`, `allwinner,sun55i-a523-cpu-ccu`, `reg = <0x08817000 0x80>`,
  parents hosc/losc/iosc/pll-periph0-2x/pll-periph0-600M, cpu0-3 `<&cpu_ccu CLK_CPU_L>`,
  cpu4-7 `<&cpu_ccu CLK_CPU_B>`).
  **v3 2026-09-26** (4 patches, base v7.3-rc1, new `ccu-sun55i-a523-cpu.c` 378 lines,
  `include/dt-bindings/clock/sun55i-a523-cpu-ccu.h`): <https://lwn.net/Articles/1096895/>.
  Cover letter: "lifting all A523 boards out of their current 768MHz misery". No cpufreq,
  OPP or regulator part in that series. Merge status after v3: not in next-20261105; lore was
  not reachable from here (HTTP 403), so later status not checked.
- Related: "clk: sunxi-ng: fix the A523/T527 GPU clock model, enable GPU DVFS" (2026-07)
  <https://lkml.iu.edu/2607.2/06110.html>.

### A7A (A733)

- Separate **CPU_PLL_CFG block at `0x08870000`** (A733-UM:1497, 64 KB). Four PLLs:
  `CPU_BACK_PLL`, `CPU_L_PLL` (cpu0-5), `CPU_B_PLL` (cpu6-7), `CPU_DSU_PLL`
  (A733-UM:14446-14457; Fvco 650 MHz-2.26 GHz, reference **26 MHz** DCXO, A733-UM:2070-2090).
  Register map A733-UM:14917, 14937 (`CPU_L_PLL_CTRL_REG` `0x1000`, `CPU_B_PLL_CTRL_REG` `0x2000`),
  programming sequence A733-UM:14578-14611.
- Vendor driver `AW733 bsp/drivers/clk/sunxi-ng/ccu-sun60iw2-cpupll.c`: regs `0x0000/0x1000/
  0x2000/0x3000` (:22-25), clock regs `0x101c/0x201c/0x301c` with mux bits 24-26 and P bits 16-17
  (:41-43, :64-84, :128-141), N range re-computed for a 24 or 26 MHz DCXO (:230-245); probe
  forces mux = PLL (:306-308). Vendor DT `cpupll_ccu@8870000`, `allwinner,sun60iw2-cpupll`
  (`AW733 bsp/configs/linux-5.15/sun60iw2p1.dtsi:908`, A7A-DTB:3114); cpu0-5 `clocks =
  <&cpupll_ccu 1>`, cpu6-7 `<... 2>` (A7A-DTB:2287, 2400).
- **The CPU PLLs use the DCXO directly, and the A7A DCXO is 26 MHz** ("DCXO-SYS 26MHz",
  A7A-SCH:1007; manual "backup clock source is 26MHz", A733-UM:14450-14457). The `LC` board DT
  only has `osc24M` "hosc" (`LC .../sun60i-a733-cubie-a7a.dts:226-232`), which is
  `SYS_CLK24M` (A733-UM:2219), not the DCXO. A CPU CCU binding needs the DCXO as parent. The
  vendor driver picks the `26m-vf…` OPP columns when the DCXO is 26 MHz
  (`AW733 bsp/drivers/cpufreq/sun50i-cpufreq-nvmem.c:667-690`). Confirm on silicon (step A7A-0).
- **Mainline / posted:** not found. `LC` has `ccu-sun60i-a733{,-r}.c` only. Junhui Liu's A733
  CCU series (RFC 2026-03 <https://lists.infradead.org/pipermail/linux-riscv/2026-March/086818.html>,
  v2 PLL patch <https://lkml.iu.edu/2607.1/08093.html>) covers the main CCU/PRCM; no CPU CCU,
  no cpufreq patch found.
- Boot frequency/voltage of the A7A (Radxa boot0 + vendor BL31 + U-Boot): not found in any local
  source. Must be read on the board (step A7A-0).

## 2. Cluster supplies

| Board | Cluster | Rail | PMIC output | Mainline DT | Evidence |
|---|---|---|---|---|---|
| A5E | little cpu0-3 | `vdd-cpul` | AXP717 `dcdc1`, 0.90-1.16 V | yes, `V71 sun55i-a527-cubie-a5e.dts:185-190`, no `cpu-supply` | measured 0.90 V (RESULTS.md) |
| A5E | big cpu4-7 | `vdd-cpub` | AXP323 `dcdc1`, 0.90-1.16 V | yes, same file :319-324, no `cpu-supply` | measured 0.90 V |
| A5E | DSU | not found (no A5E schematic or vendor A5E board DTS locally). Vendor `dsufreq` takes its frequency from the little policy's voltage (`AW2501 bsp/drivers/dsufreq/sunxi-dsufreq.c:171-195`), which points to `vdd-cpul`; inference, not proven. | | | |
| A7A | little cpu0-5 + DSU | `VDD-CPUL` | AXP8191 `dcdc5` (reg `0x16`) | PMIC node only, `LC .../sun60i-a733-cubie-a7a.dts:734-739`; **no regulators** | A7A-SCH:177; vendor `cpu-supply` A7A-DTB:2291, `dsu-supply` :2687, rail :4952 |
| A7A | big cpu6-7 | `VDD-CPUB` "0.8-1V" | AXP8191 `dcdc3` (reg `0x14`) | no | A7A-SCH:147; vendor `cpu-supply` A7A-DTB:2404, rail :4930 |

AXP8191 in `LC`: MFD only, `nr_cells = 0` (`LC drivers/mfd/axp20x.c:1398-1401`), no regulator
driver. Vendor regulator ranges: DCDC2-5 0.50-1.22 V in 10 mV steps (`0x00-0x46`) then 20 mV to
1.54 V, enable bits in `0x10` (`AW733 bsp/drivers/power/regulator/axp2101-regulator.c:1244-1310`,
regs `bsp/drivers/power/include/axp2101.h:978-985`).

## 3. Vendor OPP tables and speed bin

### A5E: tables (`AW2501 bsp/configs/linux-5.15/sun55iw3p1.dtsi`)

Compatible `allwinner,sun50i-operating-points`, `opp-shared`, `clock-latency-ns = <244144>`,
voltage per bin in `opp-microvolt-vfXXXX` (mV below; 0 = OPP not allowed for that bin).

cluster0 (cpu0-3), :347

| MHz | vf0000 | vf0100 | vf0102 | vf0200 | vf0201 | vf0300 | vf0301 | vf0302 | vf0400 | vf0500 | vf0502 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 408 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 |
| 672 | 0 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 |
| 720 | 900 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 792 | 0 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 |
| 936 | 900 | 920 | 920 | 920 | 920 | 900 | 900 | 900 | 900 | 900 | 900 |
| 1008 | 0 | 960 | 960 | 960 | 960 | 920 | 920 | 920 | 0 | 920 | 920 |
| 1032 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 920 | 0 | 0 |
| 1104 | 0 | 1000 | 0 | 1000 | 1000 | 960 | 960 | 0 | 0 | 960 | 0 |
| 1128 | 1000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 960 | 0 | 0 |
| 1224 | 1050 | 1050 | 0 | 1050 | 1050 | 1000 | 1000 | 0 | 1000 | 1000 | 0 |
| 1296 | 1100 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1320 | 0 | 1120 | 0 | 1120 | 1120 | 1050 | 1050 | 0 | 1050 | 1050 | 0 |
| 1416 | 1150 | 1150 | 0 | 1150 | 1150 | 1100 | 1100 | 0 | 1100 | 1100 | 0 |

cluster1 (cpu4-7), :560

| MHz | vf0000 | vf0100 | vf0102 | vf0200 | vf0201 | vf0300 | vf0301 | vf0302 | vf0400 | vf0500 | vf0502 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 408 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 |
| 672 | 0 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 |
| 720 | 900 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 840 | 0 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 |
| 1008 | 0 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 | 900 |
| 1200 | 0 | 920 | 920 | 920 | 920 | 920 | 920 | 920 | 920 | 920 | 920 |
| 1248 | 900 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1344 | 0 | 960 | 960 | 960 | 960 | 960 | 960 | 960 | 960 | 960 | 960 |
| 1488 | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 |
| 1584 | 1050 | 1050 | 1050 | 1050 | 1050 | 1050 | 1050 | 1050 | 1050 | 1050 | 1050 |
| 1680 | 1100 | 1100 | 1100 | 1100 | 1100 | 1100 | 1100 | 1100 | 1100 | 0 | 0 |
| 1800 | 1150 | 1150 | 1150 | 1150 | 1150 | 1150 | 1150 | 1150 | 1150 | 0 | 0 |
| 1992 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1220 | 0 | 0 |

DSU (`dsu-opp-table`, :780): 288-1152 MHz, 900-1150 mV; e.g. **792 MHz needs 920 mV on
vf0100/0102/0200/0201 and 900 mV on the others; 696 MHz is 900 mV on every bin**.

Other vendor CPU-node data: `capacity-dmips-mhz` 922 / 1024, `dynamic-power-coefficient`
286 (cpu0-3) / 354 (cpu4-7) (:156-160, :205-209); thermal zones 70/90 passive, 110 critical,
`sustainable-power` 1200/1600, cooling map on the 90 °C trip (:994-1060).

### A5E: speed bin (`AW2501 bsp/drivers/cpufreq/sun50i-cpufreq-nvmem.c:530-617`)

1. Read the 32-bit word at SID offset `0x48` (`SUN55IW3_DVFS_EFUSE_OFF`, :530). The helper adds
   `SUNXI_EFUSE_RAM_OFFSET 0x200` (`AW2501 bsp/include/sunxi-sid.h:153`), so the absolute
   address is **`0x03006248`**. In mainline terms (`sunxi_sid` A64 layout, `value_offset = 0x200`,
   `V71 drivers/nvmem/sunxi_sid.c:198-201`) this is an nvmem cell at **`0x48`**, 4 bytes, in
   the existing `sid: efuse@3006000` node (`V71 sun55i-a523.dtsi:628`).
2. `combi = bits[19:12]` if non-zero ("bak"), else `bits[11:4]` (:597-601).
3. `combi` → index through `vf_mapping_table` (`sun55iw3p1.dtsi:303-318`, "V0.71"):
   `0x00→vf0000 0x01→0100 0x21→0102 0x02→0200 0x12→0201 0x04→0300 0x14→0301 0x24→0302
   0x05→0400 0x06→0500 0x26→0502`; no match → `vf0100` default (:594).
4. `combi == 0` (unfused) → `match_vf_table_ext()` (:550-589): reads SID `0x20` (p_cpu4),
   `0x28` (p_ve, p_gpu), `0x38` (icpu); `p_min` in [2450,3050) → `vf0100`, [3050,4000) → `vf0201`,
   else `vf0100` (:544-545).

### A7A: tables (`AW733 bsp/configs/linux-5.15/sun60iw2p1-cpu-vf.dtsi`; same values in A7A-DTB:17, :621, :1363)

`vf_mapping_table` "V0.9" (:3-17): `0x00→vf0000 0x01→0100 0x11→0101 0x02→0200 0x12→0201
0x03→0300 0x04→0400 0x05→0500 0x06→0600 0x14→0401 0x15→0501`. Columns used with a 26 MHz
DCXO (`opp-microvolt-26m-vfXXXX`), mV:

cluster0 = little cpu0-5 (:20)

| MHz | vf0000 | vf0100 | vf0200 | vf0300 | vf0400 | vf0401 | vf0500 | vf0501 | vf0600 |
|---|---|---|---|---|---|---|---|---|---|
| 416 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 728 | 800 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 780 | 0 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 1014 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 1092 | 0 | 800 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1196 | 800 | 850 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 1300 | 0 | 900 | 800 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1404 | 800 | 950 | 850 | 860 | 840 | 800 | 800 | 800 | 840 |
| 1508 | 0 | 1000 | 900 | 0 | 880 | 820 | 820 | 820 | 0 |
| 1586 | 900 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1612 | 0 | 1050 | 950 | 0 | 920 | 860 | 860 | 840 | 0 |
| 1716 | 0 | 0 | 1000 | 0 | 960 | 900 | 900 | 860 | 0 |
| 1794 | 1000 | 0 | 1050 | 0 | 1000 | 950 | 950 | 900 | 0 |

cluster1 = big cpu6-7 (:623)

| MHz | vf0000 | vf0100 | vf0200 | vf0300 | vf0400 | vf0401 | vf0500 | vf0501 | vf0600 |
|---|---|---|---|---|---|---|---|---|---|
| 416 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 728 | 800 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 780 | 0 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 1014 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 1196 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 | 800 |
| 1300 | 0 | 800 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1404 | 800 | 850 | 800 | 820 | 800 | 800 | 800 | 800 | 800 |
| 1508 | 800 | 900 | 800 | 860 | 820 | 800 | 800 | 800 | 800 |
| 1586 | 850 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1612 | 0 | 950 | 850 | 900 | 840 | 820 | 820 | 820 | 850 |
| 1690 | 900 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1716 | 0 | 1000 | 900 | 0 | 870 | 850 | 850 | 840 | 0 |
| 1794 | 950 | 1050 | 950 | 0 | 920 | 900 | 900 | 870 | 0 |
| 1898 | 1000 | 0 | 1000 | 0 | 970 | 940 | 940 | 900 | 0 |
| 1950 | 0 | 0 | 0 | 0 | 1000 | 970 | 970 | 930 | 0 |
| 1976 | 1050 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2002 | 0 | 0 | 1050 | 0 | 1050 | 1000 | 1000 | 960 | 0 |

The 24 MHz columns (`opp-microvolt-vfXXXX`, 408/792/1008/1200/... MHz, up to 1800 little /
1992 big) are in the same file; they apply only if the DCXO is 24 MHz. DSU table (:1365): with
26 MHz, **780 MHz is 800 mV on every bin**; 910 MHz needs 850 mV on vf0100.
Vendor CPU-node data (A7A-DTB:2280-2432): `capacity-dmips-mhz` 430 / 1024,
`dynamic-power-coefficient` 162 (A55) / 598 (A76); zones 70/90 passive + 110 critical,
`sustainable-power` 756 (little) / 914 (big) (A7A-DTB:2694-2790).

### A7A: speed bin

`sun60iw2_nvmem_xlate()` (`AW733 bsp/drivers/cpufreq/sun50i-cpufreq-nvmem.c:667-690`) →
`sunxi_get_soc_dvfs()` (`AW733 bsp/drivers/sid/sunxi-sid.c:604-626`): SID cells `dvfs_ori`
offset `0x24C` bits [15:8] and `dvfs_bak` offset `0x24C` bits [23:16]; bak wins when non-zero
(`AW733 bsp/configs/linux-5.15/sun60iw2p1.dtsi:1438-1447`). Absolute address **`0x0300624C`**;
mainline nvmem cell **`0x4c`** in `LC`'s `sid: efuse@3006000` (`LC .../sun60i-a733-cubie-a7a.dts:491`,
which already has `thermal-sensor-calibration@44`). Value → index via the mapping table, default
`vf0000` (:670), then `26m-` prefix if the DCXO is 26 MHz (:680-685).

## 4. What is missing for mainline cpufreq

`cpufreq-dt` is built in and `sun50i-cpufreq-nvmem` is a module on both boards (TODO.md:142).
`cpufreq-dt-platdev` creates `cpufreq-dt` by itself for any machine whose cpu0 has
`operating-points-v2` and that is not on the blocklist (`V71 drivers/cpufreq/cpufreq-dt-platdev.c:227-245`);
`allwinner,sun55i-a523` and `allwinner,sun60i-a733` are not on it (:100-113). The nvmem driver
reads one cell from cpu0's OPP node, turns it into `speedN` / `opp-supported-hw`, and applies it
to every CPU (`V71 drivers/cpufreq/sun50i-cpufreq-nvmem.c:186-285`); its SoC list is
H6/A100/H616/H618/H700 only (:320-327, OPP compatibles :139-151).

| Item | A5E | A7A |
|---|---|---|
| CPU clock driver | missing; v3 posted (Przywara, §1) | missing; nothing posted; port from `ccu-sun60iw2-cpupll.c` |
| CPU CCU node + `clocks` in cpu nodes | missing (`V71 sun55i-a523.dtsi:25-87` has none) | missing (`LC ...a7a.dts:153-207`) |
| DCXO 26 MHz clock for the CPU CCU | n/a (24 MHz) | missing (`LC` has only `hosc` 24 MHz) |
| `operating-points-v2` tables | missing | missing |
| `cpu-supply` | missing; rails exist (§2) | missing; **AXP8191 regulator driver missing** |
| `#cooling-cells` | present (`V71 sun55i-a523.dtsi:30-86`, from THS v5) | missing |
| `dynamic-power-coefficient` | missing (vendor 286/354) | missing (vendor 162/598) |
| speed-bin nvmem cell | missing (cell `0x48`) | missing (cell `0x4c`) |
| `sun50i-cpufreq-nvmem` support | xlate + `allwinner,sun55i-a523` match + OPP compatible + platdev blocklist entry | same for `sun60i-a733` |
| DSU clock | stays at boot rate; no mainline DSU-freq driver. Must be kept at a rate valid at the lowest little-rail voltage | same |
| cpu-map / topology | 8 CPUs, no `cpu-map` | `cpu-map` present (`LC ...a7a.dts:138-150`) |

Side finding, A7A: `LC ...a7a.dts:196,203` name cpu6/7 `arm,cortex-a78`; the datasheet says
Cortex-A76 (`vendor-a733-reference/A733_Datasheet_V0.93.txt:8, 924, 1027`) and so does the
vendor DT (A7A-DTB:2395). Fix before upstreaming the A7A DT.

**DSU constraint (both SoCs).** When `vdd-cpul` is lowered for the little cluster, the DSU on the
same rail keeps its boot frequency. A5E: DSU at U-Boot's 792 MHz needs 920 mV on bins
vf01xx/vf02xx, but the little-cluster floor is 900 mV. A7A: the little floor is 800 mV; the DSU is
safe at ≤ 780 MHz (26 MHz columns). Until a DSU-scaling solution exists, either fix the DSU at
696 MHz (A5E) / 780 MHz (A7A) through `assigned-clock-rates` on the CPU CCU, or raise each
little OPP voltage to at least the DSU's need. Note: on an A5E of bin vf01xx/vf02xx the DSU runs
today at 792 MHz on 0.90 V, below the vendor table (read the bin, step A5E-0).

## 5. cpuidle / PSCI

### A5E (TFA-A5E, jernejsk `a523-v4`, booted per `cubie-a5e/project-cubie-a5e/configs/cubie_a5e_defconfig:83-88`)

- Topology: one cluster of 8 cores (`TFA-A5E plat/allwinner/sun55i_a523/platform.mk`
  "There are 8 cores in 1 cluster"). The two "clusters" are two clock/voltage domains in one
  DSU cluster, so a cluster power state needs all eight cores down.
- Native PSCI only (`SUNXI_PSCI_USE_SCPI := 0`, `common/allwinner-common-a55.mk:18`).
  `plat_psci_ops` = `pwr_domain_on/off/on_finish`, `system_off/reset` (`common/sunxi_native_pm_a55.c:106-113`):
  **no `pwr_domain_suspend`, no `validate_power_state` → no CPU_SUSPEND, no idle states**;
  `sunxi_idle_states[]` is empty (`sun55i_a523/sunxi_idle_states.c`).
- `CPU_OFF` clears `HOTPLUG_POWERMODE` `POWER_ON` and the PPU powers the core down
  (`sunxi_native_pm_a55.c:36-43`). Board: cpu4-7 `PPU_PWSR` = `0x100` (OFF), cpu0-3 `0x108`
  (RESULTS.md §4). The `vdd-cpub` rail stays on at 0.90 V; nothing powers it off. Turning that rail
  off with cpu4-7 offline was not investigated (what else it feeds: not found).
- Board: PSCI 1.1, `cpuidle` driver `none` (RESULTS.md).
- Vendor idle states for later (`AW2501 sun55iw3p1.dtsi:280-300`): `CPU_SLEEP_0`
  `arm,psci-suspend-param = <0x0010000>`, 46/59/3570 µs; `CLUSTER_SLEEP_0` `<0x1010000>`,
  47/74/5000 µs. These need a TF-A change first (TODO.md:135).

### A7A

- Running BL31: the Radxa vendor BL31 (TF-A v2.5 `177ce84c2`, no source, binary-patched;
  DESIGN_DECISIONS.md "A7A BL31"). Its DT advertises PSCI 1.0 and the same two idle states
  (A7A-DTB:2477-2504, params `0x10000` / `0x1010000`), so the vendor BL31 probably implements
  CPU_SUSPEND; **not verified** (test A7A-0). `LC`'s DT has no idle states (`LC ...a7a.dts:209-213`).
- Source BL31 (TFA-733, base of `tcmichals/trusted-firmware-a` `A733`): one cluster of 8
  (`sun60i_a733/platform.mk`), native ops `pwr_domain_on/off/on_finish` only
  (`common/sunxi_native_pm.c:75-82`), per-core `CPU_OFF` via the PPU (`common/sunxi_cpu_new_ops.c:36-47`
  in-file numbering; functions `sunxi_cpu_off`/`sunxi_cpu_on`), `sunxi_idle_states[]` empty.
  SCPI ops with suspend exist (`common/sunxi_scpi_pm.c:196-208`) but need an SCP. **Moving the A7A
  to this BL31 loses whatever idle states the vendor BL31 has** unless a native suspend is added.

## 6. Thermal: cooling maps

The cpufreq cooling devices register only once cpufreq-dt runs with `#cooling-cells` on the CPU
nodes (RESULTS.md: today the only cooling device is `7122000.npu`).

### A5E (`V71 sun55i-a523.dtsi:1180-1260`, THS v5 by Mikhail Kalashnikov, `patches-a523-ths-v5/5.patch`)

Already present: `cpu0-thermal` (THS1 sensor 1) and `cpu4-thermal` (THS1 sensor 0), trips
70 passive / 90 passive / 110 critical, `sustainable-power` 1200/1600, `map0` on the 90 °C trip
with `<&cpuN THERMAL_NO_LIMIT THERMAL_NO_LIMIT>` for cpu0-3 / cpu4-7 (:1208-1215, :1245-1252).
Governor on the board: `step_wise` (RESULTS.md). With `step_wise` and a map only on the 90 °C trip,
throttling starts at 90 °C, so the zone hovers around 90 °C, not below it. Board-level additions
(in `sun55i-a523-cpu-opp.dtsi` or the board DTS, not in the THS patch):

```dts
&cpu0_thermal { cooling-maps { map1 { trip = <&cpu0_threshold>;          /* 70 °C */
	cooling-device = <&cpu0 THERMAL_NO_LIMIT THERMAL_NO_LIMIT>, <&cpu1 ...>, <&cpu2 ...>, <&cpu3 ...>; }; }; };
&cpu4_thermal { cooling-maps { map1 { trip = <&cpu4_threshold>;
	cooling-device = <&cpu4 THERMAL_NO_LIMIT THERMAL_NO_LIMIT>, <&cpu5 ...>, <&cpu6 ...>, <&cpu7 ...>; }; }; };
```

Cross-map both clusters into both zones (one die, sensors 0.1-0.2 °C apart in RESULTS.md) so a
hot cpu0 zone also slows the big cluster. Alternative: `power_allocator` (trips 70 = switch-on,
90 = control temperature, already shaped that way) with `dynamic-power-coefficient` 286/354.

### A7A (`LC ...a7a.dts:255-345`)

Today: `cpu-big-thermal` (ths 0), `cpu-little-thermal` (ths 3), `gpu-thermal` with a `hot` 90 °C
and a `critical` 110 °C trip; `hot` only notifies, it never throttles. Add a `passive` 70 °C and
`passive` 90 °C trip per CPU zone (vendor A7A-DTB:2694-2790 has 70/90/110) and maps: cpu0-5 →
little zone, cpu6-7 → big zone, on both passive trips, plus `#cooling-cells = <2>` on cpu0-7.
Optionally `sustainable-power` 756/914 for `power_allocator`.

## 7. Implementation plan

Principle: **frequency-only throttling first.** cpufreq-dt works without `cpu-supply`; with no
regulator it only changes the clock. Any frequency at or below one the vendor validated at the
present rail voltage is safe. That gives a working thermal throttle without the regulator, bin and
DSU questions, then voltage scaling is added on top.

### A5E (A527)

| Step | Work | Where | Board test |
|---|---|---|---|
| A5E-0 | Read only: SID `0x03006248` (bin, §3), CPU PLL regs `0x08817004/08/0c` and `0x08817060/64/6c` (rates, P, muxes). Decode bin → `vfXXXX`. | board (owner / board owner session) | record in TODO |
| A5E-1 | Backport Przywara's v3 CPU CCU series (4 patches, §1) onto `cubie-linux-7.1` as its own commits; do not write a second driver. Node in `sun55i-a523.dtsi` waits for v4 to be sent (TODO.md:141); until then put the node in `sun55i-a523-cpu-opp.dtsi`. Check `clk_summary` shows `cpu-l`/`cpu-b`/DSU at 792 MHz and that the CCU does not reprogram the PLLs at probe. | V71 | boot, `clk_summary`, sweep |
| A5E-2 | `sun55i-a523-cpu-opp.dtsi` (pattern `sun50i-h616-cpu-opp.dtsi`), included from `sun55i-a527-cubie-a5e.dts`: plain `operating-points-v2` (no nvmem yet), **no `cpu-supply`**, two tables, OPPs 408/600/792 MHz with `opp-microvolt = <900000>` (792 MHz is 900 mV or better on every bin in both clusters, §3). cpufreq-dt binds through platdev. Add `dynamic-power-coefficient`. | V71 | `ls /sys/devices/system/cpu/cpufreq/` → policy0, policy4; `scaling_available_frequencies`; `cooling_device*` type `cpufreq-cpu0`/`-cpu4` |
| A5E-3 | Cooling maps on the 70 °C trip, both clusters into both zones (§6). | V71 | see tests below |
| A5E-4 | Fanless tests (fan unplugged, VPU off): (a) idle 15 min, 4 cores; (b) 4-core load `stress-ng --cpu 4` or `yes ×4`, 20 min, sample `thermal_zone*/temp` every 5 s, abort at 100 °C; (c) flight-like load (flight stack + RPMsg). Pass: zone temperature stays < 90 °C under (b); `cpufreq/policy0/stats/time_in_state` shows 408 MHz residency and `trans_table` transitions; `cooling_device*/cur_state` > 0; no critical trip. Record that idle (87 °C at 792 MHz) is above the 70 °C trip, so the CPUs sit at 408 MHz on the ground; check the flight loop still meets its deadline at 408 MHz. | board | log in `logs/` |
| A5E-5 | Voltage scaling: `cpu-supply = <&reg_dcdc1>` (cpu0) and `<&reg_dcdc1_323>` (cpu4); full vendor OPPs for the bin read in A5E-0; DSU fixed at 696 MHz (`assigned-clocks` on the CPU CCU) so 900 mV is valid for it. Regulator max stays 1.16 V (`a5e.dts:188, 322`), which allows up to 1416 MHz little / 1800 MHz big at 1.15 V. | V71 | `regulator_summary` shows voltage following OPP; stress at top OPP; thermal tests again |
| A5E-6 | Speed bin in `sun50i-cpufreq-nvmem.c`: `sun55i_a523_efuse_xlate()` (combi from bits [19:12]/[11:4], vendor mapping, unfused fallback), OPP compatible `allwinner,sun55i-a523-operating-points`, SoC match, platdev blocklist entry, SID cell `@48` + `nvmem-cells` on the OPP tables; per-bin voltages as `opp-microvolt-speedN` (mainline style) and `opp-supported-hw` masks. Binding update in `Documentation/devicetree/bindings/opp/allwinner,sun50i-h6-operating-points.yaml`. | V71 | KUnit not needed; boot log bin, OPP list matches the bin |
| A5E-7 | cpuidle: native `pwr_domain_suspend` + `validate_power_state` in TF-A (core level first), then the vendor idle states in the DT (TODO.md:135). TF-A repo/branch: owner to decide. | TF-A | `cpu*/cpuidle/state*/usage` rising; fanless idle temperature before/after |
| A5E-8 | Fan as a cooling device (`pwm-fan` or `gpio-fan` on the active trip), separate item already in TODO.md:133. | V71 | |

Expectation to state honestly: the board idles at 87 °C fanless at 0.90 V and 792 MHz, the
lowest vendor voltage. Dropping to 408 MHz under load cuts dynamic power roughly in half, but
leakage at 0.90 V stays. Whether 4-core load can be held below 90 °C fanless is what A5E-4
measures; if not, A5E-7 (idle states) and airflow are the remaining levers.

### A7A (A733)

| Step | Work | Where | Board test |
|---|---|---|---|
| A7A-0 | Read only: DCXO rate (24 or 26 MHz, e.g. compare `CPU_L_PLL` N with the rate the vendor kernel reported, or the RTC DCXO register), `CPU_L/B/DSU_PLL_CTRL` `0x08871000/0x08872000/0x08873000` and clock regs `+0x1c`, AXP8191 `0x10/0x14/0x16` with `i2cget -f` (boot voltages of VDD-CPUB/VDD-CPUL), SID `0x0300624C` (bin), PSCI_FEATURES for CPU_SUSPEND (e.g. boot once with the vendor idle states in an overlay and check `cpuidle/current_driver` = `psci_idle`). | board | record in TODO |
| A7A-1 | AXP8191 regulator support in `axp20x-regulator.c` (DCDC1-9, ALDO/BLDO/CLDO/DLDO from the vendor ranges, §2) and MFD cell; DT regulators with names from the schematic (`vdd-cpub` dcdc3, `vdd-cpul` dcdc5, keep `regulator-always-on` on both). Needed for A7A-5 only, but also for power-off (TODO.md:93). | LC | `regulator_summary` matches A7A-0 readbacks; no rail change at boot |
| A7A-2 | New `ccu-sun60i-a733-cpu.c` modelled on Przywara's A523 CPU CCU (same family idea, different register map, §1), parent = DCXO (26 MHz fixed clock in the board DT, or from the RTC CCU). Probe must not reprogram running PLLs. Binding `allwinner,sun60i-a733-cpu-ccu`. | LC | `clk_summary` rates equal A7A-0 readbacks |
| A7A-3 | OPP tables (plain v2) with frequency-only OPPs at or below the boot frequency at the boot voltage (from A7A-0; e.g. 416/780/1014 MHz are 800 mV on every bin in both clusters, 26 MHz columns), `clocks`, `#cooling-cells`, `dynamic-power-coefficient` 162/598, A55/A76 compatible fix. | LC | policy0 (cpu0-5), policy6 (cpu6-7) present |
| A7A-4 | Thermal zones: passive 70/90 trips + maps (§6). Fanless tests as A5E-4, but 8 cores; pass < 90 °C with throttling seen in `stats/time_in_state`. The 2026-11-11 run reached 61.4 °C with the fan (TODO.md:132), so the fanless run is the real test. | LC + board | log in `logs/` |
| A7A-5 | Voltage scaling: `cpu-supply` dcdc5 (cpu0) / dcdc3 (cpu6), full vendor OPPs for the bin, DSU fixed at ≤ 780 MHz (26 MHz) so 800 mV is valid. | LC | as A5E-5 |
| A7A-6 | Speed bin: `sun60i_a733` xlate (bits [15:8] / bak [23:16] of cell `0x4c`, vendor mapping), compatibles, blocklist, binding; 26 vs 24 MHz table choice stays a DT matter (only one set of OPPs in the board DT). | LC | |
| A7A-7 | cpuidle: if A7A-0 shows CPU_SUSPEND in the vendor BL31, add the vendor idle states to the DT now; carry a native suspend into the owner's A733 TF-A fork before switching BL31 (§5). | LC / TF-A | `cpuidle` usage, idle temperature |

Order rationale: A7A-1 (regulators) and A7A-2 (CPU CCU) are independent; the thermal win comes
from A7A-2 + A7A-3 + A7A-4 without any regulator work.

## 8. Open items (not found / to measure)

- A5E DSU supply rail: not found (no A5E schematic or vendor A5E board DTS in the workspace).
- A5E speed bin of this board: read SID `0x03006248` (A5E-0).
- A7A boot CPU frequency and voltages: not found in local sources; read on the board.
- A7A DCXO 24 vs 26 MHz on silicon (schematic says 26 MHz).
- Whether the vendor A7A BL31 implements CPU_SUSPEND: not verified.
- Merge status of the A523 CPU CCU v3 after 2026-09-26: not in next-20261105; lore not reachable.
- Upstream A523/A733 cpufreq-nvmem or CPU OPP patches: not found.

## Sources (web)

- A523 CPU CCU v3 cover/diffstat: <https://lwn.net/Articles/1096895/>
- A523 CPU CCU v1: <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058409.html>,
  <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058540.html>,
  <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058413.html>,
  <https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1058402.html>
- A523 GPU clock/DVFS series: <https://lkml.iu.edu/2607.2/06110.html>
- A733 CCU series: <https://lists.infradead.org/pipermail/linux-riscv/2026-March/086818.html>,
  <https://lkml.iu.edu/2607.1/08093.html>
- H616 cpufreq-nvmem pattern (opp-supported-hw): <https://lists.infradead.org/pipermail/linux-arm-kernel/2024-March/912862.html>
