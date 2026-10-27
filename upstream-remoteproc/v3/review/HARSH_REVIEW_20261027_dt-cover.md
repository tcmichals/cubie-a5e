# v3 pre-submission review: bindings, DTS, cover letter, commit messages

Target: linux-cubie `v3-next-20261026` (3a05cbdb8156), base next-20261026 (f1ac44c32cba, verified as the tag).
Reviewer stance: DT maintainer (Krzysztof / Rob) and sunxi maintainer (Chen-Yu). Not read: v3/review/, TODO.md.

## What was run

- `dt_binding_check` on both schemas: clean.
- `dtbs_check W=1` on all four sun55i boards: no new warnings. The only warning, `/soc unit_address_vs_reg`, was there before the series.
- A throwaway test DTS with the remoteproc node enabled (`memory-region` vram+dram, `firmware-name`) passes validation. `allwinner,tx-done-irq` with a single interrupt is rejected, so the `if/then` works.
- `checkpatch.pl --strict`: 0 errors apart from the missing Signed-off-by. 4 x "added, moved or deleted file(s), does MAINTAINERS need updating?" on patches 1, 4, 6 and 7.
- Every DTS macro exists at the base: CLK/RST_BUS_MSGBOX, *_MCU_DSP_MSGBOX, *_R_MSGBOX, *_MCU_RISCV_{MSGBOX,CFG}, CLK_MCU_RISCV, *_MCU_PUBSRAM, RST_BUS_MCU_RISCV_{CORE,DEBUG}, PD_RISCV, PD_SRAM. CCU reset offsets: 0x71c, mcu 0x68/0x114/0x124/0x128, r 0x17c. These match the msgbox reference doc.
- Node order and unit addresses are correct: msgbox@3003000 sits between dma@3002000 and efuse@3006000, and remoteproc@7130000 comes after npu@7122000. No reg overlaps with existing nodes.
- KUnit case counts: 26 and 15, matching the cover letter. Drivers, tests and bindings are byte-identical to `cubie-linux-7.1`, and so are the DTS nodes. "Same driver and test files" holds.
- Not run: the W=1 build. There is no arm64 cross compiler on this machine, and the x86 COMPILE_TEST build fails in objtool (no libelf). The cover letter's W=1 claim was not checked here. The v2 lore link returns 403 to curl; its message-id matches the project's v2 records.
- The worktree was removed. linux-cubie is still on `cubie-linux-7.1` and clean.

## Findings

### High

**H1. `allwinner,tx-done-irq` is OS policy in DT, and the precedent is misdescribed** (patch 1 binding; cover letter "Changes in v3"; patch 1 commit message)
- Quoted: "Signal TX done with the write interrupt of the receiver's bank instead of polling" and "like ti,mbox-send-noirq does for OMAP".
- Why reviewers will object: the property selects a driver mode (IRQ or 1 ms poll) and describes no hardware. The binding text gives no hardware reason to ever pick polling.
- The cited precedent works the other way. In mainline, `ti,mbox-send-noirq` is documented as "Quirk flag … Should be used only on sub-mailboxes used to communicate with WkupM3 remote processor on AM33xx/AM43xx". It is:
  - per sub-mailbox,
  - tied to a specific remote's limitation,
  - and it turns the TX IRQ *off*, because OMAP's default is IRQ.
- Here the property is controller-wide, opt-in, and turns the IRQ *on*. Rob or Krzysztof will ask: "If the interrupts are in DT, why not just use them?"
- Fix:
  - Drop the property. Make all four interrupts required. Have the driver use the write IRQ whenever the lines are present, or choose with a module parameter.
  - If the property stays, the binding description must give the hardware or firmware reason (as noirq does for WkupM3), and the cover letter must describe the noirq precedent accurately.

**H2. The switchable SRAM A3_2 and the PRCM remap register skip the sunxi SRAM framework and the rproc `sram` convention** (patch 3 binding `reg` items 3 and 4; patch 5 DTS)
- Quoted: `<0x072c0000 0x40000>` "Switchable SRAM space 1" and `<0x07010364 0x4>` "SRAM remap control register".
- Sunxi already has this mechanism:
  - `drivers/soc/sunxi/sunxi_sram.c`: `mmio-sram` sections plus a mapping register owned by the SRAM controller or system-control node.
  - Consumers claim a section with `allwinner,sram = <&section N>` (for example a64, h6, h616 VE and DE2).
  - sun55i-a523.dtsi already has `syscon@3000000` ("allwinner,sun55i-a523-system-control") with `ranges`, ready for SRAM children.
- Remoteproc bindings use an `sram` phandle for shared on-chip SRAM (ti,k3-r5f, ti,k3-dsp, xlnx,zynqmp-r5fss, amlogic,meson-mx-ao-arc).
- meson-mx-ao-arc also reaches configuration bits in another block through a syscon phandle (`amlogic,secbus2`), not a `reg` entry.
- The vendor DT's `dsp0_rproc` maps the same 0x07010364. A later mainline HiFi4 node would then have to overlap this node's `reg`.
- The cover letter offers "a syscon or the R-CCU", but Chen-Yu is more likely to ask for `allwinner,sram`.
- Fix:
  - Describe A3_2 as a `mmio-sram` section and claim it with `allwinner,sram` (extending sunxi_sram for a PRCM-resident switch), or at least with `sram = <&…>`.
  - Reach 0x07010364 through a phandle to the block that owns it.
  - Keep the dedicated SRAM space 0 in `reg`, like K3 TCMs, if wanted.

### Medium

**M1. The compatible `allwinner,sun55i-a523-rproc` does not say which processor** (patch 3; driver; file name)
- The SoC has at least two Linux-managed remote processors: the HiFi4 DSP and the E906. The vendor BSP uses `allwinner,hifi4-rproc` and `allwinner,e906-rproc`.
- Mainline names the core: `fsl,imx8mp-cm7`, `st,stm32mp1-m4`, `ti,am64-r5f`, `mediatek,mt8183-scp`, `amlogic,meson8-ao-arc`.
- A compatible is ABI and is hard to rename later.
- Fix: use `allwinner,sun55i-a523-e906` (or `-riscv`) and rename the file to match.
- Lesson 1.4 only requires a single compatible, not this name, so this is not a settled decision.

**M2. The binding has a crash interrupt with no known hardware line** (patch 3, `interrupts`/`interrupt-names: crash`)
- The cover letter says "not wired in any DT and not tested; the vendor BSP has none". The project docs say the vendor "reports crashes in software only".
- A binding describes hardware. A reviewer will ask: "Which SoC interrupt is this?"
- Fix: name the GIC SPI (from manual Table 2-15) or drop it from the binding. The driver can keep its optional code path.

**M3. `mboxes` silently became optional** (patch 3)
- `mboxes` was in `required:` in v2 and is not in v3. The changelog only says "One message box channel; mbox-names dropped".
- The message-box link exists in hardware on every board. "Standalone" or UIO operation is software policy, which Krzysztof generally does not accept as a reason to make a hardware connection optional.
- Fix: at least record it in "Changes in v3" with the reason. Better, make `mboxes` required again.

**M4. Changelogs are inaccurate or incomplete** (cover letter and patches 1, 3, 5), checked against `origin/v2-sun55i-rproc-msgbox`:
- Patch 1 (msgbox binding):
  - The v2 example (and the v2 DTS) had the DSP and CPUS addresses swapped: `dsp` was 0x07094000 and `cpus` was 0x07120000. v3 fixes this without saying so.
  - Samuel Holland was dropped from `maintainers` without a note.
- Patch 3 (rproc binding):
  - Not mentioned: `mboxes` dropped from required (M3); the `msgbox`/`parent` clocks and the `msgbox` reset removed; Jernej and Samuel removed from `maintainers`.
  - "memory-region and memory-region-names are a positional list…" is listed as a change, but v2 already had the same positional `items` and `const` names. Only the descriptions were reworded.
- Patch 5 (DTS): not mentioned: the dsp/cpus `reg` swap fix, and the msgbox clock/reset removed from the remoteproc node.
- Cover letter:
  - "Bindings: fixed lists for reg, clocks, resets, interrupts and memory-region": memory-region was already fixed in v2.
  - "all resources are required" contradicts the now-optional mboxes, memory-region and crash interrupt.

**M5. The cover letter omits text that the project's own rules require, and a merge plan**
- DESIGN_DECISIONS D15 and Lesson 4.2 say the cover letter states that the KUnit patches can be dropped or taken separately without affecting the drivers. It does not.
- There is no note on which tree takes which patch, or on dependencies:
  - patch 5 needs patches 1 and 3 and goes through sunxi;
  - patches 1+2 go through mailbox and 3+4 through remoteproc.
- Chen-Yu asked in v1 for clarity on how the series is split.

### Low

**L1. "checkpatch --strict … clean" is not literally true** (cover letter)
- Patches 1, 4, 6 and 7 each get the "does MAINTAINERS need updating?" warning.
- Fix: state the benign warnings, or reorganise the MAINTAINERS hunks (see L2).

**L2. The rproc MAINTAINERS entry is in the binding patch (3)** and its `F: drivers/remoteproc/sunxi_rproc*` matches nothing until patch 4. The msgbox entry, by contrast, is in its driver patch (2).
- Fix: move the hunk to patch 4 for consistency. `get_maintainer.pl --self-test=patterns` would flag the pattern at patch 3.

**L3. Remap bit 1 (SRAM A3_2 to the MCU) cannot be traced to the vendor BSP**
- The vendor code only does a read-modify-write of bit 0 (`sunxi_rproc_hifi4_boot.c`, `BIT_SRAM_REMAP_ENABLE 0`). The vendor E906 driver and DT never map 0x07010364.
- The patches cite no source.
- Fix: cite the manual section, or say it was confirmed on silicon, in the patch 3 commit message and the cover letter.
- Related: the driver header comment "offset 0x364 in PRCM_R_CCU / MCU_CCU" is wrong. 0x07010364 is R-PRCM only; MCU_CCU is at 0x07102000.

**L4. The CPUS bank IRQ source is stated too cleanly** (patch 1 changelog: "SPI 181/174/205 (T527 manual, Table 2-15; checked on the board)")
- The project reference doc notes that the manual *also* lists SPI 1 as "CPUS MSGBOX write IRQ for CPUX". SPI 174 for the CPUS bank rests on the silicon measurement.
- Fix: say "measured" for the CPUS bank.

**L5. The DTS commit message is silent about clocks and resets taken from other processors' banks**
- The msgbox node is enabled by default on every A523/T527 board. It now claims the clocks and shared resets of the DSP bank and the CPUS (R-domain) bank, which belong to other processors' firmware, and drops them on unbind.
- This was the original D12 concern.
- Fix: one sentence in the commit message saying why this is safe (no CPUS or DSP firmware owner in mainline) would pre-empt the question.

**L6. Default firmware name is not namespaced**
- `riscv-firmware.elf` is both the driver default and the binding example, and has no vendor/SoC path.
- linux-firmware convention is a path such as `allwinner/sun55i-a523-e906.elf`. Remoteproc maintainers usually ask for this.

**L7. Testing does not mention a non-RT run**
- Only Linux 7.1 PREEMPT_RT testing is claimed. The project checklist requires one non-RT run with `CONFIG_DEBUG_ATOMIC_SLEEP` and `CONFIG_PROVE_LOCKING`, because RT hid an atomic-context bug before.
- Fix: if it was done, say so.

### Nit

- `reg-names` "r_sram"/"r_sram1": the underscore and the unexplained `r_` prefix stand out. "sram0"/"sram1" is clearer.
- `memory-region-names` "vram" reads as video RAM.
- The clock and reset for the configuration block are named "bus" and "cfg".
- The cover letter links v2 but not v1.
- "'schema' dropped from the subjects (Krzysztof)": in v1 Krzysztof asked to drop the redundant "binding"; he did not comment on "schema" in v2. The attribution is loose.

## Conflicts with owner decisions

- H1 conflicts with "Message box TX completion: the device tree selects timer or ISR" (the `allwinner,tx-done-irq` property and the ti,mbox-send-noirq precedent).
- H2 conflicts with D9 (remap register mapped directly in `reg`; move only if maintainers ask). The new point is the sunxi `allwinner,sram` framework and the rproc `sram` precedent.
- M2 conflicts with D14 (crash IRQ kept). The objection is to the binding exposing an unidentified line, not to the driver code.
- M3 conflicts with "Mailbox is optional; UIO mode uses its own base device tree".
