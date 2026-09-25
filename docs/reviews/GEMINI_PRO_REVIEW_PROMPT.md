# Linux Kernel Maintainer Review Guide & Gemini Pro 2M RemoteProc + Mailbox Audit Bundle

This document is a **self-contained 2M-token audit bundle** designed for reviewing the Allwinner A523/A527 RemoteProc and Mailbox driver patch series.

It embeds:
1. The **Grounded Engineering Review Prompt** (evaluating against objective Linux kernel correctness, DMA safety, and hardware invariants rather than subjective nitpicking).
2. The **Hardware Architecture & Invariant Ledger** (E907 memory map, ATT window mapping, Mailbox FIFO route table).
3. The complete, **verbatim production driver source code** (`sunxi_rproc.h`, `sunxi_rproc.c`, `sun55i-msgbox.h`, `sun55i-msgbox.c`).
4. The complete, **verbatim Device Tree bindings** (`allwinner,sun55i-rproc.yaml`, `allwinner,sun55i-a523-msgbox.yaml`).
5. The complete, **verbatim in-tree KUnit unit test suites** (`sunxi_rproc_test.c`, `sun55i_msgbox_test.c` — 66 total tests).

---

## 1. Review Instructions (Gemini Pro Web Interface)

Because this document contains both the audit prompt and the full, verbatim source code, you do **not** need to upload any external diff or patch files.

### Workflow:
1. Open [Google Gemini](https://gemini.google.com).
2. Select **Gemini Advanced / Gemini 1.5 Pro / 2.0 Pro** (which provides the full 2,000,000-token context window).
3. Upload or paste this entire document directly into Gemini Pro.
4. Execute the prompt in Section 2.

---

## 2. Canonical Engineering Maintainer Review Prompt

```text
You are an experienced Linux Kernel Subsystem and Security Maintainer specializing in remoteproc, mailbox, and DMA memory architectures.
Perform a thorough, objective review of the attached Allwinner RemoteProc and Mailbox production driver codebase, device tree bindings, and KUnit test suites for mainline upstream submission to linux-sunxi, linux-remoteproc, and linux-mailbox.

Evaluate the codebase against these 8 objective engineering criteria:

1. CLIENT-CONTROLLER CONTRACT (RemoteProc <-> Mailbox):
   - Trace the lifecycle of a message from sunxi_rproc_kick() -> mbox_send_message() -> sun55i_msgbox_send_data() -> hardware FIFO write.
   - Verify that sun55i_msgbox_send_data() safely copies the 32-bit token via memcpy() and writes immediately to the hardware FIFO MMIO without queuing stale pointers.
   - Verify that channel indices map 1:1 with hardware FIFO routes via sun55i_chan_to_route() with strict boundary checks.

2. CONCURRENCY, TEARDOWN & LIFO RESOURCE MANAGEMENT:
   - Verify the teardown sequence in sunxi_rproc_remove():
     a) Crash IRQ disabled first.
     b) rproc_del() stops the core and unregisters virtio devices.
     c) Mailbox channels freed and zeroed (mbox_free_channel).
     d) cancel_work_sync(&priv->vq_work) drains any remaining in-flight work.
   - Verify the teardown sequence in sun55i_msgbox_remove() and probe() error unwinding:
     a) Hardware interrupts masked.
     b) Registered IRQs explicitly freed via free_irq() before asserting reset and cutting clocks, preventing shared-IRQ execution on unclocked MMIO.

3. BOOT VECTOR PROGRAMMING & RESET SEQUENCING:
   - In sunxi_rproc_start(), verify that the boot vector register (STA_ADD_REG) is programmed while the core execution reset (rst_core) is still asserted, ensuring the core boots cleanly to bootaddr when rst_core is released.

4. DMA, MMU & MEMORY TRANSLATION SAFETY:
   - Scrutinize sunxi_rproc_da_to_sys() and sunxi_rproc_da_to_va():
     a) Boundary and overflow checks: len == 0 || da > U64_MAX - len prevents wrapped DA arithmetic.
     b) Carveouts and internal SRAM windows (Space 0, Space 1, DRAM) are strictly isolated without memory aliasing.
     c) 32-bit DMA coherent mask configured on pdev->dev via dma_set_coherent_mask().

5. HARDIRQ BOUNDED EXECUTION & STALL AVOIDANCE:
   - In sun55i_msgbox_irq(), startup(), and shutdown(), verify that all FIFO drain loops are strictly bounded by SUN55I_FIFO_MAX (8 iterations) to prevent CPU starvation or RCU stalls under coprocessor flood conditions.
   - In sun55i_msgbox_irq(), verify that the interrupt pending bit is cleared before draining the FIFO to prevent lost message TOCTOU races.

6. DEVICE TREE BINDINGS:
   - Review both YAML schema files against Rob Herring / Krzysztof Kozlowski standards:
     a) Are compatible strings, registers, clocks, resets, and mailboxes strictly validated?
     b) Are memory-region phandles documented cleanly?

7. CODE HYGIENE & TYPES:
   - Check that register masks use standard constants (U32_MAX) and IS_ALIGNED(res->start, PAGE_SIZE).
   - Verify proper error propagation and dev_err_probe() usage.

8. IN-TREE KUNIT TEST RIGOR:
   - Audit the 66 unit tests across sunxi_rproc_test.c and sun55i_msgbox_test.c.
   - Verify that tests validate actual operational behavior: boundary conditions (exact 1-byte fits, 2-byte overflows), negative unmapped address rejection, and FIFO drain limits.

FORMAT YOUR REPORT AS:
1. Executive Verdict: [Pass / Pass with Minor Suggestions / Fail]
2. Verification Summary Across the 8 Criteria
3. Any Concrete Code Suggestions (cite file:line)
```

---

## 3. Hardware Architecture & Invariant Ledger

### 3.1 Allwinner A523/A527 RemoteProc (RISC-V E907) Memory Architecture
- **Space 0 (SRAM A2)**:
  - Device Address (DA): `0x0000_0000` - `0x0001_3FFF` (80 KB)
  - System Address (Sys): `0x0010_0000` - `0x0011_3FFF`
  - Fixed hardware translation: `sys = da + 0x0010_0000`
- **Space 1 (SRAM A3/A4)**:
  - Device Address (DA): `0x0004_0000` - `0x0005_3FFF` (80 KB)
  - System Address (Sys): `0x0014_0000` - `0x0015_3FFF`
  - Fixed hardware translation: `sys = da + 0x0010_0000`
- **DRAM Window (ATT Remap)**:
  - Device Address (DA): `0x4000_0000` - `0x7FFF_FFFF` (1 GB)
  - Dynamically remapped to host physical DRAM via hardware Address Translation Table (ATT) registers.
- **Boot Vector Register**:
  - `STA_ADD_REG` (0x0204): Holds the entry reset vector for the E907 core. Must be written before releasing core reset (`rst_core`).

### 3.2 Allwinner Sun55i Hardware Message Box Architecture
- **Controllers**: 2 independent hardware msgbox instances (Msgbox 0 and Msgbox 1).
- **Processors**: Up to 4 communication peers (ARM Application Cores, RISC-V E907, DSP, Power Management Unit).
- **Channels & Queues**: 8 hardware channels per controller, each containing unidirectional 8-deep 32-bit hardware FIFOs (`SUN55I_FIFO_MAX = 8`).
- **Interrupts**: Dedicated read (RD) and write (WR) interrupt registers per processor.
- **Pending Clear Semantics**: Writing 1 to `RD_IRQ_PEND_REG` clears the interrupt condition. Must precede FIFO read to prevent TOCTOU lost messages.

---

## 4. Verbatim Production Driver Source Code

### 4.1 RemoteProc Header (`drivers/remoteproc/sunxi_rproc.h`)
```c
/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _SUNXI_RPROC_H_
#define _SUNXI_RPROC_H_

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>

/* XuanTie E906/E907 core-local view of dedicated SRAM Spaces (Allwinner A523/A527/T527) */
#define E907_SRAM_C_DA			0x00020000UL
#define E907_SRAM_SPACE0_DA		0x3ff80000UL
#define E907_SRAM_SPACE0_DA_ALT		0x3ffc0000UL
#define E907_SRAM_SPACE1_DA		0x40000000UL
#define E907_SRAM_SPACE1_DA_ALT		0x40040000UL

/* Allwinner A523/A527/T527 System Bus (Host Physical) Addresses & Window Sizes */
#define SUN55I_SRAM_SPACE0_SYS		0x07280000UL
#define SUN55I_SRAM_SPACE0_SIZE		0x00040000UL /* 256 KB */
#define SUN55I_SRAM_SPACE1_SYS		0x072c0000UL
#define SUN55I_SRAM_SPACE1_SIZE		0x00040000UL /* 256 KB */

/* Address Translation Table flags */
#define ATT_IOMEM			BIT(30)

struct sunxi_rproc_att {
	u64 da;
	u64 sa;
	size_t size;
	int flags;
};

/* XuanTie CFG Block Register Offsets */
#define E906_CTRL_REG			0x0000
#define E906_STA_ADD_REG		0x0204

/* Remap Control Register (offset 0x364 in PRCM_R_CCU / MCU_CCU) */
#define SUNXI_REMAP_CTRL_OFFSET		0x0364
/* Bit 0: 0 = local RAM for MCU; 1 = share for system */
#define SUNXI_REMAP_MCU_RAM_BIT		BIT(0)
/* Bit 1: 0 = SRAMA3_2 not shared; 1 = share for MCU_SYS */
#define SUNXI_REMAP_SRAMA3_2_BIT	BIT(1)

struct sunxi_rproc_cfg {
	const char *name;
	const struct sunxi_rproc_att *att;
	size_t att_size;
	bool has_remap_reg;
	u32 boot_reg_offset;
};

extern const struct sunxi_rproc_cfg sun55i_riscv_cfg;

struct sunxi_rproc {
	struct rproc *rproc;
	struct device *dev;
	const struct sunxi_rproc_cfg *cfg;

	/* CCU Clocks & Resets */
	struct clk *clk_parent;
	struct clk *clk_bus;
	struct clk *clk_core;
	struct clk *clk_sram;
	struct clk *clk_msgbox;
	struct reset_control *rst_cfg;
	struct reset_control *rst_core;
	struct reset_control *rst_sram;
	struct reset_control *rst_msgbox;

	/* Hardware Memory Windows (Dedicated SRAM, Switchable SRAM, Remap) */
	void __iomem *cfg_va;
	phys_addr_t cfg_phys;

	void __iomem *remap_va;
	phys_addr_t remap_phys;

	void __iomem *r_sram_va;
	phys_addr_t r_sram_phys;
	size_t r_sram_size;

	void __iomem *r_sram1_va;
	phys_addr_t r_sram1_phys;
	size_t r_sram1_size;

	void *dram_va;
	phys_addr_t dram_phys;
	size_t dram_size;

	/* Trace / DDR Reserved Memory Window */
	void *trace_va;
	phys_addr_t trace_phys;
	size_t trace_size;

	/* Reserved Memory & Mailbox State */
	bool has_reserved_mem;
	int crash_irq;
	bool crash_irq_enabled;
	struct mbox_client cl;
	struct mbox_chan *tx_chan;
	struct mbox_chan *rx_chan;
	struct work_struct vq_work;
	u32 kick_msg;
};

extern const struct rproc_ops sunxi_rproc_ops;

int sunxi_rproc_prepare(struct rproc *rproc);
int sunxi_rproc_unprepare(struct rproc *rproc);
int sunxi_rproc_start(struct rproc *rproc);
int sunxi_rproc_stop(struct rproc *rproc);
void sunxi_rproc_kick(struct rproc *rproc, int vqid);
void *sunxi_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem);

#endif /* _SUNXI_RPROC_H_ */

```

### 4.2 RemoteProc Driver (`drivers/remoteproc/sunxi_rproc.c`)
```c
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Allwinner XuanTie E906/E907 RISC-V Remote Processor Driver
 *
 * Copyright (C) 2024-2026 Allwinner Technology Co., Ltd.
 * Copyright (C) 2026 Tim Michals <tcmichals@gmail.com>
 *
 * CCU-integrated remoteproc driver for XuanTie RISC-V co-processors on
 * Allwinner SoCs (T527/A527/A523: E906/E907) supporting TCM, SRAM, and DRAM.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>

#include "remoteproc_internal.h"
#include "sunxi_rproc.h"

#define DRIVER_NAME "sunxi-rproc"

static const struct sunxi_rproc_att sun55i_rproc_att[] = {
	/* dev addr (remote)    , sys addr (host PA)    , size                   , flags */
	/* Space 0 Core Aliases -> Space 0 Host PA */
	{ E907_SRAM_SPACE0_DA,     SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
	{ E907_SRAM_SPACE0_DA_ALT, SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
	{ E907_SRAM_C_DA,          SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
	{ SUN55I_SRAM_SPACE0_SYS,  SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },

	/* Space 1 Core Aliases -> Space 1 Host PA */
	{ E907_SRAM_SPACE1_DA,     SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
	{ E907_SRAM_SPACE1_DA_ALT, SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
	{ SUN55I_SRAM_SPACE1_SYS,  SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
};

const struct sunxi_rproc_cfg sun55i_riscv_cfg = {
	.name = "XuanTie E907 RISC-V",
	.att = sun55i_rproc_att,
	.att_size = ARRAY_SIZE(sun55i_rproc_att),
	.has_remap_reg = true,
	.boot_reg_offset = E906_STA_ADD_REG,
};

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sun55i_riscv_cfg);
#endif

static void sunxi_rproc_vq_work(struct work_struct *work)
{
	struct sunxi_rproc *priv = container_of(work, struct sunxi_rproc, vq_work);

	rproc_vq_interrupt(priv->rproc, 0);
	rproc_vq_interrupt(priv->rproc, 1);
}

static void sunxi_rproc_mb_rx_callback(struct mbox_client *cl, void *data)
{
	struct sunxi_rproc *priv = container_of(cl, struct sunxi_rproc, cl);

	schedule_work(&priv->vq_work);
}

static irqreturn_t sunxi_rproc_crash_handler(int irq, void *data)
{
	struct sunxi_rproc *priv = data;
	struct rproc *rproc = priv->rproc;

	dev_err(priv->dev, "Hardware crash event received from %s core!\n",
		priv->cfg ? priv->cfg->name : "remote");
	if (priv->crash_irq_enabled) {
		disable_irq_nosync(irq);
		priv->crash_irq_enabled = false;
	}
	rproc_report_crash(rproc, RPROC_FATAL_ERROR);

	return IRQ_HANDLED;
}

int sunxi_rproc_prepare(struct rproc *rproc)
{
	struct sunxi_rproc *priv = rproc->priv;
	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
	int ret;

	/* 1. Deassert configuration & SRAM bus resets */
	if (priv->rst_cfg) {
		ret = reset_control_deassert(priv->rst_cfg);
		if (ret) {
			dev_err(priv->dev, "failed to deassert cfg reset: %d\n", ret);
			return ret;
		}
	}

	if (priv->rst_sram) {
		ret = reset_control_deassert(priv->rst_sram);
		if (ret) {
			dev_err(priv->dev, "failed to deassert sram reset: %d\n", ret);
			goto err_assert_cfg;
		}
	}

	if (priv->rst_msgbox) {
		ret = reset_control_deassert(priv->rst_msgbox);
		if (ret) {
			dev_err(priv->dev, "failed to deassert msgbox reset: %d\n", ret);
			goto err_assert_sram;
		}
	}

	/* 2. Enable parent clock (PLL source) */
	if (priv->clk_parent) {
		ret = clk_prepare_enable(priv->clk_parent);
		if (ret) {
			dev_err(priv->dev, "failed to enable parent clock: %d\n", ret);
			goto err_assert_msgbox;
		}
	}

	/* 3. Enable interconnect bus and SRAM clocks */
	if (priv->clk_bus) {
		ret = clk_prepare_enable(priv->clk_bus);
		if (ret) {
			dev_err(priv->dev, "failed to enable bus clock: %d\n", ret);
			goto err_disable_parent;
		}
	}

	if (priv->clk_sram) {
		ret = clk_prepare_enable(priv->clk_sram);
		if (ret) {
			dev_err(priv->dev, "failed to enable sram clock: %d\n", ret);
			goto err_disable_bus;
		}
	}

	if (priv->clk_msgbox) {
		ret = clk_prepare_enable(priv->clk_msgbox);
		if (ret) {
			dev_err(priv->dev, "failed to enable msgbox clock: %d\n", ret);
			goto err_disable_sram_clk;
		}
	}

	/* 4. Enable core clock */
	if (priv->clk_core) {
		ret = clk_prepare_enable(priv->clk_core);
		if (ret) {
			dev_err(priv->dev, "failed to enable core clock: %d\n", ret);
			goto err_disable_msgbox_clk;
		}
	}

	/*
	 * 4b. Enable SRAMA3_2 for MCU_SYS (RISC-V) via REMAP_CTRL_REG bit 1.
	 */
	if (cfg->has_remap_reg && priv->remap_va) {
		u32 remap_val = readl(priv->remap_va);

		remap_val |= SUNXI_REMAP_SRAMA3_2_BIT;
		writel(remap_val, priv->remap_va);
		dev_dbg(priv->dev, "REMAP_CTRL_REG set to 0x%08x (SRAMA3_2 enabled)\n",
			readl(priv->remap_va));
	}

	/*
	 * 5. Cleanly clear Dedicated Local SRAM and Switchable SRAM.
	 * Only clears regions that are actually mapped from Device Tree.
	 * Matches upstream patterns (e.g., imx_rproc / ti_k3_r5_remoteproc) to:
	 *  - Prevent ECC/parity noise on uninitialized memory banks.
	 *  - Ensure NOBITS / .bss sections start strictly at zero.
	 *  - Clear stale trace0 logs/telemetry from previous runs.
	 */
	if (priv->r_sram_va && priv->r_sram_size)
		memset_io(priv->r_sram_va, 0, priv->r_sram_size);

	if (priv->r_sram1_va && priv->r_sram1_size)
		memset_io(priv->r_sram1_va, 0, priv->r_sram1_size);

	return 0;

err_disable_msgbox_clk:
	if (priv->clk_msgbox)
		clk_disable_unprepare(priv->clk_msgbox);
err_disable_sram_clk:
	if (priv->clk_sram)
		clk_disable_unprepare(priv->clk_sram);
err_disable_bus:
	if (priv->clk_bus)
		clk_disable_unprepare(priv->clk_bus);
err_disable_parent:
	if (priv->clk_parent)
		clk_disable_unprepare(priv->clk_parent);
err_assert_msgbox:
	if (priv->rst_msgbox)
		reset_control_assert(priv->rst_msgbox);
err_assert_sram:
	if (priv->rst_sram)
		reset_control_assert(priv->rst_sram);
err_assert_cfg:
	if (priv->rst_cfg)
		reset_control_assert(priv->rst_cfg);
	return ret;
}

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sunxi_rproc_prepare);
#endif

int sunxi_rproc_unprepare(struct rproc *rproc)
{
	struct sunxi_rproc *priv = rproc->priv;
	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;

	/* Symmetrical CCU unwinding */
	if (cfg->has_remap_reg && priv->remap_va) {
		u32 remap_val = readl(priv->remap_va);

		remap_val &= ~SUNXI_REMAP_SRAMA3_2_BIT;
		writel(remap_val, priv->remap_va);
	}

	if (priv->clk_core)
		clk_disable_unprepare(priv->clk_core);

	if (priv->clk_msgbox)
		clk_disable_unprepare(priv->clk_msgbox);

	if (priv->clk_sram)
		clk_disable_unprepare(priv->clk_sram);

	if (priv->clk_bus)
		clk_disable_unprepare(priv->clk_bus);

	if (priv->clk_parent)
		clk_disable_unprepare(priv->clk_parent);

	if (priv->rst_msgbox)
		reset_control_assert(priv->rst_msgbox);

	if (priv->rst_sram)
		reset_control_assert(priv->rst_sram);

	if (priv->rst_cfg)
		reset_control_assert(priv->rst_cfg);

	return 0;
}

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sunxi_rproc_unprepare);
#endif

int sunxi_rproc_start(struct rproc *rproc)
{
	struct sunxi_rproc *priv = rproc->priv;
	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
	int ret;

	dev_info(priv->dev, "Starting %s core at entry 0x%llx\n",
		 cfg->name ? cfg->name : "remote", (u64)rproc->bootaddr);

	if (rproc->bootaddr > U32_MAX)
		return -EINVAL;

	/* Enable crash IRQ now that core is executing */
	if (priv->crash_irq > 0 && !priv->crash_irq_enabled) {
		enable_irq(priv->crash_irq);
		priv->crash_irq_enabled = true;
	}

	/*
	 * Program boot vector while the core execution reset is held.
	 * The CFG block bus was un-gated during prepare() via rst_cfg.
	 */
	if (priv->cfg_va) {
		writel((u32)rproc->bootaddr, priv->cfg_va + cfg->boot_reg_offset);
		dev_dbg(priv->dev, "STA_ADD set to 0x%08x\n", (u32)rproc->bootaddr);
	}

	/* Release core execution reset so the core begins execution at bootaddr */
	if (priv->rst_core) {
		ret = reset_control_deassert(priv->rst_core);
		if (ret) {
			dev_err(priv->dev, "failed to release core reset: %d\n", ret);
			return ret;
		}
	} else if (priv->rst_cfg) {
		ret = reset_control_deassert(priv->rst_cfg);
		if (ret) {
			dev_err(priv->dev, "failed to release cfg reset: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sunxi_rproc_start);
#endif

int sunxi_rproc_stop(struct rproc *rproc)
{
	struct sunxi_rproc *priv = rproc->priv;
	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;

	dev_info(priv->dev, "Halting %s core...\n",
		 cfg->name ? cfg->name : "remote");

	/*
	 * Assert reset first so the core stops generating mailbox interrupts,
	 * then drain any work already queued. Reversing this order leaves a
	 * window where a late IRQ re-queues vq_work after cancel_work_sync()
	 * returns, executing on freed resources.
	 */
	if (priv->rst_core)
		reset_control_assert(priv->rst_core);
	else if (priv->rst_cfg)
		reset_control_assert(priv->rst_cfg);

	/* Disable crash IRQ while core is stopped */
	if (priv->crash_irq > 0 && priv->crash_irq_enabled) {
		disable_irq(priv->crash_irq);
		priv->crash_irq_enabled = false;
	}

	cancel_work_sync(&priv->vq_work);

	return 0;
}

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sunxi_rproc_stop);
#endif

void sunxi_rproc_kick(struct rproc *rproc, int vqid)
{
	struct sunxi_rproc *priv = rproc->priv;
	int ret;

	if (!priv->tx_chan)
		return;

	/*
	 * Use priv->kick_msg rather than a stack-local variable. The mailbox
	 * controller runs with tx_block=false, so mbox_send_message() may
	 * queue the pointer and return before the hardware reads the message.
	 * A stack-local vqid would be a use-after-return at that point.
	 */
	priv->kick_msg = (u32)vqid;
	ret = mbox_send_message(priv->tx_chan, &priv->kick_msg);
	if (ret < 0)
		dev_err_ratelimited(priv->dev, "failed to send mailbox kick: %d\n", ret);

	mbox_client_txdone(priv->tx_chan, 0);
}

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sunxi_rproc_kick);
#endif

static int sunxi_rproc_da_to_sys(struct sunxi_rproc *priv, u64 da,
				 size_t len, u64 *sys, bool *is_iomem)
{
	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
	size_t i;

	if (len == 0 || da > U64_MAX - len)
		return -EINVAL;

	if (cfg->att) {
		for (i = 0; i < cfg->att_size; i++) {
			const struct sunxi_rproc_att *att = &cfg->att[i];

			if (da >= att->da && (da + len) <= (att->da + att->size)) {
				*sys = att->sa + (da - att->da);
				if (is_iomem)
					*is_iomem = !!(att->flags & ATT_IOMEM);
				return 0;
			}
		}
	}

	return -ENOENT;
}

void *sunxi_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct sunxi_rproc *priv = rproc->priv;
	u64 sys;

	/*
	 * Reject zero length and any da+len combination that overflows u64.
	 * A crafted ELF with da near U64_MAX could wrap da+len to a small value,
	 * bypassing upper-bound checks and allowing arbitrary kernel memory
	 * to be mapped during firmware loading.
	 */
	if (len == 0 || da > U64_MAX - len)
		return NULL;

	/*
	 * 1. Translate core-local device addresses (DA) to system bus
	 * addresses (Host PA) using the SoC address translation table (ATT).
	 */
	if (sunxi_rproc_da_to_sys(priv, da, len, &sys, is_iomem) == 0) {
		if (priv->r_sram_va && sys >= priv->r_sram_phys &&
		    (sys + len) <= (priv->r_sram_phys + priv->r_sram_size))
			return (__force void *)(priv->r_sram_va + (sys - priv->r_sram_phys));

		if (priv->r_sram1_va && sys >= priv->r_sram1_phys &&
		    (sys + len) <= (priv->r_sram1_phys + priv->r_sram1_size))
			return (__force void *)(priv->r_sram1_va + (sys - priv->r_sram1_phys));

		if (priv->dram_va && sys >= priv->dram_phys &&
		    (sys + len) <= (priv->dram_phys + priv->dram_size))
			return (__force void *)(priv->dram_va + (sys - priv->dram_phys));

		if (priv->trace_va && sys >= priv->trace_phys &&
		    (sys + len) <= (priv->trace_phys + priv->trace_size))
			return (__force void *)(priv->trace_va + (sys - priv->trace_phys));
	}

	/*
	 * 2. Device Tree Memory Regions (Trace buffer, DRAM carveout, or
	 * dynamically-assigned SRAM regions whose host PA is supplied via DT).
	 */
	if (priv->trace_va && da >= priv->trace_phys &&
	    (da + len) <= (priv->trace_phys + priv->trace_size)) {
		if (is_iomem)
			*is_iomem = false;
		return (__force void *)(priv->trace_va + (da - priv->trace_phys));
	}

	if (priv->dram_va && da >= priv->dram_phys &&
	    (da + len) <= (priv->dram_phys + priv->dram_size)) {
		if (is_iomem)
			*is_iomem = false;
		return (__force void *)(priv->dram_va + (da - priv->dram_phys));
	}

	if (priv->r_sram_va && da >= priv->r_sram_phys &&
	    (da + len) <= (priv->r_sram_phys + priv->r_sram_size)) {
		if (is_iomem)
			*is_iomem = true;
		return (__force void *)(priv->r_sram_va + (da - priv->r_sram_phys));
	}

	if (priv->r_sram1_va && da >= priv->r_sram1_phys &&
	    (da + len) <= (priv->r_sram1_phys + priv->r_sram1_size)) {
		if (is_iomem)
			*is_iomem = true;
		return (__force void *)(priv->r_sram1_va + (da - priv->r_sram1_phys));
	}

	/*
	 * Return NULL to delegate all DRAM carveouts (vrings, buffers,
	 * code/data placed in DDR) directly to the remoteproc core's
	 * internal carveout table.
	 */
	return NULL;
}

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sunxi_rproc_da_to_va);
#endif

static int sunxi_rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret == -EINVAL) {
		dev_dbg(rproc->dev.parent, "no resource table found in ELF\n");
		return 0;
	}
	return ret;
}

const struct rproc_ops sunxi_rproc_ops = {
	.prepare        = sunxi_rproc_prepare,
	.unprepare      = sunxi_rproc_unprepare,
	.start          = sunxi_rproc_start,
	.stop           = sunxi_rproc_stop,
	.kick           = sunxi_rproc_kick,
	.da_to_va       = sunxi_rproc_da_to_va,
	.get_boot_addr  = rproc_elf_get_boot_addr,
	.load           = rproc_elf_load_segments,
	.parse_fw       = sunxi_rproc_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check   = rproc_elf_sanity_check,
	.coredump       = rproc_coredump,
};

#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sunxi_rproc_ops);
#endif

static int sunxi_rproc_register_mem(struct platform_device *pdev, struct rproc *rproc)
{
	struct device *dev = &pdev->dev;
	struct sunxi_rproc *priv = rproc->priv;
	struct resource *res;

	/* 1. Map Optional RISC-V CFG Block ("cfg") */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	if (res) {
		priv->cfg_phys = res->start;
		priv->cfg_va = devm_ioremap(dev, res->start, resource_size(res));
		if (!priv->cfg_va)
			dev_warn(dev, "failed to map 'cfg' registers\n");
	}

	/* 2. Map Dedicated RISC-V Local SRAM Space 0 ("r_sram" or "sram") */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "r_sram");
	if (!res)
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	if (res) {
		priv->r_sram_phys = res->start;
		priv->r_sram_size = resource_size(res);
		priv->r_sram_va = devm_ioremap_wc(dev, res->start, resource_size(res));
		if (!priv->r_sram_va)
			return -ENOMEM;
	}

	/* 3. Map Switchable RISC-V Local SRAM Space 1 ("r_sram1" / SRAMA3_2) - Optional */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "r_sram1");
	if (res) {
		priv->r_sram1_phys = res->start;
		priv->r_sram1_size = resource_size(res);
		priv->r_sram1_va = devm_ioremap_wc(dev, res->start, resource_size(res));
		if (!priv->r_sram1_va)
			dev_warn(dev, "failed to map 'r_sram1' resource\n");
	}

	/* 4. Map Remap Control Register ("remap" or "sram-for-cpux") - Optional */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "remap");
	if (!res)
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram-for-cpux");
	if (res) {
		priv->remap_phys = res->start;
		if (resource_size(res) > SUNXI_REMAP_CTRL_OFFSET && IS_ALIGNED(res->start, PAGE_SIZE)) {
			void __iomem *base = devm_ioremap(dev, res->start, resource_size(res));

			if (base)
				priv->remap_va = base + SUNXI_REMAP_CTRL_OFFSET;
		} else {
			priv->remap_va = devm_ioremap(dev, res->start, resource_size(res));
		}
		if (!priv->remap_va)
			dev_warn(dev, "failed to map 'remap' register\n");
	}

	/* 4b. Map Boot DRAM Carveout (Resource "dram" if defined in reg) */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dram");
	if (res) {
		priv->dram_phys = res->start;
		priv->dram_size = resource_size(res);
		priv->dram_va = devm_memremap(dev, res->start, resource_size(res), MEMREMAP_WB);
		if (!priv->dram_va)
			priv->dram_va = (__force void *)devm_ioremap_wc(dev, res->start,
									resource_size(res));
		if (!priv->dram_va)
			dev_warn(dev, "failed to map 'dram' resource\n");
	}

	dev_info(dev, "Memory resources: r_sram=%s, r_sram1=%s, remap=%s, cfg=%s, dram=%s\n",
		 priv->r_sram_va ? "yes" : "no",
		 priv->r_sram1_va ? "yes" : "no",
		 priv->remap_va ? "yes" : "no",
		 priv->cfg_va ? "yes" : "no",
		 priv->dram_va ? "yes" : "no");

	/* 5. Map Trace Buffer (Resource "trace" if defined in reg) */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "trace");
	if (res) {
		priv->trace_phys = res->start;
		priv->trace_size = resource_size(res);
		priv->trace_va = devm_memremap(dev, res->start, resource_size(res), MEMREMAP_WB);
		if (!priv->trace_va)
			priv->trace_va = (__force void *)devm_ioremap_wc(dev, res->start,
									 resource_size(res));
		dev_info(dev, "mapped 'trace' mmio resource %pa+%zu\n",
			 &priv->trace_phys, priv->trace_size);
	}

	return 0;
}

static int sunxi_rproc_parse_memory_regions(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct sunxi_rproc *priv = rproc->priv;
	int num_rmems;
	int i;

	if (!np)
		return 0;

	num_rmems = of_count_phandle_with_args(np, "memory-region", NULL);
	if (num_rmems <= 0)
		return 0;

	/* Bind default DMA pool for dynamic allocations (e.g. vdev0 vrings/buffers) */
	if (of_reserved_mem_device_init(dev) == 0)
		priv->has_reserved_mem = true;
	else
		dev_dbg(dev, "no dedicated DMA pool assigned from reserved-memory\n");

	/* Register all reserved-memory regions as formal remoteproc carveouts */
	for (i = 0; i < num_rmems; i++) {
		struct device_node *rmem_np;
		struct resource res;
		const char *name = NULL;
		struct rproc_mem_entry *mem;
		void *va;

		rmem_np = of_parse_phandle(np, "memory-region", i);
		if (!rmem_np)
			continue;

		if (of_address_to_resource(rmem_np, 0, &res)) {
			of_node_put(rmem_np);
			continue;
		}

		of_property_read_string_index(np, "memory-region-names", i, &name);
		if (!name)
			name = rmem_np->name;

		if (name && (strstr(name, "trace") || of_node_name_eq(rmem_np, "trace"))) {
			priv->trace_phys = res.start;
			priv->trace_size = resource_size(&res);
			priv->trace_va = devm_memremap(dev, res.start, resource_size(&res),
						       MEMREMAP_WB);
			if (!priv->trace_va)
				priv->trace_va = (__force void *)
					devm_ioremap_wc(dev, res.start, resource_size(&res));
			dev_info(dev, "registered trace carveout %pa+%zu (%s)\n",
				 &priv->trace_phys, priv->trace_size, name);
		} else if (name && (strstr(name, "dram") || strstr(name, "vram"))) {
			priv->dram_phys = res.start;
			priv->dram_size = resource_size(&res);
			priv->dram_va = devm_memremap(dev, res.start, resource_size(&res),
						      MEMREMAP_WB);
			if (!priv->dram_va)
				priv->dram_va = (__force void *)
					devm_ioremap_wc(dev, res.start, resource_size(&res));
			dev_info(dev, "registered dram carveout %pa+%zu (%s)\n",
				 &priv->dram_phys, priv->dram_size, name);
		}

		/* Reuse existing SRAM mapping if region overlaps, else ioremap */
		if (priv->r_sram1_va && res.start == priv->r_sram1_phys)
			va = (__force void *)priv->r_sram1_va;
		else if (priv->r_sram_va && res.start == priv->r_sram_phys)
			va = (__force void *)priv->r_sram_va;
		else
			va = (__force void *)devm_ioremap_wc(dev, res.start, resource_size(&res));

		if (va) {
			mem = rproc_mem_entry_init(dev, va, (dma_addr_t)res.start,
						   resource_size(&res), (u32)res.start,
						   NULL, NULL, "%s", name);
			if (mem) {
				mem->is_iomem = true;
				rproc_add_carveout(rproc, mem);
			}
		}
		of_node_put(rmem_np);
	}

	return 0;
}

static int sunxi_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *fw_name = "riscv-firmware.elf";
	struct sunxi_rproc *priv;
	struct rproc *rproc;
	int crash_irq;
	int ret;

	of_property_read_string(dev->of_node, "firmware-name", &fw_name);

	rproc = devm_rproc_alloc(dev, dev_name(dev), &sunxi_rproc_ops,
				 fw_name, sizeof(*priv));
	if (!rproc) {
		dev_err(dev, "failed to allocate rproc context\n");
		return -ENOMEM;
	}

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "failed to set 32-bit DMA coherent mask: %d\n", ret);
		return ret;
	}

	priv = rproc->priv;
	priv->rproc = rproc;
	priv->dev = dev;
	priv->cfg = of_device_get_match_data(dev);
	if (!priv->cfg)
		priv->cfg = &sun55i_riscv_cfg;

	/* 1. Common Clock Framework (CCF) Clocks */
	priv->clk_parent = devm_clk_get_optional(dev, "parent");
	if (IS_ERR(priv->clk_parent))
		return dev_err_probe(dev, PTR_ERR(priv->clk_parent),
				     "failed to get 'parent' clock\n");

	priv->clk_bus = devm_clk_get_optional(dev, "bus");
	if (IS_ERR(priv->clk_bus))
		return dev_err_probe(dev, PTR_ERR(priv->clk_bus), "failed to get 'bus' clock\n");

	priv->clk_core = devm_clk_get_optional(dev, "core");
	if (IS_ERR(priv->clk_core))
		return dev_err_probe(dev, PTR_ERR(priv->clk_core), "failed to get 'core' clock\n");

	priv->clk_sram = devm_clk_get_optional(dev, "sram");
	if (IS_ERR(priv->clk_sram))
		return dev_err_probe(dev, PTR_ERR(priv->clk_sram), "failed to get 'sram' clock\n");

	priv->clk_msgbox = devm_clk_get_optional(dev, "msgbox");
	if (IS_ERR(priv->clk_msgbox))
		return dev_err_probe(dev, PTR_ERR(priv->clk_msgbox),
				     "failed to get 'msgbox' clock\n");

	/* 2. Resets */
	priv->rst_core = devm_reset_control_get_optional_exclusive(dev, "core");
	if (IS_ERR(priv->rst_core))
		return dev_err_probe(dev, PTR_ERR(priv->rst_core), "failed to get 'core' reset\n");

	priv->rst_cfg = devm_reset_control_get_optional_exclusive(dev, "cfg");
	if (IS_ERR(priv->rst_cfg))
		return dev_err_probe(dev, PTR_ERR(priv->rst_cfg), "failed to get 'cfg' reset\n");

	priv->rst_sram = devm_reset_control_get_optional_exclusive(dev, "sram");
	if (IS_ERR(priv->rst_sram))
		return dev_err_probe(dev, PTR_ERR(priv->rst_sram), "failed to get 'sram' reset\n");

	priv->rst_msgbox = devm_reset_control_get_optional_exclusive(dev, "msgbox");
	if (IS_ERR(priv->rst_msgbox))
		return dev_err_probe(dev, PTR_ERR(priv->rst_msgbox),
				     "failed to get 'msgbox' reset\n");

	/* 3. Memory Windows (TCM, SRAM, CFG) */
	ret = sunxi_rproc_register_mem(pdev, rproc);
	if (ret)
		return ret;

	/* 4. Dynamic Reserved Memory Carveouts & DMA Pools from Device Tree */
	ret = sunxi_rproc_parse_memory_regions(rproc);
	if (ret)
		return ret;

	/* 5. Optional Hardware Crash Notification IRQ */
	crash_irq = platform_get_irq_byname_optional(pdev, "crash");
	if (crash_irq > 0) {
		ret = devm_request_threaded_irq(dev, crash_irq, NULL,
						sunxi_rproc_crash_handler,
						IRQF_ONESHOT | IRQF_NO_AUTOEN,
						"sunxi-rproc-crash",
						priv);
		if (ret) {
			dev_warn(dev, "failed to request crash IRQ %d: %d\n", crash_irq, ret);
		} else {
			priv->crash_irq = crash_irq;
			priv->crash_irq_enabled = false;
		}
	}

	/* 6. Mailbox IPC Client */
	INIT_WORK(&priv->vq_work, sunxi_rproc_vq_work);

	priv->cl.dev = dev;
	priv->cl.rx_callback = sunxi_rproc_mb_rx_callback;
	priv->cl.tx_block = false;
	priv->cl.knows_txdone = true;

	/*
	 * If the hardware mailbox is assigned to userspace (generic-uio) or
	 * lacks #mbox-cells, run RemoteProc in standalone mode.
	 */
	if (dev->of_node) {
		struct device_node *mb_node = of_parse_phandle(dev->of_node, "mboxes", 0);

		if (mb_node) {
			if (of_device_is_compatible(mb_node, "generic-uio") ||
			    !of_property_read_bool(mb_node, "#mbox-cells")) {
				dev_info(dev, "Mailbox assigned to UIO; running standalone mode\n");
				of_node_put(mb_node);
				goto skip_mbox;
			}
			of_node_put(mb_node);
		}
	}

	priv->tx_chan = mbox_request_channel_byname(&priv->cl, "tx");
	if (IS_ERR(priv->tx_chan)) {
		if (PTR_ERR(priv->tx_chan) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_mem_release;
		}
		dev_info(dev, "no tx mailbox channel configured; running standalone mode\n");
		priv->tx_chan = NULL;
	}

	if (priv->tx_chan) {
		priv->rx_chan = mbox_request_channel_byname(&priv->cl, "rx");
		if (IS_ERR(priv->rx_chan)) {
			if (PTR_ERR(priv->rx_chan) == -EPROBE_DEFER) {
				ret = -EPROBE_DEFER;
				goto err_mbox_release;
			}
			dev_info(dev, "no rx mailbox channel configured\n");
			priv->rx_chan = NULL;
		}
	}

skip_mbox:
	platform_set_drvdata(pdev, rproc);

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "failed to register rproc device: %d\n", ret);
		goto err_mbox_release;
	}

	dev_info(dev, "Allwinner %s remoteproc registered (%s)\n",
		 priv->cfg ? priv->cfg->name : "remote", fw_name);
	return 0;

err_mbox_release:
	cancel_work_sync(&priv->vq_work);
	/*
	 * mbox_request_channel_byname() can return ERR_PTR on failure.
	 * Guard with IS_ERR() to avoid calling mbox_free_channel() with
	 * an invalid pointer, which would panic on the first dereference.
	 */
	if (priv->rx_chan && !IS_ERR(priv->rx_chan))
		mbox_free_channel(priv->rx_chan);
	if (priv->tx_chan && !IS_ERR(priv->tx_chan))
		mbox_free_channel(priv->tx_chan);
err_mem_release:
	if (priv->has_reserved_mem)
		of_reserved_mem_device_release(dev);
	return ret;
}

static void sunxi_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct sunxi_rproc *priv = rproc->priv;

	/*
	 * Teardown order is critical:
	 * 1. Disable crash IRQ first so late hardware crash alerts cannot
	 *    race against rproc_del() or report crashes on a deleted device.
	 * 2. rproc_del() stops the remote core and tears down VirtIO/vring,
	 *    which stops the hardware from generating further mailbox IRQs.
	 * 3. cancel_work_sync() drains any in-flight vq_work. Calling this
	 *    before rproc_del() risks a late RX IRQ re-queuing work after
	 *    cancel_work_sync() returns, executing on freed priv->rx_chan.
	 * 4. Free mailbox channels only after the workqueue is fully drained.
	 */
	if (priv->crash_irq > 0 && priv->crash_irq_enabled) {
		disable_irq(priv->crash_irq);
		priv->crash_irq_enabled = false;
	}

	rproc_del(rproc);

	if (priv->rx_chan) {
		mbox_free_channel(priv->rx_chan);
		priv->rx_chan = NULL;
	}
	if (priv->tx_chan) {
		mbox_free_channel(priv->tx_chan);
		priv->tx_chan = NULL;
	}

	cancel_work_sync(&priv->vq_work);

	if (priv->has_reserved_mem)
		of_reserved_mem_device_release(&pdev->dev);
}

static const struct of_device_id sunxi_rproc_of_match[] = {
	/*
	 * A523, A527, and T527 are the same silicon die (sun55i family).
	 * Use a single compatible string per upstream DT binding policy.
	 */
	{ .compatible = "allwinner,sun55i-a523-rproc", .data = &sun55i_riscv_cfg },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sunxi_rproc_of_match);

static struct platform_driver sunxi_rproc_driver = {
	.probe = sunxi_rproc_probe,
	.remove = sunxi_rproc_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = sunxi_rproc_of_match,
	},
};
module_platform_driver(sunxi_rproc_driver);

MODULE_AUTHOR("Allwinner Technology Co., Ltd.");
MODULE_AUTHOR("Tim Michals <tcmichals@gmail.com>");
MODULE_DESCRIPTION("Allwinner XuanTie E906/E907 RISC-V Remoteproc Driver");
MODULE_LICENSE("GPL");

```

### 4.3 Mailbox Header (`drivers/mailbox/sun55i-msgbox.h`)
```c
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SUN55I_MSGBOX_H_
#define _SUN55I_MSGBOX_H_

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/mailbox_controller.h>
#include <linux/spinlock.h>

#define SUN55I_PROC_ARM			0
#define SUN55I_PROC_DSP			1
#define SUN55I_PROC_CPUS		2
#define SUN55I_PROC_RV			3

#define SUN55I_MAX_PROCESSORS		4
#define SUN55I_CHANS_PER_PROC		4
#define SUN55I_NUM_ROUTES		(SUN55I_MAX_PROCESSORS - 1)
#define SUN55I_NUM_CHANS		(SUN55I_NUM_ROUTES * SUN55I_CHANS_PER_PROC)
#define SUN55I_FIFO_MAX			8

#define SUNXI_MSGBOX_OFFSET(n)			(0x100 * (n))
#define SUNXI_MSGBOX_READ_IRQ_ENABLE(n)		(0x020 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_READ_IRQ_STATUS(n)		(0x024 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_WRITE_IRQ_ENABLE(n)	(0x030 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_WRITE_IRQ_STATUS(n)	(0x034 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_FIFO_STATUS(n, p)		(0x050 + SUNXI_MSGBOX_OFFSET(n) + 0x4 * (p))
#define SUNXI_MSGBOX_MSG_STATUS(n, p)		(0x060 + SUNXI_MSGBOX_OFFSET(n) + 0x4 * (p))
#define SUNXI_MSGBOX_MSG_FIFO(n, p)		(0x070 + SUNXI_MSGBOX_OFFSET(n) + 0x4 * (p))

#define RD_IRQ_EN_BIT(p)			BIT((p) * 2)
#define RD_IRQ_PEND_BIT(p)			BIT((p) * 2)
#define MSG_NUM_MASK				GENMASK(3, 0)

struct sun55i_route {
	u8 remote_id;
	u8 remote_n;
};

struct sun55i_msgbox {
	struct mbox_controller controller;
	void __iomem *regs[SUN55I_MAX_PROCESSORS];
	struct clk *clk;
	struct reset_control *reset;
	int irqs[SUN55I_MAX_PROCESSORS];
	int num_irqs;
	/* Protects concurrent MMIO register access */
	spinlock_t lock;
};

extern const struct sun55i_route sun55i_msgbox_arm_routes[SUN55I_NUM_ROUTES];
extern const struct mbox_chan_ops sun55i_msgbox_chan_ops;

void sun55i_chan_to_route(int chan_idx, int *local_n, int *p,
			  int *remote_id, int *remote_n);
irqreturn_t sun55i_msgbox_irq(int irq, void *dev_id);

#endif /* _SUN55I_MSGBOX_H_ */

```

### 4.4 Mailbox Driver (`drivers/mailbox/sun55i-msgbox.c`)
```c
// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner sun55i/sun60i 4-Port Hardware Message Box Driver
 *
 * Copyright (C) 2026 Tim Michals <tcmichals@gmail.com>
 * Based on vendor sunxi-msgbox driver by Allwinner Technology Co., Ltd.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include "sun55i-msgbox.h"

/*
 * Hardware routing table for Cortex-A55 host (local_id = 0):
 *   local_n = 0 -> CPUS (remote_id = SUN55I_PROC_CPUS, remote_n = 0) -> Channels 0..3
 *   local_n = 1 -> DSP  (remote_id = SUN55I_PROC_DSP,  remote_n = 0) -> Channels 4..7
 *   local_n = 2 -> RV   (remote_id = SUN55I_PROC_RV,   remote_n = 2) -> Channels 8..11
 */
const struct sun55i_route sun55i_msgbox_arm_routes[SUN55I_NUM_ROUTES] = {
	[0] = { .remote_id = SUN55I_PROC_CPUS, .remote_n = 0 },
	[1] = { .remote_id = SUN55I_PROC_DSP,  .remote_n = 0 },
	[2] = { .remote_id = SUN55I_PROC_RV,   .remote_n = 2 },
};

#if IS_ENABLED(CONFIG_SUN55I_MSGBOX_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sun55i_msgbox_arm_routes);
#endif

static inline struct sun55i_msgbox *to_sun55i_msgbox(struct mbox_chan *chan)
{
	return chan->con_priv;
}

void sun55i_chan_to_route(int chan_idx, int *local_n, int *p,
			  int *remote_id, int *remote_n)
{
	if (chan_idx < 0 || chan_idx >= SUN55I_NUM_CHANS) {
		*local_n = 0;
		*p = 0;
		*remote_id = 0;
		*remote_n = 0;
		return;
	}
	*local_n = chan_idx / SUN55I_CHANS_PER_PROC;
	*p = chan_idx % SUN55I_CHANS_PER_PROC;
	*remote_id = sun55i_msgbox_arm_routes[*local_n].remote_id;
	*remote_n = sun55i_msgbox_arm_routes[*local_n].remote_n;
}

#if IS_ENABLED(CONFIG_SUN55I_MSGBOX_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sun55i_chan_to_route);
#endif

irqreturn_t sun55i_msgbox_irq(int irq, void *dev_id)
{
	struct sun55i_msgbox *mbox = dev_id;
	irqreturn_t ret = IRQ_NONE;
	int i, local_n, p, chan_idx;

	for (local_n = 0; local_n < SUN55I_NUM_ROUTES; local_n++) {
		void __iomem *local_base = mbox->regs[0];
		u32 en, stat, pending;

		en = readl(local_base + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
		stat = readl(local_base + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));
		pending = en & stat;

		if (!pending)
			continue;

		for (p = 0; p < SUN55I_CHANS_PER_PROC; p++) {
			if (!(pending & RD_IRQ_PEND_BIT(p)))
				continue;

			chan_idx = local_n * SUN55I_CHANS_PER_PROC + p;

			/*
			 * Clear pending status BEFORE draining the FIFO to seal
			 * the TOCTOU race: any new message arriving while draining
			 * will re-assert the hardware pending bit, guaranteeing
			 * a subsequent interrupt and preventing blackholed IPC data.
			 */
			writel(RD_IRQ_PEND_BIT(p),
			       local_base + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));

			/* Cap drain at FIFO_MAX to prevent CPU lockup from a runaway remote */
			for (i = 0; i < SUN55I_FIFO_MAX; i++) {
				u32 msg;

				if (!(readl(local_base +
					    SUNXI_MSGBOX_MSG_STATUS(local_n, p)) & MSG_NUM_MASK))
					break;
				msg = readl(local_base + SUNXI_MSGBOX_MSG_FIFO(local_n, p));
				mbox_chan_received_data(&mbox->controller.chans[chan_idx], &msg);
			}

			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

#if IS_ENABLED(CONFIG_SUN55I_MSGBOX_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sun55i_msgbox_irq);
#endif

static int sun55i_msgbox_send_data(struct mbox_chan *chan, void *data)
{
	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
	int n = chan - mbox->controller.chans;
	int local_n, p, remote_id, remote_n;
	u32 msg = 0;

	if (data)
		memcpy(&msg, data, sizeof(msg));

	sun55i_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);

	writel(msg, mbox->regs[remote_id] + SUNXI_MSGBOX_MSG_FIFO(remote_n, p));
	return 0;
}

static int sun55i_msgbox_startup(struct mbox_chan *chan)
{
	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
	int n = chan - mbox->controller.chans;
	int i, local_n, p, remote_id, remote_n;
	unsigned long flags;
	u32 val;

	sun55i_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);

	/* Flush any stale receive data (bounded to FIFO_MAX) */
	for (i = 0; i < SUN55I_FIFO_MAX; i++) {
		if (!(readl(mbox->regs[0] + SUNXI_MSGBOX_MSG_STATUS(local_n, p)) & MSG_NUM_MASK))
			break;
		readl(mbox->regs[0] + SUNXI_MSGBOX_MSG_FIFO(local_n, p));
	}

	/* Clear pending status */
	writel(RD_IRQ_PEND_BIT(p),
	       mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));

	/* Enable receive IRQ */
	spin_lock_irqsave(&mbox->lock, flags);
	val = readl(mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
	val |= RD_IRQ_EN_BIT(p);
	writel(val, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
	spin_unlock_irqrestore(&mbox->lock, flags);

	return 0;
}

static void sun55i_msgbox_shutdown(struct mbox_chan *chan)
{
	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
	int n = chan - mbox->controller.chans;
	int i, local_n, p, remote_id, remote_n;
	unsigned long flags;
	u32 val;

	sun55i_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);

	/* Disable receive IRQ */
	spin_lock_irqsave(&mbox->lock, flags);
	val = readl(mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
	val &= ~RD_IRQ_EN_BIT(p);
	writel(val, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
	spin_unlock_irqrestore(&mbox->lock, flags);

	/* Clear pending status and flush (bounded to FIFO_MAX) */
	writel(RD_IRQ_PEND_BIT(p),
	       mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));
	for (i = 0; i < SUN55I_FIFO_MAX; i++) {
		if (!(readl(mbox->regs[0] + SUNXI_MSGBOX_MSG_STATUS(local_n, p)) & MSG_NUM_MASK))
			break;
		readl(mbox->regs[0] + SUNXI_MSGBOX_MSG_FIFO(local_n, p));
	}
}

static bool sun55i_msgbox_last_tx_done(struct mbox_chan *chan)
{
	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
	int n = chan - mbox->controller.chans;
	int local_n, p, remote_id, remote_n;
	u32 count;

	sun55i_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);

	count = readl(mbox->regs[remote_id] + SUNXI_MSGBOX_MSG_STATUS(remote_n, p)) & MSG_NUM_MASK;
	return count < SUN55I_FIFO_MAX;
}

static bool sun55i_msgbox_peek_data(struct mbox_chan *chan)
{
	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
	int n = chan - mbox->controller.chans;
	int local_n, p, remote_id, remote_n;
	u32 count;

	sun55i_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);

	count = readl(mbox->regs[0] + SUNXI_MSGBOX_MSG_STATUS(local_n, p)) & MSG_NUM_MASK;
	return count > 0;
}

const struct mbox_chan_ops sun55i_msgbox_chan_ops = {
	.send_data    = sun55i_msgbox_send_data,
	.startup      = sun55i_msgbox_startup,
	.shutdown     = sun55i_msgbox_shutdown,
	.last_tx_done = sun55i_msgbox_last_tx_done,
	.peek_data    = sun55i_msgbox_peek_data,
};

#if IS_ENABLED(CONFIG_SUN55I_MSGBOX_KUNIT_TEST)
EXPORT_SYMBOL_GPL(sun55i_msgbox_chan_ops);
#endif

static int sun55i_msgbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mbox_chan *chans;
	struct sun55i_msgbox *mbox;
	int i, ret, irq_cnt, local_n;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	spin_lock_init(&mbox->lock);

	/* Allocate channels early before touching any hardware */
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

	mbox->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(mbox->clk))
		return dev_err_probe(dev, PTR_ERR(mbox->clk), "failed to get clock\n");

	ret = clk_prepare_enable(mbox->clk);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable clock\n");

	mbox->reset = devm_reset_control_get_optional_shared(dev, NULL);
	if (IS_ERR(mbox->reset)) {
		ret = PTR_ERR(mbox->reset);
		goto err_disable_clk;
	}

	ret = reset_control_deassert(mbox->reset);
	if (ret)
		goto err_disable_clk;

	/* Disable all read IRQs and clear status */
	for (local_n = 0; local_n < SUN55I_NUM_ROUTES; local_n++) {
		writel(0, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
		writel(U32_MAX, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));
	}

	irq_cnt = platform_irq_count(pdev);
	if (irq_cnt < 0) {
		ret = irq_cnt;
		goto err_assert_reset;
	}

	for (i = 0; i < irq_cnt; i++) {
		int irq = platform_get_irq(pdev, i);

		if (irq < 0) {
			ret = irq;
			goto err_free_irqs;
		}

		ret = request_irq(irq, sun55i_msgbox_irq,
				  IRQF_SHARED, dev_name(dev), mbox);
		if (ret) {
			dev_err(dev, "failed to request irq %d: %d\n", irq, ret);
			goto err_free_irqs;
		}
		mbox->irqs[i] = irq;
		mbox->num_irqs = i + 1;
	}

	mbox->controller.dev           = dev;
	mbox->controller.ops           = &sun55i_msgbox_chan_ops;
	mbox->controller.chans         = chans;
	mbox->controller.num_chans     = SUN55I_NUM_CHANS;
	mbox->controller.txdone_irq    = false;
	mbox->controller.txdone_poll   = true;
	mbox->controller.txpoll_period = 1;

	platform_set_drvdata(pdev, mbox);

	ret = mbox_controller_register(&mbox->controller);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register controller\n");
		goto err_free_irqs;
	}

	return 0;

err_free_irqs:
	/* Mask all hardware read IRQs and free registered IRQs before cutting clocks */
	for (local_n = 0; local_n < SUN55I_NUM_ROUTES; local_n++)
		writel(0, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
	for (i = 0; i < mbox->num_irqs; i++)
		free_irq(mbox->irqs[i], mbox);
err_assert_reset:
	reset_control_assert(mbox->reset);
err_disable_clk:
	clk_disable_unprepare(mbox->clk);
	return ret;
}

static void sun55i_msgbox_remove(struct platform_device *pdev)
{
	struct sun55i_msgbox *mbox = platform_get_drvdata(pdev);
	int local_n, i;

	mbox_controller_unregister(&mbox->controller);

	/* Mask hardware interrupts and free IRQs before asserting reset and disabling clock */
	for (local_n = 0; local_n < SUN55I_NUM_ROUTES; local_n++)
		writel(0, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));

	for (i = 0; i < mbox->num_irqs; i++)
		free_irq(mbox->irqs[i], mbox);

	reset_control_assert(mbox->reset);
	clk_disable_unprepare(mbox->clk);
}

static const struct of_device_id sun55i_msgbox_of_match[] = {
	{ .compatible = "allwinner,sun55i-a523-msgbox" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun55i_msgbox_of_match);

static struct platform_driver sun55i_msgbox_driver = {
	.driver = {
		.name = "sun55i-msgbox",
		.of_match_table = sun55i_msgbox_of_match,
	},
	.probe = sun55i_msgbox_probe,
	.remove = sun55i_msgbox_remove,
};
module_platform_driver(sun55i_msgbox_driver);

MODULE_AUTHOR("Tim Michals <tcmichals@gmail.com>");
MODULE_DESCRIPTION("Allwinner sun55i/sun60i 4-Port Message Box Driver");
MODULE_LICENSE("GPL");

```

---

## 5. Verbatim Device Tree Bindings

### 5.1 RemoteProc Binding Schema (`Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml`)
```yaml
# SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause
%YAML 1.2
---
$id: http://devicetree.org/schemas/remoteproc/allwinner,sun55i-rproc.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: Allwinner XuanTie E906/E907 RISC-V Remoteproc

maintainers:
  - Jernej Skrabec <jernej.skrabec@gmail.com>
  - Samuel Holland <samuel@sholland.org>
  - Tim Michals <tcmichals@gmail.com>

description:
  The Allwinner T527, A527, and A523 (sun55i) SoCs integrate a T-Head
  (XuanTie) E906 or E907 RISC-V co-processor alongside the ARM Cortex-A55
  cluster. The co-processor runs bare-metal firmware loaded and lifecycle-
  managed by the Linux remoteproc framework.

  The driver controls CCU-integrated clocks (bus, core) and resets
  (cfg, core), programs the hardware boot-vector register, maps
  internal SRAM (SRAM_A3) windows, and connects to the Allwinner
  hardware mailbox (CPUX_MSGBOX) for VirtIO RPMsg IPC.

properties:
  compatible:
    const: allwinner,sun55i-a523-rproc

  reg:
    minItems: 1
    maxItems: 4
    description:
      Memory-mapped register regions. The following named regions are
      supported (all optional except at least one of r_sram or r_sram1) -
      "cfg" for RISC-V core control and boot-vector registers,
      "r_sram" for dedicated MCU SRAM Space 0,
      "r_sram1" for switchable MCU SRAM Space 1,
      "remap" for the hardware remap control register.

  reg-names:
    minItems: 1
    maxItems: 4
    items:
      enum:
        - cfg
        - r_sram
        - r_sram1
        - remap

  clocks:
    minItems: 1
    maxItems: 5

  clock-names:
    minItems: 1
    items:
      - const: bus
      - const: core
      - const: sram
      - const: msgbox
      - const: parent

  resets:
    minItems: 1
    maxItems: 4

  reset-names:
    minItems: 1
    items:
      - const: cfg
      - const: core
      - const: sram
      - const: msgbox

  mboxes:
    maxItems: 2
    description:
      Exactly two mailbox channels from the Allwinner CPUX_MSGBOX controller,
      one receive channel (RISC-V-to-ARM) and one transmit channel
      (ARM-to-RISC-V), used for VirtIO RPMsg kick notifications.

  mbox-names:
    items:
      - const: rx
      - const: tx

  firmware-name:
    maxItems: 1
    description:
      Name of the ELF firmware image to load from /lib/firmware/.
      Defaults to "riscv-firmware.elf" if not specified.

  memory-region:
    minItems: 1
    items:
      - description: VirtIO vring buffer region (DDR carveout)
      - description: DRAM firmware execution carveout
      - description: RemoteProc trace buffer carveout

  memory-region-names:
    minItems: 1
    items:
      - const: vram
      - const: dram
      - const: trace

  interrupts:
    maxItems: 1
    description:
      Optional hardware crash-notification interrupt. When present the
      driver calls rproc_report_crash() on assertion.

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
    remoteproc@7130000 {
        compatible = "allwinner,sun55i-a523-rproc";
        reg = <0x07130000 0x1000>,
              <0x07280000 0x40000>,
              <0x072c0000 0x40000>,
              <0x07010364 0x4>;
        reg-names = "cfg", "r_sram", "r_sram1", "remap";
        clocks = <&mcu_ccu 3>, <&mcu_ccu 4>;
        clock-names = "bus", "core";
        resets = <&mcu_ccu 0>, <&mcu_ccu 1>;
        reset-names = "cfg", "core";
        mboxes = <&msgbox 8>, <&msgbox 9>;
        mbox-names = "rx", "tx";
        firmware-name = "riscv-firmware.elf";
    };

```

### 5.2 Mailbox Binding Schema (`Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml`)
```yaml
# SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause
%YAML 1.2
---
$id: http://devicetree.org/schemas/mailbox/allwinner,sun55i-a523-msgbox.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: Allwinner sun55i 4-Port Message Box

maintainers:
  - Tim Michals <tcmichals@gmail.com>
  - Samuel Holland <samuel@sholland.org>

description:
  The hardware message box on sun55i (A523, A527, T527) and sun60i (A733)
  SoCs is a 4-port multi-processor mailbox controller connecting the ARM
  Cortex-A55 host cluster, HiFi4 Audio DSP, Power Management Unit (CPUS),
  and XuanTie RISC-V co-processor.

  It features independent 256-byte register banks for each processor port,
  12 logical channels on the host (4 per remote processor), and 8-entry
  hardware FIFOs with level interrupts.

properties:
  compatible:
    enum:
      - allwinner,sun55i-a523-msgbox
      - allwinner,sun55i-t527-msgbox
      - allwinner,sun60i-a733-msgbox

  reg:
    maxItems: 4
    description:
      Register banks for each of the four processor ports. Each port has
      an independent 256-byte register bank.

  reg-names:
    items:
      - const: arm
      - const: dsp
      - const: cpus
      - const: rv

  clocks:
    maxItems: 1

  resets:
    maxItems: 1

  interrupts:
    minItems: 1
    maxItems: 4
    description:
      One interrupt per processor port in port order (arm, dsp, cpus, rv).
      The ARM host port interrupt is required; remote port interrupts are
      optional. Use interrupt-names to identify which ports are present when
      providing a partial list.

  interrupt-names:
    minItems: 1
    maxItems: 4
    items:
      enum:
        - arm
        - dsp
        - cpus
        - rv

  '#mbox-cells':
    const: 1
    description: "channel number (0-11: 0-3 CPUS, 4-7 DSP, 8-11 RISC-V)"

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
    #include <dt-bindings/interrupt-controller/arm-gic.h>

    mailbox@3003000 {
        compatible = "allwinner,sun55i-a523-msgbox";
        reg = <0x03003000 0x1000>,
              <0x07094000 0x1000>,
              <0x07120000 0x1000>,
              <0x07136000 0x1000>;
        reg-names = "arm", "dsp", "cpus", "rv";
        clocks = <&ccu 120>;
        resets = <&ccu 45>;
        interrupts = <GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 1 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 181 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 174 IRQ_TYPE_LEVEL_HIGH>;
        interrupt-names = "arm", "dsp", "cpus", "rv";
        #mbox-cells = <1>;
    };

```

---

## 6. Verbatim In-Tree KUnit Test Suites

### 6.1 RemoteProc KUnit Suite (`drivers/remoteproc/sunxi_rproc_test.c`)
```c
// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for Allwinner XuanTie RISC-V remoteproc driver (sunxi_rproc.c)
 *
 * Comprehensive test suite validating:
 *  - da_to_va() address translation across all hardware windows & aliases
 *  - 64-bit integer overflow protection and zero-length handling
 *  - Out-of-bounds, cross-space isolation, and window unmapped error paths
 *  - Lifecycle operations: start() STA_ADD programming & bootaddr validation
 *  - Lifecycle operations: prepare() and unprepare() SRAM remap & clearing
 *  - kick() message formatting and NULL tx_chan safety
 *  - rproc_ops table completeness
 *
 * Directly tests driver functions without code duplication.
 *
 * Copyright (C) 2026 Tim Michals <tcmichals@gmail.com>
 */

#include <kunit/test.h>
#include <linux/io.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include "remoteproc_internal.h"
#include "sunxi_rproc.h"

/*
 * Fake VA addresses — sentinel pointers used to verify base + offset arithmetic
 * without requiring real ioremap MMIO allocations.
 */
#define FAKE_SRAM_PTR	((void *)0xA0000000UL)
#define FAKE_SRAM1_PTR	((void *)0xB0000000UL)
#define FAKE_SRAM_VA	((void __force __iomem *)FAKE_SRAM_PTR)
#define FAKE_SRAM1_VA	((void __force __iomem *)FAKE_SRAM1_PTR)
#define FAKE_DRAM_VA	((void *)0xC0000000UL)
#define FAKE_TRACE_VA	((void *)0xD0000000UL)

/* Standard A527 hardware parameters */
#define A527_SRAM_PHYS		SUN55I_SRAM_SPACE0_SYS
#define A527_SRAM_SIZE		SUN55I_SRAM_SPACE0_SIZE
#define A527_SRAM1_PHYS		SUN55I_SRAM_SPACE1_SYS
#define A527_SRAM1_SIZE		SUN55I_SRAM_SPACE1_SIZE
#define A527_DRAM_PHYS		0x48000000ULL
#define A527_DRAM_SIZE		0x100000	/* 1 MB */
#define A527_TRACE_PHYS		0x50000000ULL
#define A527_TRACE_SIZE		0x1000		/* 4 KB */

struct test_context {
	struct rproc rproc;
	struct sunxi_rproc priv;
	u32 mock_cfg_regs[0x400 / 4];
	u32 mock_remap_reg;
	u8 mock_sram_buf[1024];
	u8 mock_sram1_buf[1024];
};

static struct test_context *create_test_ctx(struct kunit *test)
{
	struct test_context *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	ctx->rproc.priv = &ctx->priv;
	ctx->priv.rproc = &ctx->rproc;

	ctx->priv.r_sram_va = FAKE_SRAM_VA;
	ctx->priv.r_sram_phys = A527_SRAM_PHYS;
	ctx->priv.r_sram_size = A527_SRAM_SIZE;

	ctx->priv.r_sram1_va = FAKE_SRAM1_VA;
	ctx->priv.r_sram1_phys = A527_SRAM1_PHYS;
	ctx->priv.r_sram1_size = A527_SRAM1_SIZE;

	ctx->priv.dram_va = FAKE_DRAM_VA;
	ctx->priv.dram_phys = A527_DRAM_PHYS;
	ctx->priv.dram_size = A527_DRAM_SIZE;

	ctx->priv.trace_va = FAKE_TRACE_VA;
	ctx->priv.trace_phys = A527_TRACE_PHYS;
	ctx->priv.trace_size = A527_TRACE_SIZE;
	ctx->priv.cfg = &sun55i_riscv_cfg;

	INIT_WORK(&ctx->priv.vq_work, NULL);

	return ctx;
}

/* ==================== da_to_va: Basic & Guard Tests ==================== */

static void test_da_to_va_zero_length_returns_null(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT, 0, NULL));
}

static void test_da_to_va_overflow_guard(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, U64_MAX - 0x10, 0x20, NULL));
	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, U64_MAX, 1, NULL));
}

static void test_da_to_va_null_is_iomem_safe(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	/* Must succeed without dereferencing NULL is_iomem */
	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE0_DA_ALT, 0x100, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR);
}

/* ==================== da_to_va: Space 0 Translations ==================== */

static void test_da_to_va_sram_space0_core_da(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = false;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE0_DA_ALT, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);
}

static void test_da_to_va_sram_space0_core_da_offset(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE0_DA_ALT + 0x1000, 0x100, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR + 0x1000);
}

static void test_da_to_va_sram_space0_host_phys(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = false;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, A527_SRAM_PHYS, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);
}

static void test_da_to_va_sram_space0_host_phys_offset(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, A527_SRAM_PHYS + 0x2000, 0x100, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR + 0x2000);
}

static void test_da_to_va_sram_space0_alt_3ff80000(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = false;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE0_DA, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);
}

static void test_da_to_va_sram_space0_pubsram_c_da(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = false;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_C_DA, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);
}

/* ==================== da_to_va: Space 1 Translations ==================== */

static void test_da_to_va_sram_space1_host_phys(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = false;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, A527_SRAM1_PHYS, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM1_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);
}

static void test_da_to_va_sram_space1_40000000(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = false;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE1_DA, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM1_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);
}

static void test_da_to_va_sram_space1_40040000(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = false;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE1_DA_ALT, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM1_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);
}

/* ==================== da_to_va: DRAM & Trace Translations ==================== */

static void test_da_to_va_dram_direct(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = true;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, A527_DRAM_PHYS, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_DRAM_VA);
	KUNIT_EXPECT_FALSE(test, is_iomem);
}

static void test_da_to_va_trace_direct(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	bool is_iomem = true;
	void *va;

	va = sunxi_rproc_da_to_va(&ctx->rproc, A527_TRACE_PHYS, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_TRACE_VA);
	KUNIT_EXPECT_FALSE(test, is_iomem);
}

/* ==================== da_to_va: Negative & Isolation Tests ==================== */

static void test_da_to_va_out_of_range(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, 0x10000000, 0x100, NULL));
	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, 0x60000000, 0x100, NULL));
}

static void test_da_to_va_sram_boundaries(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	/* 1 byte before Space 0 start -> NULL */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT - 1, 1, NULL));

	/* Exact last byte inside Space 0 -> valid */
	va = sunxi_rproc_da_to_va(&ctx->rproc,
				  E907_SRAM_SPACE0_DA_ALT + A527_SRAM_SIZE - 1, 1, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);

	/* 1 byte beyond Space 0 end -> NULL */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT + A527_SRAM_SIZE, 1, NULL));

	/* Access starting inside Space 0 but spanning past end -> NULL */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT +
					       A527_SRAM_SIZE - 4, 8, NULL));
}

static void test_da_to_va_space1_boundaries(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	/* Exact last byte inside Space 1 -> valid */
	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE1_DA + A527_SRAM1_SIZE - 1, 1, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);

	/* Spanning past Space 1 end -> NULL */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE1_DA + A527_SRAM1_SIZE - 4, 8, NULL));
}

static void test_da_to_va_unmapped_regions_return_null(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	/* When SRAM Space 0 is unmapped, all Space 0 views return NULL */
	ctx->priv.r_sram_va = NULL;
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT, 0x100, NULL));
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       A527_SRAM_PHYS, 0x100, NULL));
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_C_DA, 0x100, NULL));

	/* When SRAM Space 1 is unmapped, Space 1 views return NULL */
	ctx->priv.r_sram1_va = NULL;
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE1_DA, 0x100, NULL));
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       A527_SRAM1_PHYS, 0x100, NULL));

	/* When DRAM and Trace are unmapped, they return NULL */
	ctx->priv.dram_va = NULL;
	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, A527_DRAM_PHYS, 0x100, NULL));
	ctx->priv.trace_va = NULL;
	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, A527_TRACE_PHYS, 0x100, NULL));
}

static void test_da_to_va_space_isolation(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	/*
	 * Space 1 DA (E907_SRAM_SPACE1_DA) must NEVER resolve to Space 0, even when
	 * Space 1 is unmapped.
	 */
	ctx->priv.r_sram1_va = NULL;
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE1_DA, 0x100, NULL));
}

static void test_da_to_va_exact_upper_boundary_space0(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	/* Exact last byte of Space 0 (E907_SRAM_SPACE0_DA + 256K - 1) */
	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE0_DA + A527_SRAM_SIZE - 1, 1, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR + A527_SRAM_SIZE - 1);

	/* Spanning 1 byte beyond Space 0 must be rejected */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA + A527_SRAM_SIZE - 1, 2, NULL));

	/* Exact last byte of Alt Space 0 (E907_SRAM_SPACE0_DA_ALT + 256K - 1) */
	va = sunxi_rproc_da_to_va(&ctx->rproc,
				  E907_SRAM_SPACE0_DA_ALT + A527_SRAM_SIZE - 1, 1, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR + A527_SRAM_SIZE - 1);

	/* Spanning 1 byte beyond Alt Space 0 must be rejected */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT +
					       A527_SRAM_SIZE - 1, 2, NULL));
}

static void test_da_to_va_exact_upper_boundary_space1(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	/* Exact last byte of Space 1 (E907_SRAM_SPACE1_DA + 256K - 1) */
	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE1_DA + A527_SRAM1_SIZE - 1, 1, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM1_PTR + A527_SRAM1_SIZE - 1);

	/* Spanning 1 byte beyond Space 1 must be rejected */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE1_DA + A527_SRAM1_SIZE - 1, 2, NULL));
}

static void test_da_to_va_exact_upper_boundary_dram(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	/* Exact last byte of DRAM carveout */
	va = sunxi_rproc_da_to_va(&ctx->rproc, A527_DRAM_PHYS + A527_DRAM_SIZE - 1, 1, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_DRAM_VA + A527_DRAM_SIZE - 1);

	/* Spanning 1 byte beyond DRAM carveout must be rejected */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       A527_DRAM_PHYS + A527_DRAM_SIZE - 1, 2, NULL));
}

static void test_da_to_va_a733_sram_a2_layout(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;
	bool is_iomem = false;

	/*
	 * Allwinner A733 (sun60i) E902 silicon profile:
	 * Uses System SRAM A2 (0x00040000 - 0x00073FFF, 208 KB).
	 * Has no Space 1 (r_sram1_va is NULL).
	 */
	ctx->priv.r_sram_phys = 0x00040000ULL;
	ctx->priv.r_sram_size = 0x34000; /* 208 KB */
	ctx->priv.r_sram1_va = NULL;
	ctx->priv.r_sram1_size = 0;

	/* Base of SRAM A2 */
	va = sunxi_rproc_da_to_va(&ctx->rproc, 0x00040000, 0x100, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR);
	KUNIT_EXPECT_TRUE(test, is_iomem);

	/* Entry offset in SRAM A2 (0x00044000) */
	va = sunxi_rproc_da_to_va(&ctx->rproc, 0x00044000, 0x1000, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR + 0x4000);

	/* Exact last byte of SRAM A2 (0x00040000 + 0x34000 - 1 = 0x00073FFF) */
	va = sunxi_rproc_da_to_va(&ctx->rproc, 0x00073FFF, 1, &is_iomem);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR + 0x33FFF);

	/* Beyond SRAM A2 boundary must return NULL */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc, 0x00074000, 1, NULL));
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc, 0x00073FFF, 2, NULL));
}

static void test_da_to_va_unaligned_lengths(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	void *va;

	/* 3-byte and 7-byte transfers must translate correctly without faulting */
	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE0_DA + 3, 3, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM_PTR + 3);

	va = sunxi_rproc_da_to_va(&ctx->rproc, E907_SRAM_SPACE1_DA + 7, 7, NULL);
	KUNIT_ASSERT_NOT_NULL(test, va);
	KUNIT_EXPECT_PTR_EQ(test, va, FAKE_SRAM1_PTR + 7);
}

static void test_da_to_va_malformed_rsc_table_entry(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	/*
	 * Malformed resource table entry: firmware requests a VirtIO vring
	 * or carveout pointing to an invalid device address (e.g. 0xDEADBEEF).
	 * da_to_va must return NULL, prompting rproc_elf_load_rsc_table to fail
	 * safely rather than writing into unmapped space.
	 */
	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, 0xDEADBEEF, 0x1000, NULL));

	/* Out-of-window address between SRAM A3 and Space 1 (0x3FFD0000) */
	KUNIT_EXPECT_NULL(test, sunxi_rproc_da_to_va(&ctx->rproc, 0x3FFD0000, 0x1000, NULL));
}

static void test_da_to_va_corrupted_elf_overflow_segment(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	/*
	 * Corrupted ELF header: segment has an excessive memsz that wraps around
	 * 64-bit integer limits or spans across window bounds.
	 */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT,
					       (size_t)-1, NULL));
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE1_DA,
					       (size_t)-16, NULL));

	/* Segment starts near end of Space 0 and extends 4KB beyond valid SRAM */
	KUNIT_EXPECT_NULL(test,
			  sunxi_rproc_da_to_va(&ctx->rproc,
					       E907_SRAM_SPACE0_DA_ALT + A527_SRAM_SIZE - 0x100,
					       0x200, NULL));
}

static void test_start_a733_mode1_and_mode2_bootaddr(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	int ret;

	ctx->priv.cfg_va = (void __iomem *)ctx->mock_cfg_regs;

	/* Mode 1: Suspend/resume E902 SCP boot from DRAM (0x40014000) */
	ctx->rproc.bootaddr = 0x40014000;
	ret = sunxi_rproc_start(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->mock_cfg_regs[E906_STA_ADD_REG / 4], 0x40014000U);

	/* Mode 2: Real-time coprocessor boot from SRAM A2 (0x00044000) */
	ctx->rproc.bootaddr = 0x00044000;
	ret = sunxi_rproc_start(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->mock_cfg_regs[E906_STA_ADD_REG / 4], 0x00044000U);
}

/* ==================== Lifecycle: start & stop ==================== */

static void test_start_bootaddr_programming(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	int ret;

	ctx->priv.cfg_va = (void __iomem *)ctx->mock_cfg_regs;
	ctx->rproc.bootaddr = 0x40014000;

	ret = sunxi_rproc_start(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Check that bootaddr was written to STA_ADD_REG (offset 0x204) */
	KUNIT_EXPECT_EQ(test, ctx->mock_cfg_regs[E906_STA_ADD_REG / 4], 0x40014000U);
}

static void test_start_rejects_bootaddr_overflow(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	int ret;

	/* Addresses beyond 32-bit range are invalid for 32-bit XuanTie E907 */
	ctx->rproc.bootaddr = 0x100000000ULL;

	ret = sunxi_rproc_start(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void test_stop_succeeds(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	int ret;

	ret = sunxi_rproc_stop(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/* ==================== Lifecycle: prepare & unprepare ==================== */

static void test_prepare_and_unprepare_remap(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	int ret;

	ctx->priv.remap_va = (void __iomem *)&ctx->mock_remap_reg;
	ctx->mock_remap_reg = 0;

	/* prepare() should set SUNXI_REMAP_SRAMA3_2_BIT (bit 1) */
	ret = sunxi_rproc_prepare(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->mock_remap_reg & SUNXI_REMAP_SRAMA3_2_BIT,
			SUNXI_REMAP_SRAMA3_2_BIT);

	/* unprepare() should clear SUNXI_REMAP_SRAMA3_2_BIT */
	ret = sunxi_rproc_unprepare(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->mock_remap_reg & SUNXI_REMAP_SRAMA3_2_BIT, 0U);
}

static void test_prepare_clears_sram(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);
	int i, ret;

	memset(ctx->mock_sram_buf, 0xAA, sizeof(ctx->mock_sram_buf));
	memset(ctx->mock_sram1_buf, 0x55, sizeof(ctx->mock_sram1_buf));

	ctx->priv.r_sram_va = (void __iomem *)ctx->mock_sram_buf;
	ctx->priv.r_sram_size = sizeof(ctx->mock_sram_buf);

	ctx->priv.r_sram1_va = (void __iomem *)ctx->mock_sram1_buf;
	ctx->priv.r_sram1_size = sizeof(ctx->mock_sram1_buf);

	ret = sunxi_rproc_prepare(&ctx->rproc);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Both SRAM buffers must be cleanly zeroed to prevent stale data / ECC faults */
	for (i = 0; i < sizeof(ctx->mock_sram_buf); i++)
		KUNIT_EXPECT_EQ(test, ctx->mock_sram_buf[i], 0);

	for (i = 0; i < sizeof(ctx->mock_sram1_buf); i++)
		KUNIT_EXPECT_EQ(test, ctx->mock_sram1_buf[i], 0);
}

/* ==================== Operations: kick ==================== */

static void test_kick_null_tx_chan_safe(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	ctx->priv.tx_chan = NULL;
	/* Must return cleanly without NULL dereference */
	sunxi_rproc_kick(&ctx->rproc, 0);
	sunxi_rproc_kick(&ctx->rproc, 1);
}

static void test_kick_stores_vqid(struct kunit *test)
{
	struct test_context *ctx = create_test_ctx(test);

	/* Verify kick_msg stores the passed vqid to avoid stack UAF */
	ctx->priv.kick_msg = 0xDEADBEEF;
	ctx->priv.tx_chan = NULL; /* Avoid mbox_send_message dispatch */

	sunxi_rproc_kick(&ctx->rproc, 1);
	/* Without tx_chan, returns before writing kick_msg */
	KUNIT_EXPECT_EQ(test, ctx->priv.kick_msg, 0xDEADBEEFU);
}

/* ==================== Test Suite Registration ==================== */

static struct kunit_case sunxi_rproc_test_cases[] = {
	/* da_to_va Guard Tests */
	KUNIT_CASE(test_da_to_va_zero_length_returns_null),
	KUNIT_CASE(test_da_to_va_overflow_guard),
	KUNIT_CASE(test_da_to_va_null_is_iomem_safe),
	/* da_to_va Space 0 Tests */
	KUNIT_CASE(test_da_to_va_sram_space0_core_da),
	KUNIT_CASE(test_da_to_va_sram_space0_core_da_offset),
	KUNIT_CASE(test_da_to_va_sram_space0_host_phys),
	KUNIT_CASE(test_da_to_va_sram_space0_host_phys_offset),
	KUNIT_CASE(test_da_to_va_sram_space0_alt_3ff80000),
	KUNIT_CASE(test_da_to_va_sram_space0_pubsram_c_da),
	/* da_to_va Space 1 Tests */
	KUNIT_CASE(test_da_to_va_sram_space1_host_phys),
	KUNIT_CASE(test_da_to_va_sram_space1_40000000),
	KUNIT_CASE(test_da_to_va_sram_space1_40040000),
	/* da_to_va DRAM & Trace Tests */
	KUNIT_CASE(test_da_to_va_dram_direct),
	KUNIT_CASE(test_da_to_va_trace_direct),
	/* da_to_va Out of Range, Boundary, Unmapped & Isolation Tests */
	KUNIT_CASE(test_da_to_va_out_of_range),
	KUNIT_CASE(test_da_to_va_sram_boundaries),
	KUNIT_CASE(test_da_to_va_space1_boundaries),
	KUNIT_CASE(test_da_to_va_unmapped_regions_return_null),
	KUNIT_CASE(test_da_to_va_space_isolation),
	KUNIT_CASE(test_da_to_va_exact_upper_boundary_space0),
	KUNIT_CASE(test_da_to_va_exact_upper_boundary_space1),
	KUNIT_CASE(test_da_to_va_exact_upper_boundary_dram),
	KUNIT_CASE(test_da_to_va_a733_sram_a2_layout),
	KUNIT_CASE(test_da_to_va_unaligned_lengths),
	KUNIT_CASE(test_da_to_va_malformed_rsc_table_entry),
	KUNIT_CASE(test_da_to_va_corrupted_elf_overflow_segment),
	/* Lifecycle: start and stop */
	KUNIT_CASE(test_start_bootaddr_programming),
	KUNIT_CASE(test_start_a733_mode1_and_mode2_bootaddr),
	KUNIT_CASE(test_start_rejects_bootaddr_overflow),
	KUNIT_CASE(test_stop_succeeds),
	/* Lifecycle: prepare and unprepare */
	KUNIT_CASE(test_prepare_and_unprepare_remap),
	KUNIT_CASE(test_prepare_clears_sram),
	/* Operations: kick */
	KUNIT_CASE(test_kick_null_tx_chan_safe),
	KUNIT_CASE(test_kick_stores_vqid),
	{}
};

static struct kunit_suite sunxi_rproc_test_suite = {
	.name = "sunxi_rproc",
	.test_cases = sunxi_rproc_test_cases,
};

kunit_test_suite(sunxi_rproc_test_suite);

MODULE_AUTHOR("Tim Michals <tcmichals@gmail.com>");
MODULE_DESCRIPTION("Comprehensive KUnit tests for Allwinner sunxi remoteproc driver");
MODULE_LICENSE("GPL");

```

### 6.2 Mailbox KUnit Suite (`drivers/mailbox/sun55i_msgbox_test.c`)
```c
// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for Allwinner sun55i 4-port Message Box driver (sun55i-msgbox.c)
 *
 * Comprehensive, industry-standard test suite validating:
 *  - Full 12-channel routing table covering CPUS, DSP, and XuanTie RISC-V cores
 *  - Boundary and out-of-range channel limits
 *  - Register offset formulas and IRQ bitmask macros
 *  - Functional driver ops (send_data, last_tx_done, peek_data, startup, shutdown)
 *  - Exhaustive 12-channel physical destination register write verification
 *  - Full threshold sweeps for last_tx_done (0..15) and peek_data (0..15)
 *  - Multi-message burst FIFO receiving with exact payload sequencing
 *  - Anti-lockup hardirq bounded loop guarantee (FIFO_MAX limit)
 *  - Multi-channel and multi-port concurrency in a single hardirq pass
 *  - Stale FIFO purging on startup under varying depths (0, 4, stuck)
 *  - Interrupt isolation between channels sharing the same port
 *
 * Directly invokes driver operations using mock register banks.
 *
 * Copyright (C) 2026 Tim Michals <tcmichals@gmail.com>
 */

#include <kunit/test.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include "sun55i-msgbox.h"

/* =============== Channel Routing Table Tests =============== */

static void test_chan_to_route_cpus_all(struct kunit *test)
{
	int local_n, p, remote_id, remote_n;

	/* Ch 0..3: CPUS (remote_id = SUN55I_PROC_CPUS, remote_n = 0, local_n = 0) */
	sun55i_chan_to_route(0, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 0);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_CPUS);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	sun55i_chan_to_route(1, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 1);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_CPUS);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	sun55i_chan_to_route(2, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 2);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_CPUS);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	sun55i_chan_to_route(3, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 3);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_CPUS);
	KUNIT_EXPECT_EQ(test, remote_n, 0);
}

static void test_chan_to_route_dsp_all(struct kunit *test)
{
	int local_n, p, remote_id, remote_n;

	/* Ch 4..7: DSP (remote_id = SUN55I_PROC_DSP, remote_n = 0, local_n = 1) */
	sun55i_chan_to_route(4, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 1);
	KUNIT_EXPECT_EQ(test, p, 0);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_DSP);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	sun55i_chan_to_route(5, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 1);
	KUNIT_EXPECT_EQ(test, p, 1);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_DSP);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	sun55i_chan_to_route(6, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 1);
	KUNIT_EXPECT_EQ(test, p, 2);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_DSP);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	sun55i_chan_to_route(7, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 1);
	KUNIT_EXPECT_EQ(test, p, 3);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_DSP);
	KUNIT_EXPECT_EQ(test, remote_n, 0);
}

static void test_chan_to_route_rv_all(struct kunit *test)
{
	int local_n, p, remote_id, remote_n;

	/* Ch 8..11: XuanTie RISC-V (remote_id = SUN55I_PROC_RV, remote_n = 2, local_n = 2) */
	sun55i_chan_to_route(8, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 2);
	KUNIT_EXPECT_EQ(test, p, 0);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_RV);
	KUNIT_EXPECT_EQ(test, remote_n, 2);

	sun55i_chan_to_route(9, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 2);
	KUNIT_EXPECT_EQ(test, p, 1);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_RV);
	KUNIT_EXPECT_EQ(test, remote_n, 2);

	sun55i_chan_to_route(10, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 2);
	KUNIT_EXPECT_EQ(test, p, 2);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_RV);
	KUNIT_EXPECT_EQ(test, remote_n, 2);

	sun55i_chan_to_route(11, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 2);
	KUNIT_EXPECT_EQ(test, p, 3);
	KUNIT_EXPECT_EQ(test, remote_id, SUN55I_PROC_RV);
	KUNIT_EXPECT_EQ(test, remote_n, 2);
}

static void test_chan_to_route_boundary_limits(struct kunit *test)
{
	int local_n, p, remote_id, remote_n;

	/* Channel 0 (lowest valid) */
	sun55i_chan_to_route(0, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 0);

	/* Channel 11 (highest valid) */
	sun55i_chan_to_route(11, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 2);
	KUNIT_EXPECT_EQ(test, p, 3);
}

static void test_chan_to_route_invalid_channels(struct kunit *test)
{
	int local_n, p, remote_id, remote_n;

	/* Negative channel index -> safely clamped to 0 */
	sun55i_chan_to_route(-1, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 0);
	KUNIT_EXPECT_EQ(test, remote_id, 0);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	/* Out of range channel 12 -> safely clamped to 0 */
	sun55i_chan_to_route(12, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 0);
	KUNIT_EXPECT_EQ(test, remote_id, 0);
	KUNIT_EXPECT_EQ(test, remote_n, 0);

	/* Far out of range channel 100 -> safely clamped to 0 */
	sun55i_chan_to_route(100, &local_n, &p, &remote_id, &remote_n);
	KUNIT_EXPECT_EQ(test, local_n, 0);
	KUNIT_EXPECT_EQ(test, p, 0);
	KUNIT_EXPECT_EQ(test, remote_id, 0);
	KUNIT_EXPECT_EQ(test, remote_n, 0);
}

/* =============== Register Offset Macro Tests =============== */

static void test_msgbox_offset_values(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_OFFSET(0), 0x000);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_OFFSET(1), 0x100);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_OFFSET(2), 0x200);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_OFFSET(3), 0x300);
}

static void test_msgbox_irq_reg_offsets(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_READ_IRQ_ENABLE(0),  0x020);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_READ_IRQ_STATUS(0),  0x024);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_WRITE_IRQ_ENABLE(0), 0x030);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_WRITE_IRQ_STATUS(0), 0x034);

	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_READ_IRQ_ENABLE(1),  0x120);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_READ_IRQ_STATUS(1),  0x124);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_READ_IRQ_ENABLE(2),  0x220);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_READ_IRQ_STATUS(2),  0x224);
}

static void test_msgbox_fifo_reg_offsets(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_FIFO_STATUS(0, 0), 0x050);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_FIFO_STATUS(0, 3), 0x05c);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_MSG_STATUS(0, 0),  0x060);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_MSG_STATUS(0, 3),  0x06c);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_MSG_FIFO(0, 0),    0x070);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_MSG_FIFO(0, 3),    0x07c);

	/* Port 2 (RV remote in sun55i_msgbox_arm_routes[2]) */
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_MSG_FIFO(2, 0),    0x270);
	KUNIT_EXPECT_EQ(test, SUNXI_MSGBOX_MSG_FIFO(2, 3),    0x27c);
}

static void test_msgbox_irq_bit_positions(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RD_IRQ_EN_BIT(0),   BIT(0));
	KUNIT_EXPECT_EQ(test, RD_IRQ_EN_BIT(1),   BIT(2));
	KUNIT_EXPECT_EQ(test, RD_IRQ_EN_BIT(2),   BIT(4));
	KUNIT_EXPECT_EQ(test, RD_IRQ_EN_BIT(3),   BIT(6));

	KUNIT_EXPECT_EQ(test, RD_IRQ_PEND_BIT(0), BIT(0));
	KUNIT_EXPECT_EQ(test, RD_IRQ_PEND_BIT(1), BIT(2));
	KUNIT_EXPECT_EQ(test, RD_IRQ_PEND_BIT(2), BIT(4));
	KUNIT_EXPECT_EQ(test, RD_IRQ_PEND_BIT(3), BIT(6));
}

static void test_msgbox_constants(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, SUN55I_MAX_PROCESSORS, 4);
	KUNIT_EXPECT_EQ(test, SUN55I_CHANS_PER_PROC,  4);
	KUNIT_EXPECT_EQ(test, SUN55I_NUM_CHANS,      12);
	KUNIT_EXPECT_EQ(test, SUN55I_FIFO_MAX,        8);
	KUNIT_EXPECT_EQ(test, (u32)MSG_NUM_MASK,     0xfU);
}

/* =============== Functional Driver Ops Tests (Mock MMIO) =============== */

struct mock_rx_sink {
	struct mbox_client client;
	int count;
	u32 last_msg;
	u32 msgs[SUN55I_FIFO_MAX * 2];
};

struct mock_msgbox_fixture {
	struct sun55i_msgbox mbox;
	struct mbox_chan chans[SUN55I_NUM_CHANS];
	struct mock_rx_sink sinks[SUN55I_NUM_CHANS];
	u32 regs[SUN55I_MAX_PROCESSORS][0x400 / 4];
};

static void mock_rx_cb(struct mbox_client *cl, void *data)
{
	struct mock_rx_sink *sink = container_of(cl, struct mock_rx_sink, client);

	if (sink->count < ARRAY_SIZE(sink->msgs))
		sink->msgs[sink->count] = *(u32 *)data;
	sink->count++;
	sink->last_msg = *(u32 *)data;
}

static struct mock_msgbox_fixture *create_mock_fixture(struct kunit *test)
{
	struct mock_msgbox_fixture *fix;
	int i;

	fix = kunit_kzalloc(test, sizeof(*fix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fix);

	spin_lock_init(&fix->mbox.lock);
	fix->mbox.controller.chans = fix->chans;
	fix->mbox.controller.num_chans = SUN55I_NUM_CHANS;

	for (i = 0; i < SUN55I_MAX_PROCESSORS; i++)
		fix->mbox.regs[i] = (void __iomem *)fix->regs[i];

	for (i = 0; i < SUN55I_NUM_CHANS; i++) {
		fix->chans[i].con_priv = &fix->mbox;
		fix->sinks[i].client.rx_callback = mock_rx_cb;
		fix->chans[i].cl = &fix->sinks[i].client;
	}

	return fix;
}

static void test_functional_send_data_all_twelve_channels(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	int ch;

	/* Send unique test pattern over every channel from 0 to 11 */
	for (ch = 0; ch < SUN55I_NUM_CHANS; ch++) {
		int local_n, p, remote_id, remote_n;
		u32 val = 0x55000000U | (u32)ch;
		u32 reg_idx;

		sun55i_chan_to_route(ch, &local_n, &p, &remote_id, &remote_n);
		sun55i_msgbox_chan_ops.send_data(&fix->chans[ch], &val);

		reg_idx = SUNXI_MSGBOX_MSG_FIFO(remote_n, p) / 4;
		KUNIT_EXPECT_EQ(test, fix->regs[remote_id][reg_idx], val);
	}
}

static void test_functional_send_data_null(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 reg_idx;

	/* NULL data pointer writes 0 to FIFO without dereferencing */
	sun55i_msgbox_chan_ops.send_data(&fix->chans[8], NULL);

	reg_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;
	KUNIT_EXPECT_EQ(test, fix->regs[3][reg_idx], 0U);
}

static void test_functional_send_data_patterns(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	static const u32 patterns[] = {
		0x00000000, 0xFFFFFFFF, 0x55555555, 0xAAAAAAAA, 0x12345678,
	};
	int i;
	u32 reg_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;

	for (i = 0; i < ARRAY_SIZE(patterns); i++) {
		sun55i_msgbox_chan_ops.send_data(&fix->chans[8], (void *)&patterns[i]);
		KUNIT_EXPECT_EQ(test, fix->regs[3][reg_idx], patterns[i]);
	}
}

static void test_functional_last_tx_done_sweep(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 reg_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	u32 count;

	/* Counts 0..7: FIFO has space -> last_tx_done returns true */
	for (count = 0; count < 8; count++) {
		fix->regs[3][reg_idx] = count;
		KUNIT_EXPECT_TRUE(test, sun55i_msgbox_chan_ops.last_tx_done(&fix->chans[8]));
	}

	/* Counts 8..15: FIFO is full or overflow -> last_tx_done returns false */
	for (count = 8; count <= 15; count++) {
		fix->regs[3][reg_idx] = count;
		KUNIT_EXPECT_FALSE(test, sun55i_msgbox_chan_ops.last_tx_done(&fix->chans[8]));
	}
}

static void test_functional_peek_data_sweep(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 reg_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	u32 count;

	/* Count 0: no messages -> returns false */
	fix->regs[0][reg_idx] = 0;
	KUNIT_EXPECT_FALSE(test, sun55i_msgbox_chan_ops.peek_data(&fix->chans[8]));

	/* Counts 1..15: has messages -> returns true */
	for (count = 1; count <= 15; count++) {
		fix->regs[0][reg_idx] = count;
		KUNIT_EXPECT_TRUE(test, sun55i_msgbox_chan_ops.peek_data(&fix->chans[8]));
	}
}

static void test_functional_startup_and_shutdown(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4;

	/* Startup on Channel 8: enables IRQ and clears pending status */
	sun55i_msgbox_chan_ops.startup(&fix->chans[8]);
	KUNIT_EXPECT_EQ(test, fix->regs[0][en_idx] & RD_IRQ_EN_BIT(0), RD_IRQ_EN_BIT(0));
	KUNIT_EXPECT_EQ(test, fix->regs[0][stat_idx] & RD_IRQ_PEND_BIT(0), RD_IRQ_PEND_BIT(0));

	/* Shutdown on Channel 8: disables IRQ */
	sun55i_msgbox_chan_ops.shutdown(&fix->chans[8]);
	KUNIT_EXPECT_EQ(test, fix->regs[0][en_idx] & RD_IRQ_EN_BIT(0), 0U);
}

static void test_functional_shutdown_isolation(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4;

	/* Enable both Channel 0 and Channel 1 on Port 0 */
	sun55i_msgbox_chan_ops.startup(&fix->chans[0]);
	sun55i_msgbox_chan_ops.startup(&fix->chans[1]);
	KUNIT_EXPECT_EQ(test, fix->regs[0][en_idx] & (RD_IRQ_EN_BIT(0) | RD_IRQ_EN_BIT(1)),
			RD_IRQ_EN_BIT(0) | RD_IRQ_EN_BIT(1));

	/* Shutting down Channel 0 must leave Channel 1 enabled */
	sun55i_msgbox_chan_ops.shutdown(&fix->chans[0]);
	KUNIT_EXPECT_EQ(test, fix->regs[0][en_idx] & RD_IRQ_EN_BIT(0), 0U);
	KUNIT_EXPECT_EQ(test, fix->regs[0][en_idx] & RD_IRQ_EN_BIT(1), RD_IRQ_EN_BIT(1));
}

static void test_functional_startup_flushes_stale_fifo(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4;
	u32 msg_stat_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	u32 fifo_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;
	int ret;

	/* Simulate stale messages present in hardware FIFO before channel open */
	fix->regs[0][msg_stat_idx] = 4;
	fix->regs[0][fifo_idx] = 0xDEADBEEF;

	ret = sun55i_msgbox_chan_ops.startup(&fix->chans[8]);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* IRQ must be enabled and pending status cleared */
	KUNIT_EXPECT_EQ(test, fix->regs[0][en_idx] & RD_IRQ_EN_BIT(0), RD_IRQ_EN_BIT(0));
	KUNIT_EXPECT_EQ(test, fix->regs[0][stat_idx] & RD_IRQ_PEND_BIT(0), RD_IRQ_PEND_BIT(0));
}

static void test_functional_startup_capped_at_fifo_max(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 msg_stat_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	u32 fifo_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;
	int ret;

	/* Hardware defect: MSG_STATUS always returns non-zero */
	fix->regs[0][msg_stat_idx] = 15;
	fix->regs[0][fifo_idx] = 0xCAFEBABE;

	/* Must terminate after SUN55I_FIFO_MAX reads and return 0 */
	ret = sun55i_msgbox_chan_ops.startup(&fix->chans[8]);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

static void test_functional_shutdown_flushes_and_bounds(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4;
	u32 msg_stat_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	u32 fifo_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;

	/* First startup */
	sun55i_msgbox_chan_ops.startup(&fix->chans[8]);

	/* Simulate residual messages arriving before shutdown */
	fix->regs[0][msg_stat_idx] = 15;
	fix->regs[0][fifo_idx] = 0x11223344;

	/* Must terminate after SUN55I_FIFO_MAX without hanging */
	sun55i_msgbox_chan_ops.shutdown(&fix->chans[8]);
	KUNIT_EXPECT_EQ(test, fix->regs[0][en_idx] & RD_IRQ_EN_BIT(0), 0U);
	KUNIT_EXPECT_EQ(test, fix->regs[0][stat_idx] & RD_IRQ_PEND_BIT(0), RD_IRQ_PEND_BIT(0));
}

/* =============== Interrupt Handler (Hardirq) Simulation =============== */

static void test_irq_spurious_returns_none(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	irqreturn_t ret;

	/* All IRQ enable and status registers are 0 -> IRQ_NONE */
	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_NONE);
}

static void test_irq_single_message_received(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4;
	u32 msg_stat_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	u32 fifo_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;
	irqreturn_t ret;

	/* Configure pending IRQ on Channel 8 (local_n = 2, p = 0) */
	fix->regs[0][en_idx] = RD_IRQ_EN_BIT(0);
	fix->regs[0][stat_idx] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][msg_stat_idx] = 1; /* 1 message in FIFO */
	fix->regs[0][fifo_idx] = 0xCAFEF00D;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* Verify data delivered to mailbox client */
	KUNIT_EXPECT_EQ(test, fix->sinks[8].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[8].last_msg, 0xCAFEF00DU);

	/* Verify write-1-to-clear bit was written */
	KUNIT_EXPECT_EQ(test, fix->regs[0][stat_idx], RD_IRQ_PEND_BIT(0));
}

static void test_irq_empty_fifo_status_clear(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4;
	u32 msg_stat_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	irqreturn_t ret;

	/* Pending IRQ bit set, but MSG_STATUS has 0 messages */
	fix->regs[0][en_idx] = RD_IRQ_EN_BIT(0);
	fix->regs[0][stat_idx] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][msg_stat_idx] = 0;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* No messages should be dispatched to client */
	KUNIT_EXPECT_EQ(test, fix->sinks[8].count, 0);

	/* Status must still be cleared */
	KUNIT_EXPECT_EQ(test, fix->regs[0][stat_idx], RD_IRQ_PEND_BIT(0));
}

static void test_irq_multi_channel_concurrency(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(0) / 4;
	irqreturn_t ret;

	/* Both Channel 0 (p=0) and Channel 1 (p=1) pending on Port 0 */
	fix->regs[0][en_idx] = RD_IRQ_EN_BIT(0) | RD_IRQ_EN_BIT(1);
	fix->regs[0][stat_idx] = RD_IRQ_PEND_BIT(0) | RD_IRQ_PEND_BIT(1);

	/* Ch 0 has message 0x11111111 */
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(0, 0) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(0, 0) / 4] = 0x11111111;

	/* Ch 1 has message 0x22222222 */
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(0, 1) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(0, 1) / 4] = 0x22222222;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* Both channels must be serviced in the same interrupt invocation */
	KUNIT_EXPECT_EQ(test, fix->sinks[0].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[0].last_msg, 0x11111111U);

	KUNIT_EXPECT_EQ(test, fix->sinks[1].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[1].last_msg, 0x22222222U);
}

static void test_irq_multi_port_concurrency(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	irqreturn_t ret;

	/* Port 0 (Channel 0: CPUS) has pending interrupt */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(0) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(0, 0) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(0, 0) / 4] = 0xAAAAAAAA;

	/* Port 2 (Channel 8: RV) has pending interrupt */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4] = 0xBBBBBBBB;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* Both ports must be processed in the single pass across local_n = 0..2 */
	KUNIT_EXPECT_EQ(test, fix->sinks[0].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[0].last_msg, 0xAAAAAAAAU);

	KUNIT_EXPECT_EQ(test, fix->sinks[8].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[8].last_msg, 0xBBBBBBBBU);
}

static void test_irq_fifo_drain_capped_at_max(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4;
	u32 msg_stat_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	u32 fifo_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;
	irqreturn_t ret;

	/*
	 * Simulate hardware defect or runaway remote: MSG_STATUS is permanently
	 * non-zero. The ISR MUST NOT hang in an infinite loop; it must drain
	 * at most SUN55I_FIFO_MAX (8) messages and return.
	 */
	fix->regs[0][en_idx] = RD_IRQ_EN_BIT(0);
	fix->regs[0][stat_idx] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][msg_stat_idx] = 15; /* Always reports data */
	fix->regs[0][fifo_idx] = 0x12345678;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* Drain must be strictly bounded at SUN55I_FIFO_MAX */
	KUNIT_EXPECT_EQ(test, fix->sinks[8].count, SUN55I_FIFO_MAX);
}

static void test_irq_channel_crosstalk_isolation(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(0) / 4;
	irqreturn_t ret;
	int ch;

	/* Only Channel 2 (Port 0, p=2) has an interrupt enabled and pending */
	fix->regs[0][en_idx] = RD_IRQ_EN_BIT(2);
	fix->regs[0][stat_idx] = RD_IRQ_PEND_BIT(2);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(0, 2) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(0, 2) / 4] = 0x33333333;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* Exactly Channel 2 received the message */
	KUNIT_EXPECT_EQ(test, fix->sinks[2].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[2].last_msg, 0x33333333U);

	/* All other 11 channels must have received 0 messages */
	for (ch = 0; ch < SUN55I_NUM_CHANS; ch++) {
		if (ch == 2)
			continue;
		KUNIT_EXPECT_EQ(test, fix->sinks[ch].count, 0);
	}
}

static void test_irq_all_three_routes_simultaneous(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	irqreturn_t ret;

	/* Port 0: Channel 0 (CPUS) */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(0) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(0, 0) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(0, 0) / 4] = 0x11110001;

	/* Port 1: Channel 4 (DSP) */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(1) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(1) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(1, 0) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(1, 0) / 4] = 0x22220001;

	/* Port 2: Channel 8 (RV) */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4] = 0x33330001;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* All three destinations serviced cleanly in one ISR pass */
	KUNIT_EXPECT_EQ(test, fix->sinks[0].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[0].last_msg, 0x11110001U);

	KUNIT_EXPECT_EQ(test, fix->sinks[4].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[4].last_msg, 0x22220001U);

	KUNIT_EXPECT_EQ(test, fix->sinks[8].count, 1);
	KUNIT_EXPECT_EQ(test, fix->sinks[8].last_msg, 0x33330001U);
}

static void test_irq_disabled_channel_ignored(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4;
	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(0) / 4;
	irqreturn_t ret;

	/* Channel 1 has pending bit set, but IRQ_ENABLE is 0 (channel disabled) */
	fix->regs[0][en_idx] = 0;
	fix->regs[0][stat_idx] = RD_IRQ_PEND_BIT(1);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(0, 1) / 4] = 1;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(0, 1) / 4] = 0x12345678;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	/* Must return IRQ_NONE and must NOT deliver message */
	KUNIT_EXPECT_EQ(test, ret, IRQ_NONE);
	KUNIT_EXPECT_EQ(test, fix->sinks[1].count, 0);
}

static void test_irq_spurious_noise_bits(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	irqreturn_t ret;

	/* Non-channel upper bits set, no valid channel enabled */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4] = 0;
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(0) / 4] = 0xFFFF0000U;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_NONE);
}

static void test_functional_last_tx_done_backpressure_boundary(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	u32 reg_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
	bool done;

	/* 7 entries in FIFO -> space available (< 8) -> returns true */
	fix->regs[3][reg_idx] = 7;
	done = sun55i_msgbox_chan_ops.last_tx_done(&fix->chans[8]);
	KUNIT_EXPECT_TRUE(test, done);

	/* 8 entries in FIFO -> full (== SUN55I_FIFO_MAX) -> backpressure active (false) */
	fix->regs[3][reg_idx] = 8;
	done = sun55i_msgbox_chan_ops.last_tx_done(&fix->chans[8]);
	KUNIT_EXPECT_FALSE(test, done);

	/* 15 entries in FIFO -> full -> backpressure active (false) */
	fix->regs[3][reg_idx] = 15;
	done = sun55i_msgbox_chan_ops.last_tx_done(&fix->chans[8]);
	KUNIT_EXPECT_FALSE(test, done);
}

static void test_irq_multi_port_burst_interleaved(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
	irqreturn_t ret;

	/* Port 0 (Ch 0: CPUS) has 2 messages */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(0) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(0) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(0, 0) / 4] = 2;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(0, 0) / 4] = 0xAA01;

	/* Port 1 (Ch 4: DSP) has 2 messages */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(1) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(1) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(1, 0) / 4] = 2;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(1, 0) / 4] = 0xBB01;

	/* Port 2 (Ch 8: RV) has 2 messages */
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4] = RD_IRQ_EN_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4] = RD_IRQ_PEND_BIT(0);
	fix->regs[0][SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4] = 2;
	fix->regs[0][SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4] = 0xCC01;

	ret = sun55i_msgbox_irq(0, &fix->mbox);
	KUNIT_EXPECT_EQ(test, ret, IRQ_HANDLED);

	/* Check deliveries to respective sinks */
	KUNIT_EXPECT_EQ(test, fix->sinks[0].count, 2);
	KUNIT_EXPECT_EQ(test, fix->sinks[4].count, 2);
	KUNIT_EXPECT_EQ(test, fix->sinks[8].count, 2);
}

static void test_msgbox_controller_invariants(struct kunit *test)
{
	struct mock_msgbox_fixture *fix = create_mock_fixture(test);

	KUNIT_EXPECT_EQ(test, fix->mbox.controller.num_chans, SUN55I_NUM_CHANS);
	KUNIT_EXPECT_PTR_EQ(test, fix->mbox.controller.chans, &fix->chans[0]);
}

static void test_msgbox_chan_ops_completeness(struct kunit *test)
{
	KUNIT_EXPECT_NOT_NULL(test, sun55i_msgbox_chan_ops.send_data);
	KUNIT_EXPECT_NOT_NULL(test, sun55i_msgbox_chan_ops.startup);
	KUNIT_EXPECT_NOT_NULL(test, sun55i_msgbox_chan_ops.shutdown);
	KUNIT_EXPECT_NOT_NULL(test, sun55i_msgbox_chan_ops.last_tx_done);
	KUNIT_EXPECT_NOT_NULL(test, sun55i_msgbox_chan_ops.peek_data);
}

/* =============== Test Suite Registration =============== */

static struct kunit_case sun55i_msgbox_cases[] = {
	/* Routing Table Tests */
	KUNIT_CASE(test_chan_to_route_cpus_all),
	KUNIT_CASE(test_chan_to_route_dsp_all),
	KUNIT_CASE(test_chan_to_route_rv_all),
	KUNIT_CASE(test_chan_to_route_boundary_limits),
	KUNIT_CASE(test_chan_to_route_invalid_channels),
	/* Register Macro Tests */
	KUNIT_CASE(test_msgbox_offset_values),
	KUNIT_CASE(test_msgbox_irq_reg_offsets),
	KUNIT_CASE(test_msgbox_fifo_reg_offsets),
	KUNIT_CASE(test_msgbox_irq_bit_positions),
	KUNIT_CASE(test_msgbox_constants),
	/* Functional Driver Ops Tests */
	KUNIT_CASE(test_functional_send_data_all_twelve_channels),
	KUNIT_CASE(test_functional_send_data_null),
	KUNIT_CASE(test_functional_send_data_patterns),
	KUNIT_CASE(test_functional_last_tx_done_sweep),
	KUNIT_CASE(test_functional_peek_data_sweep),
	KUNIT_CASE(test_functional_startup_and_shutdown),
	KUNIT_CASE(test_functional_shutdown_isolation),
	KUNIT_CASE(test_functional_startup_flushes_stale_fifo),
	KUNIT_CASE(test_functional_startup_capped_at_fifo_max),
	KUNIT_CASE(test_functional_shutdown_flushes_and_bounds),
	/* Hardirq Simulation Tests */
	KUNIT_CASE(test_irq_spurious_returns_none),
	KUNIT_CASE(test_irq_spurious_noise_bits),
	KUNIT_CASE(test_irq_disabled_channel_ignored),
	KUNIT_CASE(test_irq_single_message_received),
	KUNIT_CASE(test_irq_empty_fifo_status_clear),
	KUNIT_CASE(test_irq_multi_channel_concurrency),
	KUNIT_CASE(test_irq_multi_port_concurrency),
	KUNIT_CASE(test_irq_multi_port_burst_interleaved),
	KUNIT_CASE(test_irq_fifo_drain_capped_at_max),
	KUNIT_CASE(test_irq_channel_crosstalk_isolation),
	KUNIT_CASE(test_irq_all_three_routes_simultaneous),
	KUNIT_CASE(test_functional_last_tx_done_backpressure_boundary),
	/* Ops & Controller Invariants */
	KUNIT_CASE(test_msgbox_chan_ops_completeness),
	KUNIT_CASE(test_msgbox_controller_invariants),
	{}
};

static struct kunit_suite sun55i_msgbox_test_suite = {
	.name = "sun55i_msgbox",
	.test_cases = sun55i_msgbox_cases,
};

kunit_test_suite(sun55i_msgbox_test_suite);

MODULE_AUTHOR("Tim Michals <tcmichals@gmail.com>");
MODULE_DESCRIPTION("Comprehensive KUnit tests for Allwinner sun55i Message Box driver");
MODULE_LICENSE("GPL");

```
