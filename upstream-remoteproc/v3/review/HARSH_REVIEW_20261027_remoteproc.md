# sun55i remoteproc v3 review: dt-bindings, driver, KUnit

Ref: `linux-cubie` branch `v3-next-20261026` (3a05cbdb8156), base next-20261026 (f1ac44c32cba).
Commits reviewed: a0729e4cf0c8 (binding), c17e31dda7ff (driver), 3a05cbdb8156 (KUnit).
Line numbers refer to the files at that ref (`git show v3-next-20261026:<path>`).

No Critical or High defects found. The da_to_va bounds and overflow checks, the kick payload
lifetime, the crash IRQ arm/disarm balance, the remove ordering (rproc_del, then
mbox_free_channel, then cancel_work_sync) and the probe unwind all check out against the
framework at this ref. Each finding below was re-checked against the framework code.

---

## Medium

### M1. `idr_for_each()` on `rproc->notifyids` without RCU or the rproc lock
`drivers/remoteproc/sunxi_rproc.c:61-66`
```c
static void sunxi_rproc_vq_work(struct work_struct *work)
{
	struct sunxi_rproc *priv = container_of(work, struct sunxi_rproc, vq_work);

	idr_for_each(&priv->rproc->notifyids, sunxi_rproc_vq_notify, priv->rproc);
}
```
`lib/idr.c:192` says `idr_for_each()` may only run concurrently with `idr_alloc()` and
`idr_remove()` "if protected by RCU". The work holds neither RCU nor `rproc->lock`.
`rproc_vq_interrupt()` (`remoteproc_virtio.c:101-120`) protects its own `idr_find()` and
`rvring->vq` with `vq_srcu`, but SRCU does not delay the `call_rcu()` free of radix-tree
nodes, so it does not cover the walk the driver adds.

The mailbox channel is open from probe to remove, whatever state the rproc is in. Any
notification from the E906 queues this work:
- a late doorbell while the rproc shuts down: `rproc_resource_cleanup()` ->
  `rproc_free_vring()` -> `idr_remove()` (`remoteproc_core.c:412`);
- a doorbell during the next boot: `rproc_alloc_vring()` -> `idr_alloc()` (`:363`).

Failure: removing id 1 shrinks the tree to a direct root entry and frees the node with RCU,
while the worker is still walking it. That is a use-after-free in the walk. The window is
small but real, for example on firmware that keeps sending after it is told to stop, or on
`echo stop` during traffic.

The callback cannot run inside `rcu_read_lock()`, because `vring_interrupt()` ->
`rpmsg_recv_done()` takes mutexes. Fix: do not walk the IDR. Call
`rproc_vq_interrupt(rproc, i)` for `i = 0 .. READ_ONCE(rproc->max_notifyid)`. That function
already does its own protected lookup and ignores ids with no vq. Alternatively, take
`rproc->lock` and skip the call unless the state is `RPROC_RUNNING`, as stm32 does in
`stm32_rproc_mb_vq_work()`. (imx_rproc.c:872 has the same `idr_for_each()` pattern. Expect a
maintainer to point at it either way, so be ready to explain why yours is safe, or change it.)

### M2. Vring and carveout addresses handed to the E906 are host PAs, not E906 DAs, for SRAM
`sunxi_rproc.c:108-109`, `:474-481` and the ATT host-PA rows at `:40-42`
```c
mem[i] = rproc_mem_entry_init(priv->dev, rmem->va, (dma_addr_t)rmem->phys,
			      rmem->size, (u32)rmem->phys, NULL, NULL, ...);
```
The docs (`ALLWINNER_T527_CHIP_SPEC_REFERENCE.md` §3, `ALLWINNER_HETEROGENEOUS_RISCV_REFERENCE.md`
table) say the E906 sees SRAM space 1 only at DA `0x40000000` (host `0x072c0000`). With the
pool on SRAM1 (overlay `cubie-a5e-testPingRpmsgSram.dtso`), three addresses the remote gets
are host PAs:
- the `vram` carveout `da`;
- every `FW_RSC_ADDR_ANY` vring `da`, set to the dma address by `rproc_alloc_carveout()`;
- every rpmsg buffer address.

None of them are in the E906's view. It works today only because the firmware converts
addresses by hand (`firmware/common/hal/rpmsg.cpp:87-93`). The remoteproc ABI says
resource-table `da` is the remote's view. The ATT "host address" rows exist only to cover
this mismatch, and they make `da_to_va()` accept two address spaces at once (see L1).

Fix: describe the translation in DT with `dma-ranges` on the rproc node
(`0x40000000 -> 0x072c0000`, `0x3ffc0000 -> 0x07280000`). `rproc_rvdev_add_device()` already
copies the parent's `dma_range_map` (`remoteproc_virtio.c:524`), and `dma_alloc_coherent()`
then returns E906 DAs. Register SRAM carveouts with the E906 DA, and drop ATT rows 3 and 4
and the firmware-side conversion. If this stays as it is, the cover letter must say that
resource-table addresses in SRAM are host PAs.

### M3. Nothing handles system sleep while the E906 is running
`sunxi_rproc.c:539-545`. There are no `dev_pm_ops`.

The two PPU domains are attached as virtual genpd devices with `DL_FLAG_PM_RUNTIME` links
(`drivers/base/power/common.c:204-205`). During system suspend, genpd's noirq path
(`genpd_finish_suspend()` -> `genpd_sync_power_off()`) powers a domain off once all its
devices have suspended, whatever their runtime-PM usage count. The E906 and its SRAM lose
power while the rproc state is still `RPROC_RUNNING`. After resume, kicks reach a dead core,
rpmsg hangs, and nothing reports a crash. Clocks and resets also stay as `prepare()` left
them.

Fix: either add `suspend`/`resume` that call `rproc_shutdown()`/`rproc_boot()` (or refuse
suspend with -EBUSY while the rproc is running), or set the domains to stay on in suspend and
say so. At minimum, state the limitation in the cover letter.

---

## Low

### L1. `da_to_va()` resolves an address that means different things in the two spaces it accepts
`sunxi_rproc.c:34-43`, `:318`

The ATT holds E906 DAs (rows 1-2) and host PAs (rows 3-4) in the same table. E906 DA
`0x40000000-0x4003ffff` is SRAM1, but host PA `0x40000000-0x4003ffff` is DRAM, and the E906
cannot reach that DRAM (its 1:1 DRAM view starts at `0x40040000`).

Where it goes wrong: the core calls `ops->da_to_va()` before it walks the carveouts
(`remoteproc_core.c:194-198`). A host-allocated vring or trace `da` in host DRAM
`0x40000000-0x4003ffff` would therefore resolve to SRAM1. That happens with a `vram`/`trace`
region placed there, or with a CMA fallback after `of_reserved_mem_device_init()` fails
(its return value is ignored, `:453`). The firmware would also be given an address it cannot
reach.

Fix: in `sunxi_rproc_parse_memory_regions()`, reject DRAM regions below `0x40040000` and
regions that end above 4 GiB (see L2). M2's `dma-ranges` removes the host-PA rows altogether.

### L2. Carveout `da` silently truncated to 32 bits
`sunxi_rproc.c:108-109`
```c
rmem->size, (u32)rmem->phys, ...
```
On boards with more than 3 GiB, DRAM continues above `0x100000000` (4 GiB DRAM ends at
`0x13fffffff`). A `memory-region` placed there gets a `da` that aliases low DRAM. `da_to_va()`
lookups then hit the wrong region, and so does `rsc->da` if the firmware names the carveout.
`dma_set_coherent_mask(32)` (`:523`) protects only the DMA allocations, not the DT regions.
Fix: in `parse_memory_regions()`, return `-EINVAL` when
`rmem->phys + rmem->size > SZ_4G`.

### L3. Standalone mode still creates virtio devices whose kicks are silently dropped
`sunxi_rproc.c:293-294`, `:630-632`
```c
if (!priv->mbox_chan)
	return;
```
`.kick` is always set, so `rproc_add_virtio_dev()` passes its check
(`remoteproc_virtio.c:388`) even with no mailbox. A firmware with a vdev resource gets an
rpmsg bus that never delivers anything in either direction, and no error is printed.

Fix: in probe, when `mboxes` is absent, set `rproc->ops->kick = NULL`. `rproc->ops` is the
per-instance `kmemdup()` copy (`remoteproc_core.c:2422`), so this is allowed. Vdev creation
then fails with ".kick method not defined" instead of hanging.

### L4. `parse_fw` treats a malformed resource table as "no table"
`sunxi_rproc.c:372-382`
```c
ret = rproc_elf_load_rsc_table(rproc, fw);
if (ret == -EINVAL) { dev_dbg(...); return 0; }
```
`find_table()` (`remoteproc_elf_loader.c:267-332`) returns NULL, and so
`rproc_elf_load_rsc_table()` returns `-EINVAL` (`:355-357`), both for a missing
`.resource_table` and for a broken one. A broken one can be a version other than 1, a size
smaller than the header, offsets past the section end, or a section past the end of the
image.

Failure: firmware built with a bad table boots with no vrings and no trace, and the only
message is a `dev_dbg`. Fix: check for the section first (the `find_table` logic, or
`rproc_elf_find_shdr`-style) and fail on a malformed table. At least use `dev_warn`, as
stm32 does.

### L5. Carveouts registered in `.prepare()` leak, and are duplicated, when `parse_fw` fails
`sunxi_rproc.c:184-186` together with `remoteproc_core.c:1409-1411,1445-1447`

When `rproc_parse_fw()` fails, `rproc_fw_boot()` jumps to `unprepare_rproc` and skips
`rproc_resource_cleanup()`. The `vram`/`dram`/`trace` entries added by `prepare()` stay on
`rproc->carveouts`. The next boot adds a second set, and the stale entries are still matched
first by `rproc_find_carveout_by_name()`.

With this driver the trigger is a `kmemdup()` failure in `rproc_elf_load_rsc_table()`, or any
non-`-EINVAL` error once L4 is fixed. This is a framework flaw that stm32 and imx share (they
also register in prepare). Fix it in the core, by running `rproc_resource_cleanup()` on that
path, or keep it in mind once L4 makes `parse_fw` fail more often.

### L6. A PT_LOAD segment that spans both SRAMs cannot be loaded
`sunxi_rproc.c:318`. The E906 sees SRAM0 and SRAM1 as one contiguous 512 KiB
(`0x3ffc0000-0x4003ffff`). The ATT only matches a range that fits inside one window, so a
linker script that treats them as one region fails with "bad phdr da". The test at
`sunxi_rproc_test.c:150-169` confirms this behaviour is intended. Fix: document it as a
firmware link constraint in the binding description or Kconfig help, or split the copy
across windows.

### L7. SRAM pool detection needs an exact base match
`sunxi_rproc.c:474-481`
```c
if (priv->r_sram1_va && rmem->phys == priv->r_sram1_phys && rmem->size <= priv->r_sram1_size)
```
A pool at an offset inside SRAM1 (for example `0x072e0000`, leaving the bottom for code)
falls through to `devm_memremap(WC)`. That creates a second mapping of a region already
claimed by `devm_ioremap_resource_wc()`, and it is tagged `is_iomem = false`. Fix: test for
containment (`phys >= base && phys + size <= base + len`) and use `va = base_va + (phys - base)`.

### L8. The KUnit rx-callback test does not test what it claims
`sunxi_rproc_test.c:490-515`

The test replaces `priv.vq_work`'s function with `rx_work_probe_fn`. As a result:
- `sunxi_rproc_vq_work()` is never run;
- a regression that calls `rproc_vq_interrupt()`/`idr_for_each()` directly from
  `sunxi_rproc_mb_rx_callback()` and also queues the work still passes. The comment calls
  this test the "regression test for calling rproc_vq_interrupt() from the callback", but it
  cannot catch that.

That conflicts with D15 ("every test must exercise driver behaviour"). Fix: keep the real
work function. Register two fake vrings in `rproc->notifyids` whose `vq` callbacks record
their context, then assert that neither ran before `flush_workqueue()` and that both ran in a
kworker.

### L9. The KUnit kick test hand-builds `struct mbox_chan` internals
`sunxi_rproc_test.c:440-454`. The test sets `active_req`, `txdone_method`, `lock`, `cl` and
`mbox` directly, and depends on how `msg_submit()`/`tx_tick()` behave inside the mailbox
core. Any change to the mailbox core breaks the test without the driver changing. The test
also calls `prepare()` twice with no `unprepare()` (`:382,:398`), and `test_prepare_clears_sram`
never calls `unprepare()` either, so runtime-PM usage counts leak (harmless only because the
kunit device goes away).

---

## Nit

- N1 `sunxi_rproc.c:397`: `.coredump = rproc_coredump` is already the default
  (`remoteproc_core.c:2427`). The driver never calls `rproc_coredump_add_segment()`, so
  `rproc_coredump()` returns at once (`remoteproc_coredump.c:248`) and recovery dumps
  nothing. Either add the SRAM windows and carveouts as dump segments, or drop the line.
- N2 `sunxi_rproc_test.c:397`: the comment says "Second boot (restart/recovery): prepare() must
  re-add". Recovery does not call `prepare()`: `rproc_boot_recovery()` -> `rproc_start()` only.
  Say "restart".
- N3 `sunxi_rproc.c:609`: `dev->of_node &&` is redundant, since the driver is OF-only and
  `of_property_present(NULL, ...)` is false anyway.
- N4 `sunxi_rproc.c:638`: `dev_err` after `rproc_add()`. Use `dev_err_probe()` for
  consistency with the rest of probe.
- N5 `sunxi_rproc.c:671-673`: the comment "Use a single compatible string per upstream DT
  binding policy" misstates the policy. The reason is that A523/A527/T527 are one die. Say
  that.
- N6 `Kconfig`: the prompt "Allwinner XuanTie RISC-V remoteproc support" reads as if XuanTie
  were an Allwinner product. Prefer "Allwinner sun55i E906 (T-Head XuanTie) remoteproc".
- N7 `sunxi_rproc.c:509`: the default firmware name `riscv-firmware.elf` sits in the root of
  `/lib/firmware`, which invites collisions. Prefer an SoC-specific path such as
  `allwinner/sun55i-a523-e906.elf`.
- N8 `sunxi_rproc.c:320-321`: `*is_iomem` is written even when the SRAM check then returns
  NULL. This is harmless, but set it only on success.
- N9 `sunxi_rproc.c:9`: the header comment says "CCU-integrated remoteproc driver", which
  means nothing to a reader. Drop it.
- N10 `sanity_check = rproc_elf_sanity_check` does not check `e_machine == EM_RISCV`, so an
  ARM ELF is loaded into SRAM. Consider a wrapper.
- N11 Binding: `memory-region-names` is not tied to `memory-region` (no `dependencies:`),
  and the binding does not say that the regions must be `no-map`. `devm_memremap(WC)` fails
  on mapped RAM with only a generic error.

---

## Verified clean (no finding)

- `da_to_va()` overflow, length 0, straddling and `is_iomem` paths (`:342`, `:318`,
  `:353-359`).
- The boot address is written and read back before the core reset is released (`:245-248`).
- Kick payloads are static, one per vring, and `notifyid` is bounds-checked. `knows_txdone`
  and `tx_block` are both false, which is safe from atomic context. The msgbox driver may
  hold the pointer (`sun55i-msgbox.c:434`); static storage covers that.
- Crash IRQ: `IRQF_NO_AUTOEN`, the armed flag, and `disable_irq_nosync()` in the thread with
  `disable_irq()` in stop stay balanced through the recovery path
  (`rproc_boot_recovery()` -> stop -> start).
- Remove order: `rproc_del()` (which cancels `boot_work` and `crash_handler`), then
  `mbox_free_channel()` (the msgbox `shutdown()` waits for its IRQ handler), then
  `cancel_work_sync()`, then the devm workqueue, IRQ, mappings, runtime PM, power domains and
  `rproc_free`. The order is correct.
- `-EPROBE_DEFER` reaches `dev_err_probe()` for clocks, resets, the IRQ, the mailbox and the
  power domains.
- Binding example headers and macros exist at this ref (`CLK_BUS_MCU_RISCV_CFG`,
  `RST_BUS_MCU_RISCV_DEBUG`, `PD_RISCV`, `PD_SRAM`, ...).
- The KUnit kick test does fail on a shared mutable payload, and the overflow and bounds
  tests do fail on a missing check.

---

## Conflicts with owner decisions

- **Remap register read-modify-write with no lock against the DSP owner of bit 0**
  (`sunxi_rproc.c:176-178`, `:214-215`; the binding's `remap` reg sits outside any node,
  inside PRCM): a maintainer will ask for a syscon/regmap or an R-CCU-provided control. This
  is covered by D9 (owner keeps it and explains it in the cover letter).
- **Every memory-region is registered as a carveout, DRAM firmware included**
  (`:105-117`, `:184-186`): this is D7. M2, L1 and L2 are validation and address-space issues
  in that scheme, not a request to drop it.
- **Positional `memory-region` with the pool at index 0** (binding `:73-85`): this is D10.
  M2's `dma-ranges` suggestion is compatible with it.
- **The ATT/`sunxi_rproc_cfg` indirection for a single SoC**: this is the A733 placeholder
  decision. It is not raised as dead code. Only the host-PA rows are questioned, in M2 and L1.
