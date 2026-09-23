# Linux Kernel Maintainer Review Prompt for Google Gemini Pro

Copy and paste the entire block below into Google Gemini Pro (or another LLM) to get an upstream Linux / sunxi mailing list maintainer review.

***

```markdown
You are a senior Linux Kernel Maintainer reviewing patches for the `linux-remoteproc`, `linux-sunxi`, and `linux-arm-kernel` mailing lists (in the role of a seasoned upstream reviewer such as Mathieu Poirier, Bjorn Andersson, Krzysztof Kozlowski, or Chen-Yu Tsai).

Your task is to conduct an uncompromising, line-by-line kernel code review of the patch below, which refactors the Allwinner A523/A527 RemoteProc driver (`sunxi_rproc`) to follow the canonical NXP `imx_rproc.c` Address Translation Table (ATT) architecture.

### Background & Architecture Context:
1. **Target Hardware**: Allwinner A523/A527/T527 octa-core Cortex-A55 with an auxiliary T-Head XuanTie E907 RISC-V coprocessor.
2. **Silicon Memory Map**:
   - Dedicated Local SRAM Space 0: Host PA `0x07280000` (256 KB). Core DA views: `0x3ff80000` (TRM default), `0x3ffc0000` (alternate alias), `0x00020000` (legacy PubSRAM-C alias), and direct Host PA `0x07280000`.
   - Switchable Local SRAM Space 1 (SRAMA3_2): Host PA `0x072c0000` (256 KB). Core DA views: `0x40000000`, `0x40040000`, and direct Host PA `0x072c0000`.
   - Boot DRAM & Trace: Dynamically passed via Device Tree `reserved-memory` nodes (e.g. `0x48000000`, `0x50000000`).
3. **Problem Solved by This Patch**:
   - Previously, `sunxi_rproc.c` contained raw hardcoded hex literals (`0x3ff80000`, `0x40000000`) and ad-hoc alias arrays directly in `da_to_va()`.
   - This patch refactors the driver to follow the upstream NXP `imx_rproc.c` model:
     - Defines `struct sunxi_rproc_att` and flag `ATT_IOMEM`.
     - Places `sun55i_rproc_att[]` table in the SoC config (`struct sunxi_rproc_cfg`).
     - Implements `sunxi_rproc_da_to_sys()` to translate core DA -> system bus address (`sys`).
     - Updates `sunxi_rproc_da_to_va()` to resolve `sys` against mapped memory windows and fall back gracefully for DT carveouts.
     - Replaces all hardcoded hex literals with named `#define` macros (`E907_SRAM_*`, `SUN55I_SRAM_*`).
     - Prepares the driver for future Allwinner A733 (XuanTie E902) and Cadence Xtensa HiFi4 DSP support by data table addition only.

### Review Instructions:
Please review this patch under strict upstream Linux kernel standards:
1. **Address Translation Correctness & Security**:
   - Check `sunxi_rproc_da_to_sys()` and `sunxi_rproc_da_to_va()`.
   - Verify integer overflow guards (`da > U64_MAX - len`).
   - Check boundary checks (`(da + len) <= (att->da + att->size)` vs `<`).
   - Verify space isolation (can a Space 1 DA ever erroneously map to Space 0 or leak into unmapped memory?).
   - Check sparse annotations (`__force`, `__iomem`, pointer typing).
2. **Mainline Linux & `imx_rproc` Architectural Fidelity**:
   - Does this faithfully reproduce the canonical upstream `imx_rproc.c` ATT model?
   - Is `struct sunxi_rproc_att` properly formatted and extensible?
   - Will future Allwinner A733 (sun60i) E902 or HiFi4 DSP support integrate cleanly with just a table without modifying `da_to_va()`?
3. **Linux Kernel Coding Standards & Style**:
   - Checkpatch compliance (naming conventions, comment formatting, type safety).
   - Are the `#define` macros in `sunxi_rproc.h` clean, non-polluting, and properly typed (`UL`)?
4. **KUnit Test Coverage**:
   - Are edge cases, 1-byte upper boundary, unaligned lengths, overflow, and unmapped windows thoroughly tested?

Please categorize your findings by severity:
- `[Critical]` - Memory corruption, integer overflow, security bypass, kernel panic.
- `[High]` - Functional regression, address space aliasing/leak, race condition.
- `[Medium]` - Deviations from upstream driver design, maintainability issues, missing error checks.
- `[Low / Nitpick]` - Code style, comments, macro naming, documentation.

---

### Patch to Review:

```diff
diff --git a/drivers/remoteproc/sunxi_rproc.h b/drivers/remoteproc/sunxi_rproc.h
index ed4c430aaf43..5b516b5ac956 100644
--- a/drivers/remoteproc/sunxi_rproc.h
+++ b/drivers/remoteproc/sunxi_rproc.h
@@ -9,16 +9,50 @@
 #include <linux/remoteproc.h>
 #include <linux/reset.h>
 
-/* XuanTie core-local view of dedicated SRAM Spaces (E906/E907) */
-#define E907_SRAM_C_DA			0x00020000
-#define E907_SRAM_SPACE0_DA		0x3ff80000
-#define E907_SRAM_SPACE0_DA_ALT		0x3ffc0000
-#define E907_SRAM_SPACE1_DA		0x40000000
+/* XuanTie E906/E907 core-local view of dedicated SRAM Spaces (Allwinner A523/A527/T527) */
+#define E907_SRAM_C_DA			0x00020000UL
+#define E907_SRAM_SPACE0_DA		0x3ff80000UL
+#define E907_SRAM_SPACE0_DA_ALT		0x3ffc0000UL
+#define E907_SRAM_SPACE1_DA		0x40000000UL
+#define E907_SRAM_SPACE1_DA_ALT		0x40040000UL
+
+/* Allwinner A523/A527/T527 System Bus (Host Physical) Addresses & Window Sizes */
+#define SUN55I_SRAM_SPACE0_SYS		0x07280000UL
+#define SUN55I_SRAM_SPACE0_SIZE		0x00040000UL /* 256 KB */
+#define SUN55I_SRAM_SPACE1_SYS		0x072c0000UL
+#define SUN55I_SRAM_SPACE1_SIZE		0x00040000UL /* 256 KB */
+
+/* Address Translation Table flags */
+#define ATT_IOMEM			BIT(30)
+
+struct sunxi_rproc_att {
+	u64 da;
+	u64 sa;
+	size_t size;
+	int flags;
+};
+
+/* XuanTie CFG Block Register Offsets */
+#define E906_CTRL_REG			0x0000
+#define E906_STA_ADD_REG		0x0204
+
+/* Remap Control Register (offset 0x364 in PRCM_R_CCU / MCU_CCU) */
+#define SUNXI_REMAP_CTRL_OFFSET		0x0364
+/* Bit 0: 0 = local RAM for MCU; 1 = share for system */
+#define SUNXI_REMAP_MCU_RAM_BIT		BIT(0)
+/* Bit 1: 0 = SRAMA3_2 not shared; 1 = share for MCU_SYS */
+#define SUNXI_REMAP_SRAMA3_2_BIT	BIT(1)
 
 struct sunxi_rproc_cfg {
 	const char *name;
+	const struct sunxi_rproc_att *att;
+	size_t att_size;
+	bool has_remap_reg;
+	u32 boot_reg_offset;
 };
 
+extern const struct sunxi_rproc_cfg sun55i_riscv_cfg;
+
 struct sunxi_rproc {
 	struct rproc *rproc;
 	struct device *dev;
diff --git a/drivers/remoteproc/sunxi_rproc.c b/drivers/remoteproc/sunxi_rproc.c
index 06544ae24c0d..e11d71e8d134 100644
--- a/drivers/remoteproc/sunxi_rproc.c
+++ b/drivers/remoteproc/sunxi_rproc.c
@@ -27,21 +27,32 @@
 
 #define DRIVER_NAME "sunxi-rproc"
 
+static const struct sunxi_rproc_att sun55i_rproc_att[] = {
+	/* dev addr (remote)    , sys addr (host PA)    , size                   , flags */
+	/* Space 0 Core Aliases -> Space 0 Host PA */
+	{ E907_SRAM_SPACE0_DA,     SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
+	{ E907_SRAM_SPACE0_DA_ALT, SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
+	{ E907_SRAM_C_DA,          SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
+	{ SUN55I_SRAM_SPACE0_SYS,  SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
+
+	/* Space 1 Core Aliases -> Space 1 Host PA */
+	{ E907_SRAM_SPACE1_DA,     SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
+	{ E907_SRAM_SPACE1_DA_ALT, SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
+	{ SUN55I_SRAM_SPACE1_SYS,  SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
+};
+
+const struct sunxi_rproc_cfg sun55i_riscv_cfg = {
 	.name = "XuanTie E907 RISC-V",
+	.att = sun55i_rproc_att,
+	.att_size = ARRAY_SIZE(sun55i_rproc_att),
+	.has_remap_reg = true,
+	.boot_reg_offset = E906_STA_ADD_REG,
 };
 
+#if IS_ENABLED(CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST)
+EXPORT_SYMBOL_GPL(sun55i_riscv_cfg);
+#endif
+
 static void sunxi_rproc_vq_work(struct work_struct *work)
 {
 	struct sunxi_rproc *priv = container_of(work, struct sunxi_rproc, vq_work);
@@ -76,6 +87,7 @@ static irqreturn_t sunxi_rproc_crash_handler(int irq, void *data)
 int sunxi_rproc_prepare(struct rproc *rproc)
 {
 	struct sunxi_rproc *priv = rproc->priv;
+	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
 	int ret;
 
 	/* 1. Deassert configuration & SRAM bus resets */
@@ -149,7 +161,7 @@ int sunxi_rproc_prepare(struct rproc *rproc)
 	/*
 	 * 4b. Enable SRAMA3_2 for MCU_SYS (RISC-V) via REMAP_CTRL_REG bit 1.
 	 */
-	if (priv->remap_va) {
+	if (cfg->has_remap_reg && priv->remap_va) {
 		u32 remap_val = readl(priv->remap_va);
 
 		remap_val |= SUNXI_REMAP_SRAMA3_2_BIT;
@@ -205,9 +217,10 @@ EXPORT_SYMBOL_GPL(sunxi_rproc_prepare);
 int sunxi_rproc_unprepare(struct rproc *rproc)
 {
 	struct sunxi_rproc *priv = rproc->priv;
+	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
 
 	/* Symmetrical CCU unwinding */
-	if (priv->remap_va) {
+	if (cfg->has_remap_reg && priv->remap_va) {
 		u32 remap_val = readl(priv->remap_va);
 
 		remap_val &= ~SUNXI_REMAP_SRAMA3_2_BIT;
@@ -248,10 +261,11 @@ EXPORT_SYMBOL_GPL(sunxi_rproc_unprepare);
 int sunxi_rproc_start(struct rproc *rproc)
 {
 	struct sunxi_rproc *priv = rproc->priv;
+	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
 	int ret;
 
 	dev_info(priv->dev, "Starting %s core at entry 0x%llx\n",
-		 priv->cfg ? priv->cfg->name : "remote", (u64)rproc->bootaddr);
+		 cfg->name ? cfg->name : "remote", (u64)rproc->bootaddr);
 
 	if (rproc->bootaddr > U32_MAX)
 		return -EINVAL;
@@ -286,7 +300,7 @@ int sunxi_rproc_start(struct rproc *rproc)
 
 	/* Program boot vector now that the CFG block bus is live */
 	if (priv->cfg_va) {
-		writel((u32)rproc->bootaddr, priv->cfg_va + E906_STA_ADD_REG);
+		writel((u32)rproc->bootaddr, priv->cfg_va + cfg->boot_reg_offset);
 		dev_dbg(priv->dev, "STA_ADD set to 0x%08x\n", (u32)rproc->bootaddr);
 	}
 
@@ -300,9 +314,10 @@ EXPORT_SYMBOL_GPL(sunxi_rproc_start);
 int sunxi_rproc_stop(struct rproc *rproc)
 {
 	struct sunxi_rproc *priv = rproc->priv;
+	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
 
 	dev_info(priv->dev, "Halting %s core...\n",
-		 priv->cfg ? priv->cfg->name : "remote");
+		 cfg->name ? cfg->name : "remote");
 
 	/*
 	 * Assert reset first so the core stops generating mailbox interrupts,
@@ -356,9 +371,32 @@ void sunxi_rproc_kick(struct rproc *rproc, int vqid)
 EXPORT_SYMBOL_GPL(sunxi_rproc_kick);
 #endif
 
+static int sunxi_rproc_da_to_sys(struct sunxi_rproc *priv, u64 da,
+				 size_t len, u64 *sys, bool *is_iomem)
+{
+	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
+	size_t i;
+
+	if (cfg->att) {
+		for (i = 0; i < cfg->att_size; i++) {
+			const struct sunxi_rproc_att *att = &cfg->att[i];
+
+			if (da >= att->da && (da + len) <= (att->da + att->size)) {
+				*sys = att->sa + (da - att->da);
+				if (is_iomem)
+					*is_iomem = !!(att->flags & ATT_IOMEM);
+				return 0;
+			}
+		}
+	}
+
+	return -ENOENT;
+}
+
 void *sunxi_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
 {
 	struct sunxi_rproc *priv = rproc->priv;
+	u64 sys;
 
 	if (len == 0)
 		return NULL;
@@ -373,85 +411,57 @@ void *sunxi_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iom
 		return NULL;
 
 	/*
-	 * 1. Dedicated MCU Local SRAM Space 0 (Resource "r_sram" / "sram")
-	 *
-	 * Valid core-local DA aliases for Space 0 on XuanTie E907:
-	 *   Host PA      (e.g. 0x07280000 — as seen by the ARM host)
-	 *   0x3ff80000   (E907_SRAM_SPACE0_DA, primary TRM alias)
-	 *   0x3ffc0000   (E907_SRAM_SPACE0_DA_ALT, secondary alias)
-	 *   0x00020000   (PubSRAM-C alias used by older E906 firmware)
-	 *
-	 * 0x40000000 (E907_SRAM_SPACE1_DA) is NOT a Space 0 alias — it
-	 * belongs exclusively to Space 1 (r_sram1). Including it here
-	 * would silently redirect Space 1 accesses into the wrong window.
+	 * 1. Translate core-local device addresses (DA) to system bus
+	 * addresses (Host PA) using the SoC address translation table (ATT).
 	 */
-	if (priv->r_sram_va) {
-		/* Host physical address view */
-		if (da >= priv->r_sram_phys &&
-		    (da + len) <= (priv->r_sram_phys + priv->r_sram_size)) {
-			if (is_iomem)
-				*is_iomem = true;
-			return (__force void *)(priv->r_sram_va + (da - priv->r_sram_phys));
-		}
-		/* High SRAM Space 0 views (0x3ff80000 / 0x3ffc0000) */
-		if (da >= 0x3ff80000 && (da + len) <= (0x3ff80000 + priv->r_sram_size)) {
-			if (is_iomem)
-				*is_iomem = true;
-			return (__force void *)(priv->r_sram_va + (da - 0x3ff80000));
-		}
-		if (da >= 0x3ffc0000 && (da + len) <= (0x3ffc0000 + priv->r_sram_size)) {
-			if (is_iomem)
-				*is_iomem = true;
-			return (__force void *)(priv->r_sram_va + (da - 0x3ffc0000));
-		}
-		/* PubSRAM C DA view (0x00020000) */
-		if (da >= 0x00020000 && (da + len) <= (0x00020000 + priv->r_sram_size)) {
-			if (is_iomem)
-				*is_iomem = true;
-			return (__force void *)(priv->r_sram_va + (da - 0x00020000));
-		}
+	if (sunxi_rproc_da_to_sys(priv, da, len, &sys, is_iomem) == 0) {
+		if (priv->r_sram_va && sys >= priv->r_sram_phys &&
+		    (sys + len) <= (priv->r_sram_phys + priv->r_sram_size))
+			return (__force void *)(priv->r_sram_va + (sys - priv->r_sram_phys));
+
+		if (priv->r_sram1_va && sys >= priv->r_sram1_phys &&
+		    (sys + len) <= (priv->r_sram1_phys + priv->r_sram1_size))
+			return (__force void *)(priv->r_sram1_va + (sys - priv->r_sram1_phys));
+
+		if (priv->dram_va && sys >= priv->dram_phys &&
+		    (sys + len) <= (priv->dram_phys + priv->dram_size))
+			return (__force void *)(priv->dram_va + (sys - priv->dram_phys));
+
+		if (priv->trace_va && sys >= priv->trace_phys &&
+		    (sys + len) <= (priv->trace_phys + priv->trace_size))
+			return (__force void *)(priv->trace_va + (sys - priv->trace_phys));
 	}
 
-	/* 2. Switchable MCU Local SRAM Space 1 ("r_sram1", Core DA 0x40040000) */
-	if (priv->r_sram1_va) {
-		/* Host physical address view (e.g., 0x072c0000 or 0x07280000) */
-		if (da >= priv->r_sram1_phys &&
-		    (da + len) <= (priv->r_sram1_phys + priv->r_sram1_size)) {
-			if (is_iomem)
-				*is_iomem = true;
-			return (__force void *)(priv->r_sram1_va + (da - priv->r_sram1_phys));
-		}
-		/* Core DA view: 0x40000000 (Space 1) and 0x40040000 */
-		if (da >= 0x40000000 &&
-		    (da + len) <= (0x40000000 + priv->r_sram1_size)) {
-			if (is_iomem)
-				*is_iomem = true;
-			return (__force void *)(priv->r_sram1_va + (da - 0x40000000));
-		}
-		if (da >= 0x40040000 &&
-		    (da + len) <= (0x40040000 + priv->r_sram1_size)) {
-			if (is_iomem)
-				*is_iomem = true;
-			return (__force void *)(priv->r_sram1_va + (da - 0x40040000));
-		}
+	/*
+	 * 2. Device Tree Memory Regions (Trace buffer, DRAM carveout, or
+	 * dynamically-assigned SRAM regions whose host PA is supplied via DT).
+	 */
+	if (priv->trace_va && da >= priv->trace_phys &&
+	    (da + len) <= (priv->trace_phys + priv->trace_size)) {
+		if (is_iomem)
+			*is_iomem = false;
+		return (__force void *)(priv->trace_va + (da - priv->trace_phys));
 	}
 
-	/* 4. Trace / Reserved Memory (from Device Tree) */
-	if (priv->trace_va) {
-		if (da >= priv->trace_phys && (da + len) <= (priv->trace_phys + priv->trace_size)) {
-			if (is_iomem)
-				*is_iomem = false;
-			return priv->trace_va + (da - priv->trace_phys);
-		}
+	if (priv->dram_va && da >= priv->dram_phys &&
+	    (da + len) <= (priv->dram_phys + priv->dram_size)) {
+		if (is_iomem)
+			*is_iomem = false;
+		return (__force void *)(priv->dram_va + (da - priv->dram_phys));
 	}
 
-	/* 5. Boot DRAM Carveout (Resource "dram" - Core/Host 0x40014000) */
-	if (priv->dram_va) {
-		if (da >= priv->dram_phys && (da + len) <= (priv->dram_phys + priv->dram_size)) {
-			if (is_iomem)
-				*is_iomem = false;
-			return priv->dram_va + (da - priv->dram_phys);
-		}
+	if (priv->r_sram_va && da >= priv->r_sram_phys &&
+	    (da + len) <= (priv->r_sram_phys + priv->r_sram_size)) {
+		if (is_iomem)
+			*is_iomem = true;
+		return (__force void *)(priv->r_sram_va + (da - priv->r_sram_phys));
+	}
+
+	if (priv->r_sram1_va && da >= priv->r_sram1_phys &&
+	    (da + len) <= (priv->r_sram1_phys + priv->r_sram1_size)) {
+		if (is_iomem)
+			*is_iomem = true;
+		return (__force void *)(priv->r_sram1_va + (da - priv->r_sram1_phys));
 	}
 
 	/*
diff --git a/drivers/remoteproc/sunxi_rproc_test.c b/drivers/remoteproc/sunxi_rproc_test.c
index 548c77aa7695..2c9996d9f8d9 100644
--- a/drivers/remoteproc/sunxi_rproc_test.c
+++ b/drivers/remoteproc/sunxi_rproc_test.c
@@ -37,10 +37,10 @@
 #define FAKE_TRACE_VA	((void *)0xD0000000UL)
 
 /* Standard A527 hardware parameters */
-#define A527_SRAM_PHYS		0x07280000ULL
-#define A527_SRAM_SIZE		0x40000		/* 256 KB */
-#define A527_SRAM1_PHYS		0x072C0000ULL
-#define A527_SRAM1_SIZE		0x40000		/* 256 KB */
+#define A527_SRAM_PHYS		SUN55I_SRAM_SPACE0_SYS
+#define A527_SRAM_SIZE		SUN55I_SRAM_SPACE0_SIZE
+#define A527_SRAM1_PHYS		SUN55I_SRAM_SPACE1_SYS
+#define A527_SRAM1_SIZE		SUN55I_SRAM_SPACE1_SIZE
 #define A527_DRAM_PHYS		0x48000000ULL
 #define A527_DRAM_SIZE		0x100000	/* 1 MB */
 #define A527_TRACE_PHYS		0x50000000ULL
```
```
