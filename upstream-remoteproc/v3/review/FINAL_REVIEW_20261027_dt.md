# DT / sunxi review: sun55i msgbox + E906 remoteproc (tree 91018e3b423e, v3 patches)

## What was run (all clean unless noted)
- `make dt_binding_check DT_SCHEMA_FILES=allwinner,sun55i-a523-msgbox.yaml:allwinner,sun55i-a523-rproc.yaml` (dtschema 2026.9): 0 warnings.
- `CHECK_DTBS=y` for sun55i-a527-cubie-a5e, sun55i-t527-avaota-a1, sun55i-t527-orangepi-4a, sun55i-h728-x96qpro+: 0 warnings. W=1 adds only the existing `/soc` unit_address_vs_reg warning.
- A test board DT (cubie-a5e + reserved-memory vram/dram/trace + `&rproc` okay + `allwinner,tx-done-irq`) passes dt-validate.
- W=1 compile (ARCH=um) of sun55i-msgbox.o, sun55i_msgbox_test.o, sunxi_rproc.o and sunxi_rproc_test.o: no warnings.
- Checked against the tree and found correct: node order by address (mailbox@3003000 between 3002000 and 3006000; remoteproc@7130000 after npu@7122000), property order and status placement, unit addresses, labels, header constants (all upstream), and every DT property the drivers read.

## Bug

**B1. v3 patch 1 still has the old msgbox binding.** It does not match the tree or the owner's decision of 2026-10-27 ("Mailbox binding requires all four interrupts").
- `v3-0001…patch:117,125`: `minItems: 1` on `interrupts` and `interrupt-names`.
- `:158`: the `if: required: [allwinner,tx-done-irq] then: minItems: 4` block.
- `:138`: "Needs the interrupts of the remote banks".
- `interrupt-names` is not in `required`.
- The Changes-in-v3 note (`:27`) says "interrupts and interrupt-names are a fixed list … with minItems: 1".
- Tree: `allwinner,sun55i-a523-msgbox.yaml:70-82` has no minItems, `:107-108` requires both, and there is no conditional.
- Regenerate the patch and rewrite the note, e.g. "all four lines exist on every A523 and are required, answering 'How could an interrupt be optional?'".

**B2. The exported driver and test patches are older than the tested tree, and their commit messages and the cover letter state wrong facts.** Patch file vs tree, diff lines: sun55i-msgbox.c 32, sunxi_rproc.c 216, sunxi_rproc.h 8, sun55i_msgbox_test.c 423, sunxi_rproc_test.c 737. The rproc binding (patch 3) and the dtsi (patch 5) match.
- **Patch 2 driver has a real fix missing.**
  - It sets `controller.chans/ops/dev` after `devm_request_irq()`; the tree moved this before the IRQ ("The interrupt handler walks the channels: set them up before it", sun55i-msgbox.c:497-501).
  - It lacks `clear_bit(n, &mbox->tx_held)` in send_data (dropping a held message after a timed-out blocking send).
  - It uses `mutex_init` instead of `devm_mutex_init`.
- **Patch 4 driver lacks the current tree's code, and its Changes-in-v3 does not mention any of it:**
  - `sunxi_rproc_sanity_check()` (rejects non-RV32 ELF);
  - coredump segments (`sunxi_rproc_add_dump_segments`);
  - DMA-pool error handling (`-ENODEV` vs other errors, devm release action);
  - the "memory-region above 4 GiB" check;
  - the `has_reserved_mem` removal.
- **Test counts:**
  - Cover letter `v3-0000:56`: "KUnit: 27/27 message box, 15/15 remoteproc".
  - Patch 6 `:6`: "Add 26 KUnit tests". This also disagrees with the cover's 27.
  - Patch 7 `:6`: "Add 15 KUnit tests".
  - Tree: 36 KUNIT_CASE in sun55i_msgbox_test.c and 26 in sunxi_rproc_test.c, 62 in total.
- **Patch 7 commit message:**
  - `:17-19` says the fakes are "plain memory accessed with readl() and writel(), so the tests depend on ARM64 or COMPILE_TEST". `:25` says "Mock register reads use readl()". `:52` has `depends on … (ARM64 || COMPILE_TEST) && !UML`.
  - Tree: register and SRAM access goes through KUNIT_STATIC_STUB_REDIRECT helpers (sunxi_rproc.c:58-80). drivers/remoteproc/Kconfig:394 has `depends on SUNXI_REMOTEPROC && KUNIT && COMMON_CLK && RESET_CONTROLLER`, and the tests now use fake clocks and a fake reset controller (Kconfig help :397-403).
- **Cover letter diffstat** (yaml 157 lines, driver 602/691, tests 902/551) is stale.
- Re-export from the series branch, then re-run checkpatch, dt_binding_check and dtbs_check on the exported base before restating "clean".

## Should-fix

**S1. The binding patch must justify `allwinner,tx-done-irq` itself.**
- Patch 1's commit message only says "Add optional allwinner,tx-done-irq property to select write-IRQ based TX completion instead of polling" (`v3-0001:15-17`). That reads as OS policy.
- The precedent (`ti,mbox-send-noirq`, ti,omap-mailbox.yaml) and the system-integrator rationale are only in the cover letter, which does not go into git history. DT maintainers review patch 1 on its own.
- Put two or three sentences in the patch 1 commit message. This changes only where the reasoning is written; the property stays as decided.

**S2. Patch 3 justifies the optional `mboxes` with an out-of-tree UIO driver.**
- `v3-0003:28-29`: "mboxes is optional: a user can drive the message box from userspace (UIO) instead". The UIO driver is project-only (DESIGN_DECISIONS "Mailbox is optional").
- Bindings are not justified by userspace or out-of-tree users. Use the reason the driver actually implements: without `mboxes` the core is loaded, started and stopped with no IPC (sunxi_rproc.c:713-737, "standalone mode").

**S3. The driver supports a DMA pool on on-chip SRAM described in `/reserved-memory`, and the cover letter advertises it.**
- sunxi_rproc.c:575-583 ("A pool placed on the core's SRAM reuses the existing mapping") matches a memory-region whose address equals r_sram or r_sram1.
- The project's overlay `cubie-a5e-rpmsg-sram.dtso` does this: a `shared-dma-pool` at 0x072c0000, inside the node's own `reg` r_sram1.
- The cover letter (`v3-0000:58-59`) says it was tested "with vrings in DRAM and in SRAM".
- `/reserved-memory` carves system RAM ("exclude reserved memory from normal usage", reserved-memory.yaml). On-chip SRAM is described with `mmio-sram` (sram.yaml) and a `sram` phandle in mainline: ti,k3-dsp-rproc.yaml:77 and amlogic,meson-mx-ao-arc.yaml:50.
- Expect Krzysztof or Rob to object as soon as anyone asks how SRAM vrings are described. Either drop the SRAM-pool branch and the claim from v3, or describe it the mmio-sram way. D10 covers the order of the DRAM regions, not this.

**S4. The driver hardcodes physical addresses that are already in DT `reg`.**
- `sun55i_rproc_att` (sunxi_rproc.c:38-47) uses SUN55I_SRAM_SPACE0_SYS 0x07280000, SPACE1_SYS 0x072c0000 and their sizes from sunxi_rproc.h:21-24. The same values are `reg` "r_sram"/"r_sram1".
- `da_to_va()` translates through the hardcoded `sa` and then looks the result up in the DT-mapped windows (sunxi_rproc.c:377-423). If a DT's `reg` differs, the lookup fails silently with NULL.
- This also breaks the project's own rule (.agents/rules/devicetree-hardware-configuration.md §1: no hardcoded physical addresses).
- Keep the per-SoC ATT (A733 decision) but take `sa` and `size` from `priv->r_sram*_phys/size`. The table then only needs the E906 `da`, and the identity rows :45-46 come for free.

**S5. memory-region index 0: the binding and the driver disagree.**
- The binding says item 0 is the "Virtio vring and buffer pool" named `vram` (rproc.yaml:76, :83).
- The driver accepts an index 0 that is not a DMA pool ("-ENODEV: it is not a DMA pool (a DT without virtio)", sunxi_rproc.c:541-551) and registers it as a carveout labelled with name[0].
- So a DT with DRAM firmware and no virtio must name its DRAM region "vram".
- Either the binding describes the no-virtio case, or the driver rejects a non-pool index 0. The positional layout (D10) is unchanged either way.

**S6. Project docs now contradict the 2026-10-27 interrupts decision.** The next AI or human pass will re-add `minItems`.
- UPSTREAM_LESSONS_LEARNED.md Lesson 1.2 (:55-90) prescribes `minItems: 1` on msgbox interrupts. Its checklist line says "Every `interrupts` and `interrupt-names` uses positional `items:` with `minItems`".
- .agents/rules/upstream-patch-submissions.md:27 says "with `minItems: 1`".
- DESIGN_DECISIONS.md:55-56 still says the D12 heading is "REOPENED … must be redone before v3 is sent", although the tree has done it (four clocks and resets, shared reset never asserted).

**S7. Not in the series, but in cubie-linux-7.1: the A733 board DTS files misuse the sun55i compatible.**
- sun60i-a733-cubie-a7a.dts:344-353 and sun60i-a733-cubie-a7z.dts:434-435 have `compatible = "allwinner,sun55i-a523-msgbox", "allwinner,sun6i-a31-msgbox"` with 2 regs, 1 clock, 1 reset and 1 IRQ.
- `CHECK_DTBS=y` on a7a gives 5 schema errors: compatible too long; reg, clocks, resets and interrupts too short.
- If sun55i-msgbox binds first it fails probe at `devm_platform_ioremap_resource(pdev, 2)` (sun55i-msgbox.c:503-508).
- Use the sun6i compatible alone, or a sun60i one, so the tree's allwinner dtbs_check stays clean.

## Nit

**N1.** drivers/remoteproc/Kconfig:383 adds SUNXI_REMOTEPROC after XLNX_R5_REMOTEPROC (:370). The list is otherwise alphabetical (ST_SLIM :313, STM32 :316, TI_K3_* :331-357), so place it after STM32_RPROC. There is also a double blank line at :390-391.

**N2.** The rproc binding example (rproc.yaml:127-147) has no `memory-region`/`memory-region-names`, which is the positional list Krzysztof asked for (KK8), so the example never exercises it. Its `firmware-name = "riscv-firmware.elf"` is also the driver default (sunxi_rproc.c:614) and is a bare name at the /lib/firmware root. Mainline examples use a vendor/SoC path (e.g. "allwinner/sun55i-a523/…").

**N3.** The crash IRQ is looked up only by name (`platform_get_irq_byname_optional(pdev, "crash")`, sunxi_rproc.c:696). The binding allows `interrupts` without `interrupt-names` (rproc.yaml:87-96), and that interrupt is then silently ignored. Use index 0 in the driver, or tie the two together in the binding.

**N4.** reg-names `r_sram`/`r_sram1` (rproc.yaml:34-35): sunxi uses the `r_` prefix for PRCM/R-domain blocks (`r_ccu`, `r_pio`), which these SRAMs are not, and the descriptions say "SRAM space 0/1". `sram0`/`sram1` would read better. These names are ABI, so it is cheap only now; owner's call.

**N5.** The rproc binding says "CPUX message box" (rproc.yaml:18). The msgbox binding calls the same thing the "Cortex-A55 host bank" (msgbox.yaml:30, :72). Use one term.

**N6. MAINTAINERS:**
- The entry titles are inconsistent: "ALLWINNER SUN55I MESSAGE BOX DRIVER" (:913) vs "ALLWINNER XUANTIE RISC-V REMOTEPROC DRIVER" (:935).
- The ARM/Allwinner `N: sun[x456789]i` regex (:2434) does not match "sun55i". `get_maintainer.pl -f drivers/mailbox/sun55i-msgbox.c` returns no sunxi maintainers and no linux-arm-kernel (the rproc driver does get them, via "sunxi"). Make sure the send list (TODO T3) adds them for patches 2 and 6.

**N7.** `devm_pm_domain_attach_list(dev, NULL, …)` (sunxi_rproc.c:646) attaches by index, so the required `power-domain-names` (rproc.yaml:103-106) is never used. Passing `pd_names = {"riscv", "sram"}` would bind the order to the names.
