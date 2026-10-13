# Task: fix the v3 sun55i msgbox + remoteproc series before it is sent

You are working on an unsent v3 Linux kernel patch series. It adds an
Allwinner A523 message box driver and a XuanTie E906 remoteproc driver. A
manual review found bugs and binding problems. It also found that the
review tooling missed them. Fix both. Each item below gives the problem,
the evidence and the fix. Apply them in order.

## 0. Ground rules (read first)

- Read these before touching git:
  `/home/tcmichals/ssdData/projects/home/CubieA5E/.agents/rules/kernel-branch-discipline.md`
  and `.agents/rules/git-identity.md`. All commits use
  `Tim Michals <tcmichals@gmail.com>`. Never use a `@verityinst.com` address.
- Kernel tree: `/home/tcmichals/ssdData/projects/home/CubieA5E/linux-cubie`.
  Workspace: `/home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/upstream-remoteproc`.
- The series has 7 commits: msgbox binding, msgbox driver, msgbox KUnit,
  rproc binding, rproc driver, rproc KUnit, DTS. The board branch
  `cubie-linux-7.1` carries the code plus extra fixup commits. The branches
  `v3-sun55i-rproc-msgbox-fixed` and `origin/v3-sun55i-rproc-msgbox` exist.
  **Find out which branch `v3/*.patch` was exported from before editing.**
  If it is unclear, stop and ask the user.
- Fold every fix into the commit it belongs to. Use
  `git commit --fixup=<sha>` and then
  `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash <base>`. Do not add
  "fix review comments" commits to the series.
- The board relies on two things upstream will not accept: the UIO
  standalone mode and the `generic-uio` mailbox check (overlay
  `cubie-a5e/project-cubie-a5e/dts-overlay/allwinner/cubie-a5e-uio.dtso`).
  Remove them from the upstream series. Keep them only as a separate
  `LOCAL:` commit on `cubie-linux-7.1` (see 2.6).
- Match the style of the code around each change. Keep comments to the
  existing density. Do not add narrative comments that explain the fix.

---

## 1. Mailbox driver: `drivers/mailbox/sun55i-msgbox.{c,h}`

### 1.1 Request only the ARM port interrupt, and make all teardown devm

**Problem.** Probe requests every interrupt in DT (4 lines, `IRQF_SHARED`).
`sun55i_msgbox_irq()` only services `regs[0]`, the ARM bank. The
`dsp`/`cpus`/`rv` lines signal reads pending in the *other* processors'
banks. A kick written into the RV FIFO raises SPI 174, and that line stays
high until the E906 drains its FIFO. Linux's handler returns `IRQ_NONE`
every time. The result is spurious interrupts and possibly "nobody cared"
disabling the line.

**Fix.** Request interrupt index 0 only, with no `IRQF_SHARED`. Convert
clocks, resets, the IRQ and the controller to devm, and delete
`sun55i_msgbox_remove()`. devm then releases in reverse order:

1. The controller is unregistered. Each channel's `shutdown()` masks its
   own RX IRQ.
2. The IRQ is freed.
3. The resets are asserted.
4. The clocks are disabled.

This is the ordering the hand-written `remove()` was attempting. The
`irq_cnt` clamp (old finding M1) disappears with it.

### 1.2 Enable every bank's clock and reset

**Problem.** The driver writes and polls `regs[1..3]`, but it enables only
`CLK_BUS_MSGBOX`. The RV bank's gate (`CLK_BUS_MCU_RISCV_MSGBOX`) and reset
are owned by the rproc node and are only on while the rproc is prepared.
The DSP bank's gate is never enabled at all. All four gates exist in the
dt-bindings headers:

| Bank | Clock | Reset |
|---|---|---|
| arm (0x03003000) | `&ccu CLK_BUS_MSGBOX` | `&ccu RST_BUS_MSGBOX` |
| dsp (0x07120000) | `&mcu_ccu CLK_BUS_MCU_DSP_MSGBOX` | `&mcu_ccu RST_BUS_MCU_DSP_MSGBOX` |
| cpus (0x07094000) | `&r_ccu CLK_BUS_R_MSGBOX` | `&r_ccu RST_BUS_R_MSGBOX` |
| rv (0x07136000) | `&mcu_ccu CLK_BUS_MCU_RISCV_MSGBOX` | `&mcu_ccu RST_BUS_MCU_RISCV_MSGBOX` |

**Fix.** The msgbox node takes all 4 clocks and 4 resets (see 3.1 and
4.1). The rproc node drops its `msgbox` clock and reset (2.7).

> Hardware caveat: on unbind, the CPUS bank reset is asserted while the CPUS
> firmware may still use that bank. The resets are fetched as *shared*, so
> this happens only when the last user goes away. Verify on the board that
> CPUS firmware (if any runs) survives `unbind` of the msgbox. If it does
> not, leave that reset deasserted on remove.

Replace `sun55i_msgbox_probe()` and delete `sun55i_msgbox_remove()`:

```c
static void sun55i_msgbox_assert_reset(void *data)
{
	reset_control_assert(data);
}

static int sun55i_msgbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_bulk_data *clks;
	struct reset_control *rst;
	struct mbox_chan *chans;
	struct sun55i_msgbox *mbox;
	int i, ret, irq, local_n;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	spin_lock_init(&mbox->lock);

	chans = devm_kcalloc(dev, SUN55I_NUM_CHANS, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	for (i = 0; i < SUN55I_NUM_CHANS; i++)
		chans[i].con_priv = mbox;

	for (i = 0; i < SUN55I_MAX_PROCESSORS; i++) {
		mbox->regs[i] = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(mbox->regs[i]))
			return dev_err_probe(dev, PTR_ERR(mbox->regs[i]),
					     "failed to map resource %d\n", i);
	}

	/* Every port's register bank has its own bus clock and reset */
	ret = devm_clk_bulk_get_all_enabled(dev, &clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to enable clocks\n");

	rst = devm_reset_control_array_get_shared(dev);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "failed to get resets\n");

	ret = reset_control_deassert(rst);
	if (ret)
		return dev_err_probe(dev, ret, "failed to deassert resets\n");

	ret = devm_add_action_or_reset(dev, sun55i_msgbox_assert_reset, rst);
	if (ret)
		return ret;

	/* Disable all read IRQs and clear status */
	for (local_n = 0; local_n < SUN55I_NUM_ROUTES; local_n++) {
		writel(0, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
		writel(U32_MAX, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));
	}

	/*
	 * Only the ARM port interrupt belongs to this processor; the others
	 * signal reads pending in the remote processors' own banks.
	 */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, sun55i_msgbox_irq, 0, dev_name(dev), mbox);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request irq\n");

	mbox->controller.dev           = dev;
	mbox->controller.ops           = &sun55i_msgbox_chan_ops;
	mbox->controller.chans         = chans;
	mbox->controller.num_chans     = SUN55I_NUM_CHANS;
	mbox->controller.txdone_irq    = false;
	mbox->controller.txdone_poll   = true;
	mbox->controller.txpoll_period = 1;

	ret = devm_mbox_controller_register(dev, &mbox->controller);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register controller\n");

	return 0;
}
```

- Remove `.remove = sun55i_msgbox_remove` from `sun55i_msgbox_driver`.
- In `struct sun55i_msgbox` (header), delete `clk`, `reset`, `irqs[]` and
  `num_irqs`.
- Remove the now-unused `#include <linux/clk.h>` / `<linux/reset.h>` only
  if nothing else needs them. `clk_bulk_data` needs `<linux/clk.h>`, so
  keep that one.
- Run `grep` over `sun55i_msgbox_test.c` for the deleted fields; none are
  used today.

### 1.3 Remove A733 / sun60i from the series

A733 has no compatible that the driver matches and no DTS in the series.
Remove "sun60i" and "A733" from:

- the file header comment and `MODULE_DESCRIPTION` in `sun55i-msgbox.c`,
- the `SUN55I_MSGBOX` Kconfig prompt and help text,
- the binding (3.1),
- the commit subject of patch 2/7. The new subject is
  `mailbox: sun55i: add Allwinner A523 message box driver`.

A733 support goes in a later series, together with its compatible and DTS.

### 1.4 KUnit visibility macros

The driver currently uses non-static functions with
`#if IS_ENABLED(CONFIG_SUN55I_MSGBOX_KUNIT_TEST) EXPORT_SYMBOL_GPL(...)`.
Switch to the standard helpers:

- In the `.c` file, add `#include <kunit/visibility.h>`. Mark
  `sun55i_msgbox_arm_routes`, `sun55i_chan_to_route`, `sun55i_msgbox_irq`
  and `sun55i_msgbox_chan_ops` as `VISIBLE_IF_KUNIT`. Follow each one with
  `EXPORT_SYMBOL_IF_KUNIT(name);` and delete the `#if ... #endif` export
  blocks.
- In the header, wrap the 4 declarations in `#if IS_ENABLED(CONFIG_KUNIT)`
  ... `#endif`. Without it you get "static declaration follows non-static".
- In `sun55i_msgbox_test.c`, add `MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");`.

---

## 2. Remoteproc driver: `drivers/remoteproc/sunxi_rproc.{c,h}`

### 2.1 `stop()` sleeps under a spinlock, and can deadlock (must fix)

**Problem.** `sunxi_rproc_stop()` calls `disable_irq(priv->crash_irq)`
inside `spin_lock_irqsave(&priv->lock)`. `disable_irq()` calls
`synchronize_irq()`, which has `might_sleep()` (`kernel/irq/manage.c`) and
waits for the threaded handler. The threaded `sunxi_rproc_crash_handler()`
itself spins on `priv->lock`, so `stop()` waits for the handler while
holding the lock the handler needs. rproc ops are already serialized by
`rproc->lock`, so the spinlock is not needed.

**Fix.** Replace the `crash_irq_enabled` bool and `priv->lock` with an
atomic bit.

Header: delete `spinlock_t lock;` and `bool crash_irq_enabled;`, then add:

```c
#define SUNXI_RPROC_CRASH_IRQ_ARMED	0
...
	unsigned long flags;
```

Driver:

```c
static irqreturn_t sunxi_rproc_crash_handler(int irq, void *data)
{
	struct sunxi_rproc *priv = data;

	dev_err(priv->dev, "crash event from %s core\n", priv->cfg->name);

	/* Stays masked until the next start(); recovery runs stop() first */
	if (test_and_clear_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
		disable_irq_nosync(irq);

	rproc_report_crash(priv->rproc, RPROC_FATAL_ERROR);

	return IRQ_HANDLED;
}
```

In `sunxi_rproc_start()`, after the core reset is released:

```c
	if (priv->crash_irq > 0 &&
	    !test_and_set_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
		enable_irq(priv->crash_irq);
```

In `sunxi_rproc_stop()`, after asserting `rst_core`. No lock is held, so
`disable_irq()` is allowed to sleep:

```c
	if (priv->crash_irq > 0 &&
	    test_and_clear_bit(SUNXI_RPROC_CRASH_IRQ_ARMED, &priv->flags))
		disable_irq(priv->crash_irq);
```

- Drop `spin_lock_init(&priv->lock)` and `priv->crash_irq_enabled = false`
  from probe, and the `unsigned long flags` locals in start/stop.
- The cover letter mentions `test_and_clear_bit`. That makes
  `run_adversarial_audit.py` P4 pass legitimately now.

### 2.2 `remove()` frees the mailbox before `rproc_del()` (must fix)

**Problem.** `remove()` frees the crash IRQ, then the mailbox channels, then
cancels work, and only then calls `rproc_del()`. But `rproc_del()` shuts
down a running core and removes the virtio/rpmsg devices, and that teardown
can still call `.kick()` → `mbox_send_message(priv->tx_chan)` on a freed
channel. Probe requests the mailbox *before* `rproc_add()`, so the LIFO
order is `rproc_del()` first.

The v2 Sashiko question ("late IRQ after `rproc_del()` touches freed
vqs") is already handled by two things:

- `stop()` asserts the core reset and then calls `cancel_work_sync()`.
- `rproc_vq_interrupt()` returns `IRQ_NONE` when `idr_find()` comes back
  NULL.

**Fix.**

```c
static void sunxi_rproc_free_mbox(struct sunxi_rproc *priv)
{
	if (priv->rx_chan)
		mbox_free_channel(priv->rx_chan);
	if (priv->tx_chan)
		mbox_free_channel(priv->tx_chan);
	priv->rx_chan = NULL;
	priv->tx_chan = NULL;
}

static void sunxi_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct sunxi_rproc *priv = rproc->priv;

	/*
	 * rproc_del() stops the core and removes the virtio devices, which may
	 * still kick, so the mailbox must outlive it.
	 */
	rproc_del(rproc);

	sunxi_rproc_free_mbox(priv);
	cancel_work_sync(&priv->vq_work);

	if (priv->has_reserved_mem)
		of_reserved_mem_device_release(&pdev->dev);
}
```

- Drop the explicit `devm_free_irq()`. The crash IRQ was requested after
  `devm_rproc_alloc()`, so devm frees the IRQ before the rproc. By that
  point `stop()` has disarmed it, or it was never enabled (`IRQF_NO_AUTOEN`).
- Use `sunxi_rproc_free_mbox()` in the probe error path too (2.6).

### 2.3 DT carveouts only exist for the first boot (must fix)

**Problem.** `sunxi_rproc_parse_memory_regions()` calls
`rproc_add_carveout()` once, at probe. `rproc_resource_cleanup()`
(`remoteproc_core.c`) frees every entry of `rproc->carveouts` on each
shutdown and on failed boots. After the first stop, crash recovery or
restart, the named carveouts are gone. stm32 and imx register carveouts in
`.prepare()` for this reason (`stm32_rproc_prepare()`, `imx_rproc_prepare()`).

**Fix.** Map once at probe, and create the carveout entries in every
`prepare()`.

Header:

```c
#define SUNXI_RPROC_MAX_RMEM		3

struct sunxi_rproc_rmem {
	const char *name;
	void *va;
	phys_addr_t phys;
	size_t size;
	bool is_iomem;
};
...
	/* in struct sunxi_rproc */
	struct sunxi_rproc_rmem rmem[SUNXI_RPROC_MAX_RMEM];
	unsigned int num_rmem;
```

### 2.4 `vram`/`dram` overwrite each other, and `devm_memremap()` errors are ignored (must fix)

**Problems.**

- Both names hit the same `strstr(name, "dram") || strstr(name, "vram")`
  branch. A DT using all three regions the binding allows therefore ends
  up with `dram_*` pointing at whichever region came last.
- `devm_memremap()` returns `ERR_PTR` on failure, never NULL. The `if (va)`
  check lets an `ERR_PTR` through into a carveout.

**Fix.** Match names exactly, and check `IS_ERR`. Replace
`sunxi_rproc_parse_memory_regions()`:

```c
static int sunxi_rproc_parse_memory_regions(struct sunxi_rproc *priv)
{
	struct device *dev = priv->dev;
	struct device_node *np = dev->of_node;
	int i, count;

	count = of_reserved_mem_region_count(np);
	if (count <= 0)
		return 0;
	if (count > SUNXI_RPROC_MAX_RMEM)
		return dev_err_probe(dev, -EINVAL, "too many memory-region entries (%d)\n",
				     count);

	/* memory-region[0] is the default DMA pool for vrings and vdev buffers */
	if (!of_reserved_mem_device_init(dev))
		priv->has_reserved_mem = true;

	for (i = 0; i < count; i++) {
		struct sunxi_rproc_rmem *rmem = &priv->rmem[i];
		struct resource res;
		int ret;

		ret = of_reserved_mem_region_to_resource(np, i, &res);
		if (ret)
			return dev_err_probe(dev, ret, "invalid memory-region %d\n", i);

		if (of_property_read_string_index(np, "memory-region-names", i, &rmem->name))
			rmem->name = devm_kasprintf(dev, GFP_KERNEL, "rmem%d", i);
		if (!rmem->name)
			return -ENOMEM;

		rmem->phys = res.start;
		rmem->size = resource_size(&res);

		/* A pool placed on the core's SRAM reuses the existing mapping */
		if (priv->r_sram1_va && rmem->phys == priv->r_sram1_phys &&
		    rmem->size <= priv->r_sram1_size) {
			rmem->va = (__force void *)priv->r_sram1_va;
			rmem->is_iomem = true;
		} else if (priv->r_sram_va && rmem->phys == priv->r_sram_phys &&
			   rmem->size <= priv->r_sram_size) {
			rmem->va = (__force void *)priv->r_sram_va;
			rmem->is_iomem = true;
		} else {
			/* Same attribute as the DMA pool, so no WB/WC alias */
			rmem->va = devm_memremap(dev, rmem->phys, rmem->size, MEMREMAP_WC);
			if (IS_ERR(rmem->va))
				return dev_err_probe(dev, PTR_ERR(rmem->va),
						     "failed to map %s\n", rmem->name);
		}

		if (!strcmp(rmem->name, "dram")) {
			priv->dram_va = rmem->va;
			priv->dram_phys = rmem->phys;
			priv->dram_size = rmem->size;
		} else if (!strcmp(rmem->name, "trace")) {
			priv->trace_va = rmem->va;
			priv->trace_phys = rmem->phys;
			priv->trace_size = rmem->size;
		}

		priv->num_rmem++;
	}

	return 0;
}
```

This keeps the overlays working:

- `cubie-a5e-rpmsg-sram.dtso` has one unnamed region at 0x072c0000. It is
  named `rmem0` and reuses the `r_sram1` mapping.
- `cubie-a5e-testPingRpmsg.dtso` has one unnamed DDR region. It is named
  `rmem0` and mapped WC.

**Check:** does anything (firmware resource table, `rproc_find_carveout_by_name`)
depend on the old node-name fallback? Grep the firmware tree under
`cubie-a5e/` for carveout names first. If something does, keep
`rmem_np->name` as the fallback name instead of `rmem%d`.

In probe, call `sunxi_rproc_parse_memory_regions(priv)` after
`sunxi_rproc_register_mem()`. On failure, `goto err_mem_release`, because
`of_reserved_mem_device_init()` may already have succeeded.

In `sunxi_rproc_prepare()`, allocate the entries *before* touching
hardware. Free them on any error path, and add them only once everything
succeeded. A prepare failure gets no `rproc_resource_cleanup()` from the
core (see `rproc_fw_boot()`: prepare failure → `disable_iommu`), so
nothing may be left on the list.

```c
	struct rproc_mem_entry *mem[SUNXI_RPROC_MAX_RMEM];
	unsigned int i;
	int ret;

	for (i = 0; i < priv->num_rmem; i++) {
		const struct sunxi_rproc_rmem *rmem = &priv->rmem[i];

		mem[i] = rproc_mem_entry_init(priv->dev, rmem->va, (dma_addr_t)rmem->phys,
					      rmem->size, (u32)rmem->phys, NULL, NULL,
					      "%s", rmem->name);
		if (!mem[i]) {
			while (i--)
				kfree(mem[i]);
			return -ENOMEM;
		}
		mem[i]->is_iomem = rmem->is_iomem;
	}

	/* ... existing reset/clock/remap/memset sequence; the first failure
	 * now jumps to err_free_mem instead of returning ...
	 */

	/* rproc_resource_cleanup() frees rproc->carveouts after every run */
	for (i = 0; i < priv->num_rmem; i++)
		rproc_add_carveout(rproc, mem[i]);

	return 0;

	/* ... existing unwind labels ... */
err_assert_cfg:
	reset_control_assert(priv->rst_cfg);
err_free_mem:
	for (i = 0; i < priv->num_rmem; i++)
		kfree(mem[i]);
	return ret;
```

- Add `#include <linux/slab.h>`.
- The KUnit fixtures leave `num_rmem == 0` and `priv->dev == NULL`, so the
  existing `test_prepare_*` cases are unaffected. Confirm by running them.
- Add a test: set `num_rmem = 1` with a fake entry, call `prepare()`, check
  that `rproc->carveouts` has 1 entry, then call
  `rproc_resource_cleanup()`-equivalent list cleanup and `prepare()` again,
  and check that the entry is back. This needs `INIT_LIST_HEAD(&ctx->rproc.carveouts)`
  in `create_test_ctx()`.

### 2.5 Remove undocumented DT fallbacks

**Problem.** `sunxi_rproc_register_mem()` looks up `"sram"` and
`"sram-for-cpux"`, which are not in the binding. The remap mapping has
two modes: a large page-aligned region gets offset 0x364 added, anything
else is used as-is.

**Fix.**

- Delete the `"sram"` and `"sram-for-cpux"` lookups.
- For `remap`, map the resource directly. The binding defines it as the
  4-byte register itself:
  `priv->remap_va = devm_ioremap(dev, res->start, resource_size(res));`
- Delete `SUNXI_REMAP_CTRL_OFFSET` if it is no longer used. The tests use
  `SUNXI_REMAP_SRAMA3_2_BIT`, so keep that.

### 2.6 Make the mailbox required upstream, and move UIO mode to a LOCAL commit

The upstream probe drops the `generic-uio` / `#mbox-cells` check and the
"standalone mode" fallbacks:

```c
	priv->tx_chan = mbox_request_channel_byname(&priv->cl, "tx");
	if (IS_ERR(priv->tx_chan)) {
		ret = dev_err_probe(dev, PTR_ERR(priv->tx_chan),
				    "failed to request tx mailbox\n");
		priv->tx_chan = NULL;
		goto err_mem_release;
	}

	priv->rx_chan = mbox_request_channel_byname(&priv->cl, "rx");
	if (IS_ERR(priv->rx_chan)) {
		ret = dev_err_probe(dev, PTR_ERR(priv->rx_chan),
				    "failed to request rx mailbox\n");
		priv->rx_chan = NULL;
		goto err_mbox_release;
	}
	...
err_mbox_release:
	sunxi_rproc_free_mbox(priv);
	cancel_work_sync(&priv->vq_work);
err_mem_release:
	if (priv->has_reserved_mem)
		of_reserved_mem_device_release(dev);
	return ret;
```

- Keep the `if (!priv->tx_chan) return;` in `kick()`.
  `test_kick_null_tx_chan_safe` depends on it.
- On `cubie-linux-7.1` only, add a separate commit
  `LOCAL: remoteproc: sunxi: allow standalone mode when msgbox is bound to UIO`.
  It restores the skip, using `of_property_present(mb_node, "#mbox-cells")`
  rather than `of_property_read_bool()`. Never export this commit into
  `v3/`.

### 2.7 Drop the `parent` and `msgbox` clocks/resets

- Delete `clk_parent`, `clk_msgbox` and `rst_msgbox`: the struct fields,
  the probe getters, the prepare/unprepare enable/disable, and the error
  labels.
- The RV msgbox bank clock/reset now belongs to the msgbox node (1.2).
  `parent` is not a hardware input. If a parent is needed, use
  `assigned-clock-parents` in DT.
- Make `bus`/`core` clocks and `cfg`/`core` resets required, using
  `devm_clk_get()` / `devm_reset_control_get_exclusive()`. Keep `sram`
  optional to match the binding (3.2).
- `clk_prepare_enable(NULL)` and `reset_control_*(NULL)` are no-ops, so the
  `if (priv->clk_x)` guards around optional handles can go.

### 2.8 Smaller cleanups

- `priv->cfg ? priv->cfg : &sun55i_riscv_cfg` appears in every op. Probe
  always sets `cfg`, so use `priv->cfg` directly. Keep the probe fallback,
  or drop it, since `.data` is always set.
- `sunxi_rproc_da_to_va()` repeats the `len == 0 || da > U64_MAX - len`
  check that `sunxi_rproc_da_to_sys()` already does. Keep one of them.
- Demote to `dev_dbg` (or delete) the `dev_info` calls in `start()`,
  `stop()`, `register_mem()` ("Memory resources: ..."), the carveout
  registration and the end of probe.
- KUnit visibility: same conversion as 1.4 for `sun55i_riscv_cfg`,
  `sunxi_rproc_ops`, `sunxi_rproc_prepare/unprepare/start/stop/kick/da_to_va`.
  Add `MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");` to `sunxi_rproc_test.c`.
- Provenance: the copyright line says Allwinner and there is an Allwinner
  `MODULE_AUTHOR`. State in the commit message which vendor driver (with
  its Tina SDK path and license) the code derives from, and mirror the same
  "Based on" line in `sun55i-msgbox.c`.

### 2.9 Remove A733 from the rproc tests

`sunxi_rproc_test.c` defines a test-only `a733_e902_cfg` and two cases:
`test_da_to_va_a733_sram_a2_layout` and `test_start_a733_mode1_and_mode2_bootaddr`.
They test a config the driver never matches (no compatible, no ATT, E902
rather than E906). Reviewers will ask why the tests cover hardware the
driver does not support.

- Delete `a733_e902_cfg` and both cases from the upstream series. Move
  them to the future A733 series, or into a `LOCAL:` commit on
  `cubie-linux-7.1`.
- Fix the commit message counts. Count the `KUNIT_CASE(` lines after
  editing: 6/7 says 34 today but has 33, and will be 31 after this
  removal plus any test added in 2.3.

---

## 3. Bindings

### 3.1 `Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml`

Changes:

- Use the SPDX form `(GPL-2.0-only OR BSD-2-Clause)`.
- Make `compatible` a single const. `t527` is the same die, and `a733` has
  no driver or DTS.
- Describe 4 clocks and 4 resets, and require `resets`.
- Remove Samuel Holland from `maintainers` unless he agreed to it in
  writing.

```yaml
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
%YAML 1.2
---
$id: http://devicetree.org/schemas/mailbox/allwinner,sun55i-a523-msgbox.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: Allwinner A523 Message Box

maintainers:
  - Tim Michals <tcmichals@gmail.com>

description:
  The message box on the Allwinner A523, A527 and T527 (sun55i) SoCs links
  four processors - the Cortex-A55 cluster, the HiFi4 DSP, the CPUS power
  management core and the XuanTie E906 RISC-V core. Each processor has its
  own register bank holding 8-entry receive FIFOs for messages from the
  other three, and a sender writes into the receiver's bank. The host side
  therefore uses all four banks and exposes 12 channels, 4 per remote
  processor.

properties:
  compatible:
    const: allwinner,sun55i-a523-msgbox

  reg:
    items:
      - description: Cortex-A55 host bank
      - description: HiFi4 DSP bank
      - description: CPUS bank
      - description: XuanTie RISC-V bank

  reg-names:
    items:
      - const: arm
      - const: dsp
      - const: cpus
      - const: rv

  clocks:
    items:
      - description: Bus clock of the Cortex-A55 host bank
      - description: Bus clock of the HiFi4 DSP bank
      - description: Bus clock of the CPUS bank
      - description: Bus clock of the XuanTie RISC-V bank

  resets:
    items:
      - description: Reset of the Cortex-A55 host bank
      - description: Reset of the HiFi4 DSP bank
      - description: Reset of the CPUS bank
      - description: Reset of the XuanTie RISC-V bank

  interrupts:
    minItems: 1
    items:
      - description: Cortex-A55 host bank interrupt
      - description: HiFi4 DSP bank interrupt
      - description: CPUS bank interrupt
      - description: XuanTie RISC-V bank interrupt

  interrupt-names:
    minItems: 1
    items:
      - const: arm
      - const: dsp
      - const: cpus
      - const: rv

  '#mbox-cells':
    const: 1
    description:
      Host channel number. Channels 0-3 reach the CPUS, 4-7 the DSP and
      8-11 the RISC-V core.

required:
  - compatible
  - reg
  - reg-names
  - clocks
  - resets
  - interrupts
  - '#mbox-cells'

additionalProperties: false

examples:
  - |
    #include <dt-bindings/clock/sun55i-a523-ccu.h>
    #include <dt-bindings/clock/sun55i-a523-mcu-ccu.h>
    #include <dt-bindings/clock/sun55i-a523-r-ccu.h>
    #include <dt-bindings/interrupt-controller/arm-gic.h>
    #include <dt-bindings/reset/sun55i-a523-ccu.h>
    #include <dt-bindings/reset/sun55i-a523-mcu-ccu.h>
    #include <dt-bindings/reset/sun55i-a523-r-ccu.h>

    mailbox@3003000 {
        compatible = "allwinner,sun55i-a523-msgbox";
        reg = <0x03003000 0x1000>,
              <0x07120000 0x1000>,
              <0x07094000 0x1000>,
              <0x07136000 0x1000>;
        reg-names = "arm", "dsp", "cpus", "rv";
        clocks = <&ccu CLK_BUS_MSGBOX>,
                 <&mcu_ccu CLK_BUS_MCU_DSP_MSGBOX>,
                 <&r_ccu CLK_BUS_R_MSGBOX>,
                 <&mcu_ccu CLK_BUS_MCU_RISCV_MSGBOX>;
        resets = <&ccu RST_BUS_MSGBOX>,
                 <&mcu_ccu RST_BUS_MCU_DSP_MSGBOX>,
                 <&r_ccu RST_BUS_R_MSGBOX>,
                 <&mcu_ccu RST_BUS_MCU_RISCV_MSGBOX>;
        interrupts = <GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 1 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 181 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 174 IRQ_TYPE_LEVEL_HIGH>;
        interrupt-names = "arm", "dsp", "cpus", "rv";
        #mbox-cells = <1>;
    };
```

### 3.2 Rename and rewrite the rproc binding

```
git mv Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml \
       Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml
```

The file name must match the compatible. Update `$id`, the MAINTAINERS
`F:` line, the commit subject and the cover letter.

Changes:

- Describe the hardware, not the driver.
- Drop the `parent` and `msgbox` clocks and the `msgbox` reset.
- Give each clock and reset an item description.
- Make `mboxes` exactly two items.
- Drop the driver default from `firmware-name`.
- Remove Jernej Skrabec and Samuel Holland from `maintainers` unless they
  agreed.

```yaml
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
%YAML 1.2
---
$id: http://devicetree.org/schemas/remoteproc/allwinner,sun55i-a523-rproc.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: Allwinner A523 XuanTie E906 RISC-V Remote Processor

maintainers:
  - Tim Michals <tcmichals@gmail.com>

description:
  The Allwinner A523, A527 and T527 (sun55i) SoCs contain a T-Head XuanTie
  E906 RISC-V core next to the Cortex-A55 cluster. The core runs from a
  dedicated 256 KiB SRAM, can also use a second 256 KiB SRAM that is
  switched to it through a remap register, and can access DRAM. Its reset
  vector is set in a register of its configuration block, and it exchanges
  notifications with the host through the CPUX message box.

properties:
  compatible:
    const: allwinner,sun55i-a523-rproc

  reg:
    items:
      - description: Configuration block, including the boot address register
      - description: Dedicated SRAM space 0
      - description: Switchable SRAM space 1
      - description: SRAM remap control register

  reg-names:
    items:
      - const: cfg
      - const: r_sram
      - const: r_sram1
      - const: remap

  clocks:
    minItems: 2
    items:
      - description: Configuration block bus clock
      - description: RISC-V core clock
      - description: MCU public SRAM bus clock

  clock-names:
    minItems: 2
    items:
      - const: bus
      - const: core
      - const: sram

  resets:
    minItems: 2
    items:
      - description: Configuration block reset
      - description: RISC-V core reset
      - description: MCU public SRAM reset

  reset-names:
    minItems: 2
    items:
      - const: cfg
      - const: core
      - const: sram

  mboxes:
    items:
      - description: Receive channel, RISC-V core to host
      - description: Transmit channel, host to RISC-V core

  mbox-names:
    items:
      - const: rx
      - const: tx

  firmware-name:
    maxItems: 1

  memory-region:
    minItems: 1
    items:
      - description: Virtio vring and buffer pool
      - description: Firmware code and data in DRAM
      - description: Trace buffer

  memory-region-names:
    minItems: 1
    items:
      - const: vram
      - const: dram
      - const: trace

  interrupts:
    maxItems: 1
    description: Crash notification from the RISC-V core

  interrupt-names:
    items:
      - const: crash

required:
  - compatible
  - reg
  - reg-names
  - clocks
  - clock-names
  - resets
  - reset-names
  - mboxes
  - mbox-names

additionalProperties: false

examples:
  - |
    #include <dt-bindings/clock/sun55i-a523-mcu-ccu.h>
    #include <dt-bindings/reset/sun55i-a523-mcu-ccu.h>

    remoteproc@7130000 {
        compatible = "allwinner,sun55i-a523-rproc";
        reg = <0x07130000 0x1000>,
              <0x07280000 0x40000>,
              <0x072c0000 0x40000>,
              <0x07010364 0x4>;
        reg-names = "cfg", "r_sram", "r_sram1", "remap";
        clocks = <&mcu_ccu CLK_BUS_MCU_RISCV_CFG>,
                 <&mcu_ccu CLK_MCU_RISCV>,
                 <&mcu_ccu CLK_BUS_MCU_PUBSRAM>;
        clock-names = "bus", "core", "sram";
        resets = <&mcu_ccu RST_BUS_MCU_RISCV_CFG>,
                 <&mcu_ccu RST_BUS_MCU_RISCV_CORE>,
                 <&mcu_ccu RST_BUS_MCU_PUBSRAM>;
        reset-names = "cfg", "core", "sram";
        mboxes = <&msgbox 8>, <&msgbox 9>;
        mbox-names = "rx", "tx";
        firmware-name = "riscv-firmware.elf";
    };
```

**Open design question. Do not change the code for it; put it in the cover
letter.** `remap` at 0x07010364 is a register in the PRCM/R_CCU block (just
past r_ccu's `0x250` reg window), and the driver read-modify-writes it
without a lock shared with the CCU driver. Ask the maintainers whether they
want a syscon phandle, an R_CCU-exported regmap, or the sunxi SRAM claim
API instead.

---

## 4. DTS: `arch/arm64/boot/dts/allwinner/sun55i-a523.dtsi`

### 4.1 msgbox node

```dts
		msgbox: mailbox@3003000 {
			compatible = "allwinner,sun55i-a523-msgbox";
			reg = <0x03003000 0x1000>,
			      <0x07120000 0x1000>,
			      <0x07094000 0x1000>,
			      <0x07136000 0x1000>;
			reg-names = "arm", "dsp", "cpus", "rv";
			clocks = <&ccu CLK_BUS_MSGBOX>,
				 <&mcu_ccu CLK_BUS_MCU_DSP_MSGBOX>,
				 <&r_ccu CLK_BUS_R_MSGBOX>,
				 <&mcu_ccu CLK_BUS_MCU_RISCV_MSGBOX>;
			resets = <&ccu RST_BUS_MSGBOX>,
				 <&mcu_ccu RST_BUS_MCU_DSP_MSGBOX>,
				 <&r_ccu RST_BUS_R_MSGBOX>,
				 <&mcu_ccu RST_BUS_MCU_RISCV_MSGBOX>;
			interrupts = <GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>,
				     <GIC_SPI 1 IRQ_TYPE_LEVEL_HIGH>,
				     <GIC_SPI 181 IRQ_TYPE_LEVEL_HIGH>,
				     <GIC_SPI 174 IRQ_TYPE_LEVEL_HIGH>;
			interrupt-names = "arm", "dsp", "cpus", "rv";
			#mbox-cells = <1>;
		};
```

### 4.2 rproc node

```dts
			clocks = <&mcu_ccu CLK_BUS_MCU_RISCV_CFG>,
				 <&mcu_ccu CLK_MCU_RISCV>,
				 <&mcu_ccu CLK_BUS_MCU_PUBSRAM>;
			clock-names = "bus", "core", "sram";
			resets = <&mcu_ccu RST_BUS_MCU_RISCV_CFG>,
				 <&mcu_ccu RST_BUS_MCU_RISCV_CORE>,
				 <&mcu_ccu RST_BUS_MCU_PUBSRAM>;
			reset-names = "cfg", "core", "sram";
```

- Check that nothing else in the dtsi uses `RST_BUS_MCU_PUBSRAM`. The rproc
  gets it as exclusive, so another user would make probe fail.
- Update the 7/7 commit message, which currently lists the msgbox
  clock/reset for the rproc.

---

## 5. Cover letter: `v3/v3-0000-cover-letter.patch`

- Add the v2 lore link:
  `https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/`.
- Rewrite the teardown bullet. The current one says
  "free_irq -> mbox_free_channel -> cancel_work_sync -> rproc_del", which
  is wrong. Describe what the code now does: `rproc_del()`, then mailbox
  free, then work cancel. Say that the crash IRQ is devm-managed and
  disarmed in `stop()`.
- Replace the "crash_irq_enabled is protected by priv->lock" bullet with
  the atomic-bit design.
- Add bullets for these changes:
  - only the ARM interrupt is requested,
  - per-bank clocks and resets,
  - carveouts registered in `prepare()`,
  - exact `memory-region-names` matching,
  - the binding rename,
  - A733 removed.
- Remove the `platform_irq_count()` clamp bullet; the code is gone.
- Add the open `remap` question from 3.2.
- Fix the inconsistent bullet indentation in section 2/3 (6 spaces vs 5).
- Regenerate the diffstat.

---

## 6. Review tooling (`cubie-a5e/upstream-remoteproc/` and `.agents/`)

These are the reasons the bugs above were missed. Fix them in the
workspace repo; they are not part of the kernel series.

### 6.1 `scripts/run_adversarial_audit.py`

- **Invert R5.** `TEARDOWN_INVERSION_RPROC_DEL` and the Stage 2 check in
  `analyze_source_code()` currently flag `rproc_del()` *before*
  `mbox_free_channel()`, which is the correct order. Make them flag
  `mbox_free_channel()` before `rproc_del()` inside `sunxi_rproc_remove()`.
  Update the title to "mailbox freed before rproc_del() (kick after free)".
- **Add a rule `SLEEP_UNDER_SPINLOCK`.** Inside any
  `spin_lock*(...)` … `spin_unlock*(...)` span, flag `disable_irq(`,
  `synchronize_irq(`, `cancel_work_sync(`, `flush_work(`, `msleep(`,
  `mutex_lock(`, `kmalloc(...GFP_KERNEL` and `devm_`.
- **Add a rule `RPROC_CARVEOUT_IN_PROBE`.** Flag `rproc_add_carveout(` in
  a function reachable from probe but not from the `.prepare` op.
- **Add a rule `MEMREMAP_NULL_CHECK`.** Flag a `devm_memremap(` result that
  is tested with `if (x)` or `!x` instead of `IS_ERR`.
- **Add a rule `FOREIGN_IRQ_REQUEST`.** Flag a `request_irq` loop over
  `platform_irq_count()` when the handler only reads `regs[0]`.
- **Change the report wording.** Replace "NO STATIC FINDINGS" with
  "NO REGRESSIONS OF KNOWN FINDINGS". Delete the hard-coded "Legacy
  v2->v3 matrix" (rows R1, R5 and M1 are now wrong). Tier 0 of kreview
  regenerates that matrix from code.

### 6.2 `.agents/skills/kreview/SKILL.md`

Make this one canonical file. Delete the stale copy at
`cubie-a5e/.agents/skills/kreview/SKILL.md`, which lacks the review rules
and Tier 0. Copy it to `.claude/skills/kreview/SKILL.md` so Claude Code
also loads it.

- **Make the Sashiko protocol the main review.** Insert as the first step:
  "Load `<sashiko prompts>/kernel/review-core.md`,
  `technical-patterns.md`, `subsystem/subsystem.md` and every matched
  subsystem guide, and follow review-core.md. Load **every callee** of each
  modified function, as `agent/review.md` PHASE 2 requires." The prompts
  dir is
  `~/.local/share/sashiko/prompts/<hash>/kernel/`; resolve it with `ls`,
  because the hash changes on `sashiko init`.
- **Keep Tier 0** (prior-review regression matrix). Add: "a fix for a
  reviewer item is new code. Review it in both directions (what breaks if
  it runs earlier, and what breaks if later)."
- **Demote Tiers 1-3** to a supplementary checklist that runs after the
  Sashiko pass. Fix their wording:
  - Tier 2.2: "Are shared registers guarded by a lock, **and is nothing
    inside the lock able to sleep** (`disable_irq`, `synchronize_irq`,
    `cancel_work_sync`, `mutex_*`, GFP_KERNEL)?"
  - Tier 2.3: replace it with "remove() is the exact reverse of probe. For
    remoteproc, `rproc_del()` runs before the mailbox channels are freed,
    because shutdown and virtio removal still kick."
  - Tier 2.4: "Which region ends up in each `priv->*_va`? Can two DT names
    land in the same field?"
  - Tier 3: add "What does the framework free between runs?
    (`rproc_resource_cleanup()` frees carveouts on each shutdown, so
    carveouts belong in `.prepare()`.)" and "Does every requested IRQ have
    a handler that services its source?"
- **Add a Tier 5 "DT reviewer" checklist:**
  - the file name matches the compatible,
  - the description describes hardware rather than the driver,
  - no `parent` clocks,
  - every multi-item clocks/resets has item descriptions,
  - no compatible without a driver match or DTS user,
  - the SPDX form is `(GPL-2.0-only OR BSD-2-Clause)`,
  - maintainers listed have agreed,
  - every reg-name and property the driver reads is in the binding.
- **Verdict rule:** "CLEAN" is allowed only after a real
  `sashiko review <range>` run, with its output saved under `v<N>/`.

### 6.3 New Sashiko subsystem guide

Sashiko has no remoteproc or mailbox guide. Create
`upstream-remoteproc/scripts/prompts/subsystem/remoteproc.md` (keep it in
the repo, because `sashiko init` overwrites the prompts dir). Copy it into
`<sashiko prompts>/kernel/subsystem/`, and add this row to that dir's
`subsystem.md`:

```
| Remoteproc/Mailbox | drivers/remoteproc/, drivers/mailbox/, rproc_, mbox_, rproc_ops, mbox_chan_ops | remoteproc.md |
```

The guide should state these invariants, each with its "REPORT as bug" form
and the core function to load:

1. Carveouts from DT are registered in `.prepare()`, never only in probe
   (`rproc_resource_cleanup`, `stm32_rproc_prepare`).
2. `remove()`: `rproc_del()` comes before freeing mailbox channels, and
   `.kick()` can run during `rproc_del()` (`rproc_shutdown`,
   `rproc_remove_virtio_dev`).
3. `.stop()` and `.start()` run under `rproc->lock` (a mutex). No spinlock
   is needed to serialize them against each other, and nothing that sleeps
   may be called under one.
4. A `.prepare()` failure gets no `rproc_resource_cleanup()`
   (`rproc_fw_boot`). Anything added to rproc lists in prepare must be
   undone on its own error path.
5. Mailbox: with `txdone_poll`, `mbox_send_message()` keeps the *pointer*,
   so the payload must outlive the call.
   `mbox_controller_unregister()` calls `shutdown()` for every active
   channel. The ISR must not service channels whose RX IRQ is masked.
6. A controller requests only interrupts its own handler services.
7. A bank, FIFO or register the driver touches must have its clock and
   reset owned by the same DT node.

### 6.4 Docs

- `UPSTREAM_LESSONS_LEARNED.md` line ~173-187 and the checklist item at
  ~298 say "`rproc_del()` called last". Replace them with the corrected
  rule, explaining the v2 Sashiko question and why reversing the order was
  wrong. Add lessons for 2.1, 2.3, 2.4 and 1.1.
- `README.md`, `SUBMISSION_TRACKER.md` and `v3/AUDIT.md` must stop
  saying "Status: CLEAN" / "100% Clean" until 7.3 has been done.

---

## 7. Verification (all must pass; report actual output)

Run in `linux-cubie` on the upstream branch:

1. `make dt_binding_check DT_SCHEMA_FILES="allwinner,sun55i-a523-msgbox.yaml allwinner,sun55i-a523-rproc.yaml"`
   should report 0 warnings.
2. `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- dtbs_check` for
   `allwinner/sun55i-a527-cubie-a5e.dtb` should report no new warnings for
   msgbox or rproc.
3. `make ARCH=arm64 ... W=1 drivers/mailbox/ drivers/remoteproc/` with both
   drivers `=m` and `=y`, plus a `COMPILE_TEST` x86_64 build. Use
   `CONFIG_KUNIT=n` once, to catch `VISIBLE_IF_KUNIT` declaration errors.
4. KUnit (UML is excluded by Kconfig):
   `./tools/testing/kunit/kunit.py run --arch=arm64 --cross_compile=aarch64-linux-gnu- --kconfig_add CONFIG_SUN55I_MSGBOX=y --kconfig_add CONFIG_SUN55I_MSGBOX_KUNIT_TEST=y --kconfig_add CONFIG_REMOTEPROC=y --kconfig_add CONFIG_SUNXI_REMOTEPROC=y --kconfig_add CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST=y --kconfig_add CONFIG_ARCH_SUNXI=y`.
   Every case should pass. Report the counts.
5. `scripts/checkpatch.pl --strict` on all exported patches should report
   0 errors and 0 warnings. Justify any remaining CHECKs.
6. `sashiko review <base>..HEAD`, with the output saved to
   `v3/SASHIKO_REVIEW.txt`. Address or rebut each finding in `v3/AUDIT.md`.
7. The updated `run_adversarial_audit.py` should run clean on the fixed
   code. To prove the new rules work, it must flag the *old* code: run it
   with `--git-ref` on the pre-fix commit and confirm R5-inverted,
   SLEEP_UNDER_SPINLOCK, RPROC_CARVEOUT_IN_PROBE and MEMREMAP_NULL_CHECK
   all fire.

Then on the board (Cubie A5E). These require `cubie-linux-7.1` with the
same fixes plus the LOCAL commits. Build the kernel with
`CONFIG_PROVE_LOCKING=y` and `CONFIG_DEBUG_ATOMIC_SLEEP=y`:

8. With overlay `cubie-a5e-testPingRpmsg.dtso`, run start → ping → stop →
   start → ping. The second ping must work (this checks the carveout fix).
   Check `/sys/kernel/debug/remoteproc/remoteproc0/carveout_memories` after
   each start.
9. With `cubie-a5e-testCrash.dtso`, trigger a crash and let recovery run,
   then stop. Expect no lockdep or "sleeping function called from invalid
   context" splat.
10. Unbind the rproc while it is running:
    `echo 7130000.remoteproc > /sys/bus/platform/drivers/sunxi-rproc/unbind`.
    Expect no oops.
11. Check `/proc/interrupts` before and after 1000 pings. Only the ARM
    msgbox IRQ should be registered, and the spurious count should not grow.
12. With `cubie-a5e-uio.dtso`, the LOCAL standalone mode must still work.
13. Unbind and rebind the msgbox while no client is bound (see the 1.2 CPUS
    reset caveat).

Finally, re-export the patches with
`git format-patch -v3 --cover-letter -o ../cubie-a5e/upstream-remoteproc/v3/`,
re-apply the cover letter edits, and run
`scripts/verify_patchset.sh`. Leave `linux-cubie` on `cubie-linux-7.1`
when done.

## Deliverable

A short report covering:

- which items were applied and in which commit,
- the verification output for each step in section 7 (paste the failures
  verbatim),
- any item you skipped or changed, and why,
- the answers to the open questions: the carveout name fallback (2.4), the
  CPUS reset (1.2) and the RST_BUS_MCU_PUBSRAM sharing check (4.2).
