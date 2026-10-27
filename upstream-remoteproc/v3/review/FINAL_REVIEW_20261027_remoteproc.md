# sunxi_rproc final review (linux-cubie 91018e3b423e)

Files read in full: drivers/remoteproc/sunxi_rproc.c, sunxi_rproc.h, sunxi_rproc_test.c,
Kconfig/Makefile entries, allwinner,sun55i-a523-rproc.yaml, sun55i-a523.dtsi rproc node,
board DTS (rproc not enabled there; overlays in project-cubie-a5e/dts-overlay checked).
Checked against remoteproc_core.c / remoteproc_virtio.c / remoteproc_elf_loader.c,
drivers/of/of_reserved_mem.c, kernel/dma/coherent.c, drivers/base/power/*, kernel/irq/irqdesc.c,
drivers/mailbox/mailbox.c and sun55i-msgbox.c in the same tree. Owner decisions in
DESIGN_DECISIONS.md were not re-raised. Nothing was compiled (no toolchain/O= dir at hand).

## Bugs

### B1. KUnit module does not link: `irq_to_desc()` is not exported on SPARSE_IRQ
sunxi_rproc_test.c:724 and :730

    KUNIT_EXPECT_EQ(test, irq_to_desc(irq)->depth, 0U);
    KUNIT_EXPECT_EQ(test, irq_to_desc(irq)->depth, 1U);

kernel/irq/irqdesc.c:415-421 (the CONFIG_SPARSE_IRQ variant):

    struct irq_desc *irq_to_desc(unsigned int irq) { ... }
    #ifdef CONFIG_KVM_BOOK3S_64_HV_MODULE
    EXPORT_SYMBOL_GPL(irq_to_desc);
    #endif

arm64 (arch/arm64/Kconfig:251) and UML (arch/um/Kconfig:46) select SPARSE_IRQ. The test is
`tristate` (Kconfig:392); it is forced to `m` whenever SUNXI_REMOTEPROC=m, and allmodconfig with
KUNIT_ALL_TESTS gives `m`. Then modpost fails: `"irq_to_desc" [drivers/remoteproc/sunxi_rproc_test.ko]
undefined!`. The board config builds it `=y` (linux.config:59), which hides it; the 0-day robot
will not. Fix: drop the depth checks (the `irqd_irq_disabled()` checks plus a third start()/stop()
already prove balance), or check the depth indirectly (e.g. a second `enable_irq()` must leave it
enabled, then `disable_irq()` once must disable it).

### B2. A non-pool `memory-region[0]` fails probe; the "-ENODEV: not a DMA pool" branch is dead
sunxi_rproc.c:541-552

    /*
     * memory-region[0] is the default DMA pool for vrings and vdev buffers;
     * -ENODEV: it is not a DMA pool (a DT without virtio)
     */
    ret = of_reserved_mem_device_init(dev);
    ...
    } else if (ret != -ENODEV) {
            return dev_err_probe(dev, ret, "failed to set up the DMA pool\n");

of_reserved_mem.c:688-701: -ENODEV is returned only when the phandle does not resolve; a region
without `compatible = "shared-dma-pool"` returns **-EINVAL** (`!rmem->ops || !rmem->ops->device_init`).
A disabled node returns 0. So with count > 0 the -ENODEV branch is effectively unreachable (and if
it were taken, `of_reserved_mem_region_to_resource()` would fail probe two lines later anyway), and
the case the comment promises ("a DT without virtio": index 0 is a plain `no-map` region) fails
probe with "failed to set up the DMA pool: -22". The core itself treats a missing pool as
non-critical (remoteproc_virtio.c:421-429 ignores the return value). Fix: accept -EINVAL (not a
pool) the way the comment intends, or require `shared-dma-pool` in the binding text and drop the
-ENODEV special case and the comment. No KUnit test covers this function (see S2).

## Should-fix

### S1. `parse_fw()` failure leaks the carveouts and dump segments `prepare()` registered
sunxi_rproc.c:245-248 register `rproc->carveouts` and `rproc->dump_segments` in `prepare()`.
remoteproc_core.c:1394-1405 / 1435-1440 (`rproc_fw_boot()`):

    ret = rproc_prepare_device(rproc);
    ...
    ret = rproc_parse_fw(rproc, fw);
    if (ret)
            goto unprepare_rproc;      /* skips rproc_resource_cleanup() */

`sunxi_rproc_parse_fw()` returns the `rproc_elf_load_rsc_table()` error for anything but -EINVAL,
i.e. -ENOMEM from `kmemdup()` (remoteproc_elf_loader.c:350-352). Scenario: boot under memory
pressure / fault injection -> -ENOMEM -> unprepare runs, but the mem entries and dump segments stay
on the lists. The next boot's `prepare()` appends a second set (duplicate "dram"/"vram" carveouts:
`rproc_find_carveout_by_name()` returns the stale one; duplicated dump segments). If the driver is
unbound instead, `rproc_del()` -> `rproc_shutdown()` returns -EINVAL (state OFFLINE) and
`rproc_type_release()` (core.c:2369) frees neither list: a permanent kmemleak. imx_rproc has the
same prepare-time pattern, but that does not make it correct. Fix: register carveouts and dump
segments in `parse_fw()` after the table is loaded (allocate the entries first so nothing can fail
after list insertion), or fix the core's `unprepare_rproc` label to run `rproc_resource_cleanup()`.

### S2. KUnit leaves the crash ISR and all probe/teardown code untested
Measured-coverage goal is 100%, but these driver functions are never called by any test:
`sunxi_rproc_crash_handler()` (:115), `sunxi_rproc_register_mem()` (:485),
`sunxi_rproc_parse_memory_regions()` (:528), `sunxi_rproc_release_dma_pool()` (:523),
`sunxi_rproc_free_mbox()` (:598), `sunxi_rproc_probe()`, `sunxi_rproc_remove()`, and the warning
branch of `sunxi_rproc_add_dump_segments()` (:152). The crash tests request the line with a test
handler (`mock_crash_irq`, test.c:673/699), so the ISR's `test_and_clear_bit()` +
`disable_irq_nosync()` handshake with `stop()` (the exact race checklist group 18 targets) is not
exercised: a handler that disabled unconditionally (depth 2 after a racing stop(), line dead after the
next start()) would pass every test. Likewise B2 would have been caught by a parse_memory_regions
test (>4 GiB, >3 entries, pool/non-pool index 0, SRAM-placed pool, missing names).

### S3. `pm_runtime_put()` (async) in `unprepare()` and the prepare error path
sunxi_rproc.c:263 and :288. On unbind, `rproc_del()` -> `rproc_shutdown()` -> `unprepare()` queues
an async idle; `remove()` returns and devres' `pm_runtime_disable()` -> `__pm_runtime_barrier()`
(runtime.c:1428-1436) cancels it. The device is left RPM_ACTIVE and the two PPU domains go off only
as a side effect of `devm_pm_domain_detach_list()` dropping the device links. On a normal shutdown
the power-off timing is likewise non-deterministic relative to the next `prepare()`. Use
`pm_runtime_put_sync()` (as imx_dsp_rproc.c:767 does) so the domains are off when `unprepare()`
returns.

## Nits

N1. **Kconfig/Makefile placement and text.** Kconfig:383 and Makefile:43-44 put SUNXI after
XLNX; both files are alphabetical (STM32_RPROC < SUNXI < TI_K3). Kconfig:390-391 has a double blank
line. The help text does not give the module name ("If compiled as a module, it will be called
sunxi_rproc"), as peers do.

N2. **The dump segments hardcode E906 addresses, bypassing the per-SoC cfg.** sunxi_rproc.c:141-143
use `E906_SRAM_SPACE0_DA` / `E906_SRAM_SPACE1_DA` directly, while da_to_va goes through `cfg->att`.
The cfg indirection is kept for the A733/E902 on purpose; this function is the one place that
breaks it (an E902 dump would carry E906 addresses). Take the core-side DA from the cfg/ATT.

N3. **Classifying memory regions.** sunxi_rproc.c:576-590 treat a region as SRAM only on an exact
base match (`rmem->phys == priv->r_sram1_phys`). A pool on part of an SRAM (e.g. 0x072e0000, 128 KiB)
gets a second WC mapping through `devm_memremap()` and `is_iomem = false`, and is added as a
duplicate dump segment (:144-148). Test containment (`phys >= sram_phys && phys + size <=
sram_phys + sram_size`) and use `sram_va + offset`. Also no check that a DRAM region lies in the
E906's 1:1 DRAM view (>= 0x40040000, header :14-18): a region at 0x40000000-0x4003ffff gets
`da = phys`, which the ATT resolves to SRAM space 1.

N4. **Comment accuracy.**
- :399-404: "allowing arbitrary kernel memory to be mapped". Nothing is mapped; an out-of-range
  offset into an existing mapping is returned. Say that.
- :408-414: "return NULL rather than falling through to compare DA against Host PAs". The ATT
  itself has host-PA entries (:44-46), and both paths return NULL anyway (:428/:435). Reword and
  simplify to one `return NULL`.
- :342-347: the kick comment says the pointer is queued "with txdone_poll". `add_to_rbuf()` queues it
  in every mode, including the DT-selected tx-done-irq mode.
- :462-464: `-EINVAL` also means a malformed table (bad version, non-zero reserved bytes,
  truncated; elf_loader.c:281-307), not only "no resource table found". Also, `parse_fw` logs on
  `rproc->dev.parent` while `sanity_check` (:450) uses `&rproc->dev`; pick one.

N5. **Header hygiene and naming.** sunxi_rproc.h includes clk.h, interrupt.h, io.h, pm_domain.h and
reset.h only for pointer types (forward declarations do), while sunxi_rproc.c uses
`dma_set_coherent_mask()` without `<linux/dma-mapping.h>`. `ATT_IOMEM` and the `E906_*` macros are
not namespaced (`SUNXI_`/`SUN55I_`). `int flags` holds `BIT(30)` (use `u32`). Comments are in Title
Case ("XuanTie CFG Block Register Offsets", "CCU Clocks & Resets"). `SUNXI_REMAP_MCU_RAM_BIT` is
used only by the test. `priv->pd_list` (:101) is never read after probe; make it a local.

N6. `sunxi_rproc_register_mem(pdev, rproc)` (:485) takes `rproc` only to reach `priv`. The error
labels mix styles (`err_disable_sram_clk` vs `err_disable_bus`, :252-255).

N7. The crash ISR's `dev_err()` (:119) duplicates `rproc_report_crash()`'s own dev_err
(core.c:2707-2708). Make it dev_dbg or drop it.

N8. **Standalone mode with a vdev.** `kick()` returns silently when there is no channel (:356-357).
Firmware with a vdev resource on a DT without `mboxes` gets virtio/rpmsg devices that never deliver
or receive anything, with nothing logged. Use a `dev_warn_once()`, or refuse the vdev in standalone
mode.

N9. The default firmware name `"riscv-firmware.elf"` (:611) is generic and sits at the top of
/lib/firmware, where it can collide. Peers either use the core default (`rproc-%s-fw`) or a vendor
path.

N10. **KUnit hygiene.**
- An ASSERT failure mid-test leaks resources: `rproc_alloc()` in
  `test_vq_work_notifies_every_vring` (:634-648), carveouts in `test_rproc_da_to_va_dram_carveout`
  (:320-346) and `test_prepare_adds_dump_segments` (:1057). Use `kunit_add_action_or_reset()`.
- The kick test hand-builds a `struct mbox_chan` (:528-537: active_req, txdone_method, lock), which
  ties the test to mailbox-core internals.

N11. Wrapping `readl`/`writel`/`memset_io` and the vq notify in `KUNIT_STATIC_STUB_REDIRECT`
functions (:58-90) is test-only indirection in production MMIO paths. Expect a maintainer to ask
why; have the answer ready in the cover letter (D15 keeps KUnit but does not cover this).

## Verified OK (no finding)
- da_to_va: one overflow guard before every `da + len`; each SRAM alias has one ATT entry, and
  space 0/1 do not overlap; `is_iomem` is set only for a returned pointer; DRAM is left to carveouts.
- Probe: INIT_WORK before any path that cancels it; workqueue before the channel; crash IRQ
  `IRQF_NO_AUTOEN`; `-EPROBE_DEFER` propagated; `mbox_chan` reset to NULL on ERR_PTR.
- Teardown: `rproc_del()` -> `mbox_free_channel()` -> `cancel_work_sync()`, then devres frees the
  workqueue, the IRQ and (last) the rproc.
- Crash IRQ: ONESHOT thread + `disable_irq_nosync()` keeps the line masked;
  `test_and_set/clear_bit` gives one `disable_irq*()` per arm; `stop()`'s sync `disable_irq()`
  runs from the recovery work, never from the IRQ thread, so it cannot deadlock.
- prepare/unprepare: reset/clock order mirrored, every failure unwound, mem entries freed, remap
  only bit 1. STA_ADD is written with the cfg reset/clock up, with read-back before the core reset
  is released. The recovery path keeps clocks and resets.
- Kick: immutable per-vring payload; a full queue (`-ENOBUFS`) drops a doorbell, which is harmless
  because the firmware drains the doorbell FIFO and scans all vrings.
- sanity_check rejects non-ELF32 / non-RISC-V before prepare(); coredump ELF info set.

## Core limitations seen (not driver findings; for the cover letter / a separate core patch)
- `rproc_del()` does not flush `rproc->crash_handler`. A crash reported just before unbind runs
  after `rproc_del()` (state DELETED is neither CRASHED nor OFFLINE, core.c:1866-1879), calls
  `ops->stop()` on resources devres has already released, and on an rproc that may be freed.
  This applies to every driver.
- No SRCU or lock protects `rproc_vq_interrupt()` (remoteproc_virtio.c:95-99) against
  `rp_del_vqs()` during `rproc_stop_subdevices()`, which runs before `ops->stop()`, so a vq work
  item can race vring teardown. This applies to imx/stm32 too. **Note:** checklist group 6 in
  `.agents/rules/linux-kernel-review-prompt.md` says the lookup is "under the remoteproc vq SRCU".
  That is false in this tree; correct the checklist.
- After a crash with recovery disabled, `rproc_shutdown()` returns -EINVAL (state CRASHED). Unbind
  then leaves the clocks enabled, the resets deasserted and the PM reference held.
