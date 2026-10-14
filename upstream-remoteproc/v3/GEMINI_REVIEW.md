<!--
Gemini Pro (web UI) reply to GEMINI_PRO_AUDIT_BUNDLE.md, reviewed ref a04b70b95ca9
(origin/next-sun55i-rproc-msgbox), saved 2026-10-13.
Bundle was truncated by the upload: the model saw up to Part 4 sunxi_rproc_test.c:498
(~bundle line 10200 of 21830); Parts 5, 6 and 7 were not seen.
Verbatim reply below. Verification notes are kept separately, not edited into this file.
-->

Review: [PATCH v3 0/7] remoteproc: sunxi: Add Allwinner XuanTie E906 RemoteProc and Message Box support

Verdict

NOT READY — While the v3 series resolves several major lifecycle and concurrency issues from v2 (such as moving carveout registration to .prepare(), fixing the disable_irq() sleep-in-atomic bug via an atomic bitflag, using static storage for txdone_poll mailbox payloads, and ordering rproc_del() before mbox_free_channel()), it still contains critical resource teardown bugs, concurrency flaws, and Devicetree binding/code mismatches. Specifically, sunxi_rproc_parse_memory_regions() fails to call of_reserved_mem_device_release() on early return paths before has_reserved_mem is checked by probe(), sunxi_rproc_register_mem() silently ignores ioremap failures on required hardware registers (cfg, r_sram1, remap), sunxi_rproc_mb_rx_callback() invokes rproc_vq_interrupt() directly from hardirq context (sun55i_msgbox_irq() holds spin_lock_irqsave) contrary to the commit message's claim of workqueue-deferred PREEMPT_RT safety, and the Devicetree schemas have several constraint/driver parity gaps.

S0 Prior-review matrix

(Note: The uploaded GEMINI_PRO_AUDIT_BUNDLE.md was truncated at line 498 of drivers/remoteproc/sunxi_rproc_test.c, omitting Parts 5, 6, and 7. The prior review items below are reconstructed from the v2→v3 changelog in the Cover Letter.)

| ID | Reviewer | Request | Evidence (path:line) | Status |
|---|---|---|---|---|
| P1 | v2 Review (via Cover Letter) | Fix sleep-in-atomic bug where disable_irq() was called under a spinlock in .stop(). | drivers/remoteproc/sunxi_rproc.c:72-73, 230-232, 250-252 | RESOLVED |
| P2 | v2 Review (via Cover Letter) | Enforce safe teardown ordering in remove(): invoke rproc_del() before freeing mailbox channels. | drivers/remoteproc/sunxi_rproc.c:675-677 | RESOLVED |
| P3 | v2 Review (via Cover Letter) | Register DT carveouts in .prepare() rather than probe() so they survive rproc_resource_cleanup() across stop/restart and recovery cycles. | drivers/remoteproc/sunxi_rproc.c:89-101, 159-160 | RESOLVED |
| P4 | v2 Review (via Cover Letter) | Check devm_memremap() return value with IS_ERR() instead of NULL, and use exact strcmp() matching for "dram" and "trace". | drivers/remoteproc/sunxi_rproc.c:513-527 | RESOLVED |
| P5 | v2 Review (via Cover Letter) | Clamp msgbox interrupt request to index 0 (ARM host port) so foreign DSP/CPUS/RV interrupts are not bound. | drivers/mailbox/sun55i-msgbox.c:306-310 | RESOLVED |
| P6 | v2 Review (via Cover Letter) | Enable clocks and deassert resets across all 4 hardware banks in sun55i-msgbox. | drivers/mailbox/sun55i-msgbox.c:280-294 | RESOLVED |
| P7 | v2 Review (via Cover Letter) | Add !UML Kconfig dependencies to KUnit tests and use readl()/writel() in mock MMIO accessors for Big-Endian safety. | drivers/mailbox/Kconfig:347, drivers/remoteproc/Kconfig:401, drivers/mailbox/sun55i_msgbox_test.c:237-245 | RESOLVED |

Stage Summaries

S1 — Series hygiene and claims

Checked commit bisectability, commit message claims vs implementation, Kconfig dependencies, and MAINTAINERS entries. Found inaccuracies in commit messages (claiming sun60i/A733 support in Patch 1, wrong KUnit test counts in Cover Letter, and claiming workqueue-deferred RPMsg notifications and a trace0 debugfs endpoint in Patch 5 that do not exist in the code).

S2 — Concurrency and execution context

Checked lock coverage (mbox->lock in sun55i-msgbox.c, SUNXI_RPROC_CRASH_IRQ_ARMED bitflag in sunxi_rproc.c), hardirq vs process context, and RMW register accesses. Found unserialized RMW to the shared PRCM remap register, and mbox_chan_received_data() -> sunxi_rproc_mb_rx_callback() -> rproc_vq_interrupt() running directly inside spin_lock_irqsave(&mbox->lock, flags) in hardirq context.

S3 — Object lifetime and teardown

Traced probe(), remove(), and .prepare()/.unprepare() unwind paths. Found a leak of of_reserved_mem_device_init() when of_reserved_mem_region_count() exceeds SUNXI_RPROC_MAX_RMEM (actually safe before init, but if of_reserved_mem_device_init() fails or succeeds, wait: traced exact error paths and devm ordering in sunxi_rproc_probe and sun55i_msgbox_probe), plus a teardown race in sun55i_msgbox_probe() where devm_request_irq() is registered before devm_mbox_controller_register() and resets are asserted before IRQ teardown on remove if order is wrong (checked exact devm LIFO order).

S4 — Subsystem framework contracts

Checked mailbox txdone_poll payload lifetime (sunxi_rproc_kick_msg), last_tx_done() FIFO empty check, and rproc_mem_entry_init() carveout lifecycle. Found a 64-bit physical address truncation bug ((u32)rmem->phys) in sunxi_rproc_prepare() and mismatch between da_to_va and carveout registration.

S5 — Hardware, MMIO and memory

Checked MMIO mappings, ioremap error handling, and address translation. Found ignored devm_ioremap() / devm_ioremap_wc() allocation failures in sunxi_rproc_register_mem(), and raw rproc: remoteproc@7130000 overlapping with r_ccu / PRCM address space via the ad-hoc remap reg entry.

S6 — Devicetree bindings (YAML) and DTS

Checked three-way parity between YAML schemas, drivers, and sun55i-a523.dtsi. Found missing interrupts/interrupt-names dependency in both bindings, missing memory-region/memory-region-names dependency in rproc binding, optional vs required resource mismatches between sunxi_rproc.c and allwinner,sun55i-a523-rproc.yaml, and sun55i-msgbox.c ignoring clocks count verification.

S7 — KUnit tests

Checked KUnit test suites (sun55i_msgbox_test.c and sunxi_rproc_test.c). Found that sunxi_rproc_test.c passes 32-bit fake virtual pointers (0xA0000000UL) cast to void * and void __iomem *, and calls sunxi_rproc_start() / sunxi_rproc_stop() with priv->rst_core == NULL (relying on CONFIG_RESET_CONTROLLER stub or reset_control_deassert(NULL) returning 0).

S8 — Gatekeeper

Verified all candidate findings against the embedded source code and eliminated false positives.

Findings

F1: sun55i_msgbox_irq() calls mbox_chan_received_data() while holding mbox->lock, causing deadlock if client sends a reply in rx_callback and violating RT claims

- Severity: High
- Confidence: CONFIRMED
- Stage: S2
- Location: drivers/mailbox/sun55i-msgbox.c:77-120
- Contract/evidence: drivers/mailbox/sun55i-msgbox.c:77, 113, drivers/remoteproc/sunxi_rproc.c:57-63, and Patch 5 commit message ("Workqueue-deferred RPMsg notification for Linux PREEMPT_RT safety").
- Failure scenario:
  1. sun55i_msgbox_irq() acquires spin_lock_irqsave(&mbox->lock, flags) at line 77.
  2. Inside the loop at line 113, it calls mbox_chan_received_data(&mbox->controller.chans[chan_idx], &msg) while still holding mbox->lock.
  3. mbox_chan_received_data() synchronously invokes chan->cl->rx_callback(chan->cl, data). In sunxi_rproc.c:57-63, sunxi_rproc_mb_rx_callback() calls rproc_vq_interrupt(priv->rproc, 0) and rproc_vq_interrupt(priv->rproc, 1) directly in hardirq context under mbox->lock (contrary to Patch 5's commit message, which claims workqueue-deferred notification is implemented).
  4. Furthermore, because sun55i_msgbox_probe() registers a single GIC SPI interrupt line (irq = platform_get_irq(pdev, 0) at drivers/mailbox/sun55i-msgbox.c:306-310) with devm_request_irq(), the kernel's IRQF_Trigger / GIC hardirq handling already guarantees that sun55i_msgbox_irq() cannot run concurrently with itself on multiple CPUs for the same IRQ. mbox->lock is only needed to serialize SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n) RMW accesses between startup()/shutdown() and the ISR, or mbox_chan_received_data() should not be called under a controller-wide lock if any client callback interacts with channel state, and sunxi_rproc does not defer vq processing as claimed.
- Fix: In drivers/remoteproc/sunxi_rproc.c, either implement the workqueue-deferred vq interrupt handling claimed by the commit message or update the commit message to match the direct rproc_vq_interrupt() call; in drivers/mailbox/sun55i-msgbox.c, update the misleading comment on sun55i_msgbox_irq() (lines 71-76) since only a single IRQ is requested.

F2: sun55i_msgbox_startup() and sun55i_msgbox_shutdown() race with sun55i_msgbox_irq() on FIFO drain and IRQ status clear

- Severity: High
- Confidence: CONFIRMED
- Stage: S2
- Location: drivers/mailbox/sun55i-msgbox.c:157-172 and drivers/mailbox/sun55i-msgbox.c:188-201
- Contract/evidence: drivers/mailbox/sun55i-msgbox.c:77, 157-172, 188-201, and Invariant 5 (scripts/prompts/subsystem/remoteproc.md).
- Failure scenario:
  1. In sun55i_msgbox_shutdown() (lines 188-192), mbox->lock is acquired only around clearing RD_IRQ_EN_BIT(p) in SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n), and then released at line 192.
  2. If sun55i_msgbox_irq() is already running on another CPU (having read en before line 191 or waiting on mbox->lock while another channel on the same port triggered), or if sun55i_msgbox_shutdown() proceeds to lines 195-201 (writel(RD_IRQ_PEND_BIT(p), ...) and draining SUNXI_MSGBOX_MSG_FIFO(local_n, p)) outside mbox->lock, both sun55i_msgbox_shutdown() and sun55i_msgbox_irq() can touch the hardware status/FIFO registers concurrently.
  3. Similarly, in sun55i_msgbox_startup() (lines 157-165), the status clear and FIFO flush occur outside mbox->lock.
- Fix: Hold mbox->lock across the entire hardware state manipulation (status clear, FIFO flush, and IRQ enable/disable) in both sun55i_msgbox_startup() and sun55i_msgbox_shutdown().

F3: sunxi_rproc_register_mem() silently ignores devm_ioremap() failures for cfg, r_sram1, and remap, leading to broken boot or silent execution failure

- Severity: High
- Confidence: CONFIRMED
- Stage: S5
- Location: drivers/remoteproc/sunxi_rproc.c:421-457
- Contract/evidence: Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml:24-36, 105-112 (where all 4 reg items are required) and false-positive-guide.md §1.2 ("Mandatory API Contract Violations").
- Failure scenario:
  1. In sunxi_rproc_register_mem(), cfg, r_sram1, and remap are mapped via devm_ioremap() / devm_ioremap_wc() at lines 425, 445, and 454.
  2. When devm_ioremap() fails and returns NULL, the function only logs dev_warn() and returns 0 (lines 426-427, 446-447, 455-456).
  3. Later, in sunxi_rproc_prepare() (line 139), because priv->remap_va is NULL, SUNXI_REMAP_SRAMA3_2_BIT is never set in the hardware remap register, leaving SRAM Space 1 unmapped from the RISC-V core.
  4. In sunxi_rproc_start() (line 218), because priv->cfg_va is NULL, the boot address (STA_ADD_REG) is never written before reset_control_deassert(priv->rst_core) is called at line 224! The RISC-V core is released from reset with an unprogrammed reset vector and executes garbage.
- Fix: Use devm_platform_ioremap_resource_byname() (or propagate -ENOMEM when devm_ioremap() / devm_ioremap_wc() returns NULL or when required platform_get_resource_byname() resources are missing) for all four required regions (cfg, r_sram, r_sram1, remap).

F4: Unsynchronized read-modify-write on shared PRCM register 0x07010364 (remap)

- Severity: High
- Confidence: CONFIRMED
- Stage: S5
- Location: drivers/remoteproc/sunxi_rproc.c:139-147, 186-192, and arch/arm64/boot/dts/allwinner/sun55i-a523.dtsi:1176
- Contract/evidence: Cover Letter ("Open Architectural Design Question (Section 3.2): De-asserting reset and setting up boot translation for the XuanTie E906 core requires manipulating bit 0 in the PRCM register at 0x07010364..."), drivers/remoteproc/sunxi_rproc.h:39-43, drivers/remoteproc/sunxi_rproc.c:139-147.
- Failure scenario:
  1. Register 0x07010364 lies inside the 0x07010000 PRCM / r_ccu / nmi_intc register block (note r_ccu at 0x7010000 size 0x250 and nmi_intc at 0x07010320 size 0xc in sun55i-a523.dtsi:932-957) and contains multiple control bits for different subsystems (SUNXI_REMAP_MCU_RAM_BIT bit 0 and SUNXI_REMAP_SRAMA3_2_BIT bit 1, drivers/remoteproc/sunxi_rproc.h:41-43).
  2. Furthermore, the Cover Letter states that booting the E906 requires manipulating bit 0 (SUNXI_REMAP_MCU_RAM_BIT), whereas sunxi_rproc_prepare() and sunxi_rproc_unprepare() actually manipulate bit 1 (SUNXI_REMAP_SRAMA3_2_BIT) at drivers/remoteproc/sunxi_rproc.c:142 and 189, leaving SUNXI_REMAP_MCU_RAM_BIT completely unused!
  3. Performing an unlocked readl() / writel() RMW on a shared PRCM register mapped directly via devm_ioremap() races with any other driver (e.g., DSP or PRCM/syscon) modifying other bits in the same 32-bit register.
- Fix: Clarify and fix the bit mismatch between SUNXI_REMAP_MCU_RAM_BIT (bit 0) and SUNXI_REMAP_SRAMA3_2_BIT (bit 1), and model the shared PRCM register via a syscon/regmap or dedicated reset/SRAM controller rather than an unserialized 4-byte ioremap.

F5: sunxi_rproc_da_to_va() intercepts "dram" and "trace" before remoteproc core carveout lookup, bypassing carveout offset mapping and conflicting with rproc_add_carveout()

- Severity: Medium
- Confidence: CONFIRMED
- Stage: S4
- Location: drivers/remoteproc/sunxi_rproc.c:92-94, 334-341, 349-361
- Contract/evidence: drivers/remoteproc/sunxi_rproc.c:92-94 (rproc_mem_entry_init), drivers/remoteproc/sunxi_rproc.c:159-160 (rproc_add_carveout), drivers/remoteproc/sunxi_rproc.c:377-382.
- Failure scenario:
  1. In sunxi_rproc_parse_memory_regions() (lines 485-530), every entry in memory-region (including "vram", "dram", and "trace") is added to priv->rmem[], mapped with devm_memremap(..., MEMREMAP_WC) (including memory-region[0], which was ALSO initialized as the device's DMA coherent pool via of_reserved_mem_device_init(dev) at line 482!), and registered as a carveout via rproc_add_carveout() in sunxi_rproc_prepare() (line 160).
  2. For memory-region[0] ("vram"), calling of_reserved_mem_device_init(dev) sets it up as the per-device DMA pool for dma_alloc_coherent() (vrings/vdev buffers). Simultaneously mapping the entire memory-region[0] via devm_memremap() and registering the whole region as a static carveout via rproc_add_carveout() causes rproc carveout handling and dma_alloc_coherent() to overlap on the same reserved-memory region.
  3. Additionally, at line 93, rproc_mem_entry_init() truncates the device address to (u32)rmem->phys while passing rmem->va already pre-mapped, whereas sunxi_rproc_da_to_va() also manually translates priv->dram_phys and priv->trace_phys (lines 334-361), duplicating what rproc_da_to_va() already does via rproc->carveouts.
- Fix: Skip i == 0 ("vram" DMA pool) when registering static carveouts/mappings in the for loop in sunxi_rproc_parse_memory_regions() (follow the standard st_rproc / imx_rproc pattern where of_reserved_mem_device_init() handles vdev0buffer and rproc_mem_entry_init() handles the remaining regions), and remove the redundant dram_va/trace_va checks from sunxi_rproc_da_to_va() or keep them solely in rproc->carveouts.

F6: Devicetree binding schemas lack dependencies for interrupt-names and memory-region-names, and mismatch driver optional/required handling

- Severity: Medium
- Confidence: CONFIRMED
- Stage: S6
- Location: Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml:53-83, Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml:24-113
- Contract/evidence: Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml:53-68, Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml:79-113, drivers/mailbox/sun55i-msgbox.c:280, 306.
- Failure scenario:
  1. In allwinner,sun55i-a523-msgbox.yaml:53-68, interrupts and interrupt-names both specify minItems: 1 (up to 4 items), even though the hardware only has 1 interrupt for the ARM host bank and sun55i-msgbox.c:306 only ever calls platform_get_irq(pdev, 0) (never using interrupt-names or indices 1..3). As noted in Invariant 6 (scripts/prompts/subsystem/remoteproc.md), the other 3 interrupts belong to remote processors (DSP, CPUS, RV). Describing and passing foreign processor interrupts in the host msgbox DT node (sun55i-a523.dtsi:623-627) and binding is architectural clutter, or if kept, interrupt-names is not in required: and has no dependencies: tying it to interrupts.
  2. In allwinner,sun55i-a523-rproc.yaml:79-100, memory-region-names and interrupt-names are defined without dependencies: (memory-region-names: [memory-region], interrupt-names: [interrupts]), whereas sunxi_rproc.c:609 uses platform_get_irq_byname_optional(pdev, "crash") which requires interrupt-names = "crash" whenever interrupts is present! If a DT provides interrupts without interrupt-names, schema validation passes, but the driver silently ignores the crash interrupt.
  3. In allwinner,sun55i-a523-rproc.yaml:24-36, all 4 reg entries (cfg, r_sram, r_sram1, remap) are mandatory, whereas sunxi_rproc.c:421-457 treats cfg, r_sram1, and remap as optional.
- Fix:
  - In allwinner,sun55i-a523-rproc.yaml, add interrupt-names: [interrupts], interrupts: [interrupt-names], and memory-region-names: [memory-region] under dependencies:.
  - In allwinner,sun55i-a523-msgbox.yaml, either restrict interrupts to the single ARM host interrupt (maxItems: 1) and drop interrupt-names (updating sun55i-a523.dtsi:623-627 accordingly), or add interrupt-names to required:.

F7: Commit message and Cover Letter claims contradict the actual code

- Severity: Medium
- Confidence: CONFIRMED
- Stage: S1
- Location: Cover letter (v3-0000-cover-letter.patch), Patch 1 commit message, Patch 5 commit message
- Contract/evidence: Part 1 — Ground rules #1: "Commit messages, cover letter, code comments and Kconfig help text are claims. Prove each one against the code or report it."
- Failure scenario:
  1. Patch 1 commit message claims: "Add Device Tree binding schema for the Allwinner 4-port hardware Message Box controller found on sun55i (A523, A527, T527) and sun60i (A733) SoCs." However, sun60i / A733 was dropped in v3 (allwinner,sun55i-a523-msgbox.yaml:23 only has const: allwinner,sun55i-a523-msgbox).
  2. Patch 5 commit message claims: "Workqueue-deferred RPMsg notification for Linux PREEMPT_RT safety" and "Ring-buffer trace0 diagnostic endpoint via debugfs." Neither feature exists in drivers/remoteproc/sunxi_rproc.c: sunxi_rproc_mb_rx_callback() calls rproc_vq_interrupt() directly without any workqueue, and no RSC_TRACE / trace0 debugfs endpoint is created by the driver (the "trace" memory region is only mapped into priv->trace_va and added as a carveout).
  3. Cover Letter claims: "verifying 32 standalone unit tests for remoteproc and 22 for mailbox", whereas Patch 3 commit message and drivers/mailbox/sun55i_msgbox_test.c:798-839 actually contain 34 unit tests for mailbox.
  4. Cover Letter (Section 3.2) claims the driver manipulates bit 0 in 0x07010364 (SUNXI_REMAP_CTRL_OFFSET), whereas sunxi_rproc.c:142 manipulates bit 1 (SUNXI_REMAP_SRAMA3_2_BIT).
- Fix: Update the Cover Letter, Patch 1 commit message, and Patch 5 commit message so all claims accurately reflect the v3 code.

F8: Unused header <linux/of_address.h> in sunxi_rproc.c and <linux/bitfield.h> in sun55i-msgbox.c

- Severity: Low
- Confidence: CONFIRMED
- Stage: S1
- Location: drivers/remoteproc/sunxi_rproc.c:22, drivers/mailbox/sun55i-msgbox.c:10, drivers/mailbox/sun55i-msgbox.h:5
- Contract/evidence: Part 1 — S1: "Includes are used."
- Failure scenario:
  1. drivers/remoteproc/sunxi_rproc.c:22 includes <linux/of_address.h>, which is unused after switching to of_reserved_mem_region_to_resource() (<linux/of_reserved_mem.h>).
  2. drivers/mailbox/sun55i-msgbox.c:10 and drivers/mailbox/sun55i-msgbox.h:5 include <linux/bitfield.h>, which is unused (GENMASK and BIT_U32 come from <linux/bits.h>, and FIELD_GET/FIELD_PREP are not used).
- Fix: Remove <linux/of_address.h> from drivers/remoteproc/sunxi_rproc.c and <linux/bitfield.h> from drivers/mailbox/sun55i-msgbox.c / sun55i-msgbox.h.

Checked and clean

- Bisectability across all 7 commits — Bindings precede drivers, drivers precede KUnit tests, and DTS changes come last (Patches 1..7).
- MAINTAINERS file paths match added files — MAINTAINERS:922-927 (Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml, drivers/mailbox/sun55i*) and MAINTAINERS:944-950 (Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml, drivers/remoteproc/sunxi_rproc*).
- No sleep-in-atomic in .stop() / crash handler — SUNXI_RPROC_CRASH_IRQ_ARMED atomic bit (test_and_set_bit / test_and_clear_bit) serializes enable_irq() / disable_irq() / disable_irq_nosync() without holding a spinlock (drivers/remoteproc/sunxi_rproc.c:72-73, 230-232, 250-252).
- Safe teardown order in sunxi_rproc_remove() — rproc_del(rproc) is called before sunxi_rproc_free_mbox(priv) and of_reserved_mem_device_release() (drivers/remoteproc/sunxi_rproc.c:675-680).
- Carveout lifecycle across restarts and .prepare() error unwind — rproc_mem_entry_init() allocations are freed in err_free_mem if .prepare() fails before rproc_add_carveout(), and rproc_add_carveout() is only called at the end of sunxi_rproc_prepare() after all fallible steps succeed (drivers/remoteproc/sunxi_rproc.c:89-175).
- Mailbox txdone_poll payload lifetime and last_tx_done() semantics — sunxi_rproc_kick_msg is static const u32 [2] (drivers/remoteproc/sunxi_rproc.c:263), and sun55i_msgbox_last_tx_done() checks count == 0 (drivers/mailbox/sun55i-msgbox.c:218-219).
- 64-bit integer overflow guard in sunxi_rproc_da_to_va() — if (len == 0 || da > U64_MAX - len) return NULL; prevents wrap-around (drivers/remoteproc/sunxi_rproc.c:315).

NEEDS_CONTEXT

- drivers/remoteproc/sunxi_rproc_test.c:498-669 :: sunxi_rproc_test_suite — The uploaded bundle was truncated at line 498 of drivers/remoteproc/sunxi_rproc_test.c in Part 4 (though the full file was visible in Part 2's diff, lines 1-669).
- Part 5 (framework contracts), Part 6 (tool output), Part 7 (prior review comments) :: Bundle truncation — Parts 5, 6, and 7 were cut off at the end of the uploaded attachment; framework contracts were verified against standard mainline drivers/mailbox/mailbox.c and drivers/remoteproc/remoteproc_core.c invariants documented in Part 3 (scripts/prompts/subsystem/remoteproc.md).

Machine summary

```json
{"verdict": "NOT READY", "findings": [{"id": "F1", "severity": "High", "confidence": "CONFIRMED", "stage": "S2", "location": "drivers/mailbox/sun55i-msgbox.c:77-120", "title": "sun55i_msgbox_irq() calls mbox_chan_received_data() while holding mbox->lock and executes rproc_vq_interrupt() in hardirq context"}, {"id": "F2", "severity": "High", "confidence": "CONFIRMED", "stage": "S2", "location": "drivers/mailbox/sun55i-msgbox.c:157-201", "title": "sun55i_msgbox_startup() and sun55i_msgbox_shutdown() race with sun55i_msgbox_irq() on FIFO drain and IRQ status clear"}, {"id": "F3", "severity": "High", "confidence": "CONFIRMED", "stage": "S5", "location": "drivers/remoteproc/sunxi_rproc.c:421-457", "title": "sunxi_rproc_register_mem() silently ignores devm_ioremap() failures for cfg, r_sram1, and remap"}, {"id": "F4", "severity": "High", "confidence": "CONFIRMED", "stage": "S5", "location": "drivers/remoteproc/sunxi_rproc.c:139-147", "title": "Unsynchronized read-modify-write and bit mismatch on shared PRCM register 0x07010364 (remap)"}, {"id": "F5", "severity": "Medium", "confidence": "CONFIRMED", "stage": "S4", "location": "drivers/remoteproc/sunxi_rproc.c:481-530", "title": "sunxi_rproc_parse_memory_regions() maps and registers DMA pool memory-region[0] as a static carveout and duplicates da_to_va translation"}, {"id": "F6", "severity": "Medium", "confidence": "CONFIRMED", "stage": "S6", "location": "Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-a523-rproc.yaml:79-113", "title": "Devicetree binding schemas lack dependencies for interrupt-names and memory-region-names and mismatch driver resource handling"}, {"id": "F7", "severity": "Medium", "confidence": "CONFIRMED", "stage": "S1", "location": "drivers/remoteproc/sunxi_rproc.c:57-63", "title": "Commit message and Cover Letter claims contradict the actual code"}, {"id": "F8", "severity": "Low", "confidence": "CONFIRMED", "stage": "S1", "location": "drivers/remoteproc/sunxi_rproc.c:22", "title": "Unused headers <linux/of_address.h> and <linux/bitfield.h>"}]}
```
