# Linux Kernel Driver Code Review Prompt

## Purpose

This is the **mandatory self-review prompt** to run against every Linux kernel driver
before `git commit`, before `git format-patch`, and before `git send-email`.

When writing or reviewing any kernel driver patch, apply **every question below** to the
code. If any answer is "I'm not sure", stop and fix it. A "works on my board" answer is
not acceptable.

---

## HOW TO USE

Paste this prompt to yourself (or an AI reviewer) with the full driver source:

> "You are a Linux kernel maintainer performing a pre-submission review.
> Apply every check in the linux-kernel-review-prompt.md rule to the following
> driver code. For each check, quote the exact line(s) of concern and explain
> the specific failure mode. Do not summarize — be exhaustive."

---

## CHECK GROUP 1: Integer Overflow in Address Arithmetic

For every expression of the form `da + len`, `base + offset`, `phys + size`:

1. Is `da` or `base` a `u64`? If yes: **can it be close to `U64_MAX`?**
2. Does the code check `da > U64_MAX - len` (or equivalent) BEFORE using `da + len` in a comparison?
3. If a crafted ELF segment sets `da = 0xFFFFFFFF00000000` and `len = 0x100000001`, does `da + len` wrap to a value that passes the upper-bound check?
4. Does every branch of `da_to_va()` (or equivalent address translation) share a single overflow guard at the top, or does each branch independently risk overflow?

**Expected answer:** A single `if (da > U64_MAX - len) return NULL;` guard appears before any `da + len` comparison. Zero branches bypass it.

---

## CHECK GROUP 2: Stack Use-After-Free via Async Kernel APIs

For every call to `mbox_send_message()`, `dma_async_memcpy_pg_offload()`, `call_rcu()`, timer callbacks, or any API documented as non-blocking or deferred:

1. What is the type and storage class of the `data` pointer argument?
2. Is the pointer a local stack variable (function parameter or `auto` variable)?
3. Does the calling function return before the kernel framework has consumed the pointed-to data?
4. Is `tx_block = false` set anywhere in the mailbox client config? If yes, `mbox_send_message()` **will** return before the hardware has read the message.

**Expected answer:** All message data passed to async APIs lives in `struct` members, static storage, or dynamically allocated memory — never on the call stack.

---

## CHECK GROUP 3: ERR_PTR Passed as Non-NULL to Free Functions

For every error path in `probe()` that goes to a cleanup label:

1. Which pointers were set by `mbox_request_channel_byname()`, `devm_clk_get()`, `request_irq()`, or similar?
2. On the `-EPROBE_DEFER` or non-NULL error path, do any of these pointers hold `ERR_PTR(-EPROBE_DEFER)` (i.e., non-NULL but not a valid pointer)?
3. Does the cleanup code check `IS_ERR()` before calling `mbox_free_channel()`, `clk_put()`, `free_irq()`, etc.?

**Expected answer:** Every cleanup of a channel/resource pointer uses `if (ptr && !IS_ERR(ptr))`, not just `if (ptr)`.

---

## CHECK GROUP 4: Uninitialized Work Struct on Error Path

In `probe()`:

1. Where exactly is `INIT_WORK(&priv->vq_work, ...)` called?
2. Is there any `goto` label reached before `INIT_WORK` that leads to `cancel_work_sync(&priv->vq_work)`?
3. Calling `cancel_work_sync()` on an uninitialized `work_struct` triggers a kernel BUG. Is there any code path where this can happen?

**Expected answer:** `INIT_WORK()` appears **before** the first `goto` that targets any label calling `cancel_work_sync()`.

---

## CHECK GROUP 5: Hardware Register Access Under Reset (Bus Fault)

In `start()` and `recovery` paths:

1. Is there a `writel()` or `readl()` to a peripheral register block before `reset_control_deassert()` has been called for that block's bus/fabric reset?
2. On a recovery path (`stop()` followed by `start()` without `prepare()`), which resets are still asserted?
3. If `rst_cfg` is asserted (as it would be after `stop()` without `unprepare()`), and the code writes to `STA_ADD_REG` (which is inside the CFG block's address space), what happens on ARM64?

**Expected answer:** Boot vector register writes happen **after** the relevant reset is deasserted. The order is: `reset_control_deassert()` → `writel(boot_addr)`.

---

## CHECK GROUP 6: Inverted Teardown Order (UAF in remove/stop)

In `remove()`:

1. What is the exact sequence of: `rproc_del()`, `cancel_work_sync()`, `mbox_free_channel()`?
2. Can `rproc_del()` internally trigger the `stop()` callback, which asserts reset and causes the firmware to stop sending mailbox interrupts?
3. If `cancel_work_sync()` is called BEFORE `rproc_del()`, is there a window where `rproc_del()`'s internal `stop()` causes a late mailbox IRQ to re-queue `vq_work`, which then executes after `mbox_free_channel()` has freed `rx_chan`?

In `stop()`:

1. Does `cancel_work_sync()` run before or after `reset_control_assert()`?
2. If the remote core is still running when `cancel_work_sync()` runs, can it send a mailbox interrupt that re-queues the work after `cancel_work_sync()` returns?

**Expected answer:**
- `remove()`: `rproc_del()` → `cancel_work_sync()` → `mbox_free_channel()`
- `stop()`: `reset_control_assert()` (halt core) → `cancel_work_sync()` (drain work)

---

## CHECK GROUP 7: Ambiguous/Overlapping Address Region Mapping

In `da_to_va()` or any address translation function:

1. Does any DA alias appear in more than one `if` branch?
2. Specifically: if both `r_sram` (Space 0) and `r_sram1` (Space 1) are present, and both check `da >= 0x40000000`, which branch wins?
3. Does mapping `0x40000000` in the Space 0 block silently redirect what should be Space 1 accesses into the wrong SRAM window?
4. For each constant DA alias used (e.g. `0x40000000`, `0x3ffc0000`, `0x3ff80000`): is it documented which hardware space it belongs to, and does only one `if` branch handle it?

**Expected answer:** Each DA alias appears in exactly one branch. Ambiguous aliases (like `0x40000000` shared by Space 0 and Space 1) are resolved by checking which resource is present, or removed from the wrong block.

---

## CHECK GROUP 8: Redundant Mapping with Conflicting Cache Attributes

1. Does `probe()` call both a `register_mem()` function AND a `parse_memory_regions()` function?
2. Do both functions independently map the same physical memory regions from the Device Tree?
3. Is any physical address range mapped first with `ioremap_wc()` (Write-Combining) and later with `memremap(MEMREMAP_WB)` (Write-Back cacheable), or vice versa?
4. On ARM64, does having two PTEs for the same PA with different memory type attributes (WC vs. WB) violate the architecture's memory aliasing rules?
5. Does execution fall through without a `continue` or `return` between the "named region" handling and the generic `ioremap_wc()` fallback, causing a third mapping of the same PA?

**Expected answer:** Each physical address is mapped exactly once, with one consistent cache attribute, from one code path.

---

## CHECK GROUP 9: IRQ Failure Handling

1. For every `request_irq()` or `devm_request_irq()` call: what happens if it returns an error?
2. Does the code use `dev_warn()` and continue, or does it propagate the error?
3. If the IRQ handler is not registered but the hardware interrupt source is enabled and unmasked, can the CPU receive an unhandled interrupt that storms the interrupt line?

**Expected answer:** `request_irq()` failures are either fatal (propagated as an error) or the interrupt source is explicitly masked to prevent storms.

---

## CHECK GROUP 10: Reset Control Leak on Remove/Error

1. In `probe()` error paths and in `remove()`: is `reset_control_assert()` called for every reset that was previously deasserted?
2. Does `remove()` mirror every `reset_control_deassert()` from `prepare()` with a corresponding `reset_control_assert()`?
3. If the driver is unbound and rebound, does the hardware start from a clean reset state?

**Expected answer:** Every deasserted reset is re-asserted on the corresponding error unwind or in `remove()`/`unprepare()`.

---

## CHECK GROUP 11: DT Binding Schema Completeness

1. Run `make dt_binding_check` — does it pass with zero errors?
2. Do all examples in the YAML compile without errors under `dtc`?
3. Are all examples syntactically complete (matching `{` and `}`, valid property syntax)?
4. Are `memory-region-names` values constrained to an `enum:` list in the schema?
5. Does the schema enforce that at least one SRAM region (`r_sram` or `r_sram1`) is required?
6. Are there `status: true` or other inherited-base-schema properties redeclared unnecessarily?
7. Are unused includes (`#include <dt-bindings/...>`) present in examples?

---

## CHECK GROUP 12: Compatible String Policy

1. Are A523, A527, and T527 (or equivalent SoC variants) confirmed to have different IP block implementations, or are they the same die?
2. If same die: is there only one `compatible` string?
3. Are there any catch-all fallback compatible strings (e.g. `allwinner,sunxi-rproc`) that would match unknown future hardware?
4. Does the `of_match` table match the binding YAML `compatible` enum exactly?

---

## CHECK GROUP 13: DTS Node Ordering and Correctness

1. Are all `simple-bus` child nodes sorted by their `reg` base address in ascending order?
2. Does the `compatible` string in the DTS match the binding schema exactly (no extra or missing strings)?
3. Are all required properties from the binding schema present in the DTS example?

---

## FINAL GATE: Before Every `git send-email`

Run this checklist and confirm ALL pass:

- [ ] `checkpatch.pl --strict` → 0 errors, 0 warnings
- [ ] `make dt_binding_check` → 0 errors
- [ ] `make dtbs_check` on affected `.dtsi` → 0 errors
- [ ] All 13 check groups above answered with "Expected answer"
- [ ] KUnit tests added for any new pure-logic code
- [ ] Cover letter references the previous version's thread
- [ ] Full CC list on every patch (no split CC across patches)
- [ ] Commit messages explain WHY, not just what

---

## CHECK GROUP 14: Kernel Base and DTC Compilation of Examples

**This is what Rob Herring's bot caught that our local check missed.**

1. Is the series based on `linux-next` or the latest `-rc1`? If based on an older tag, is that noted in the cover letter?
2. Does the DT binding example DTS use symbolic macro constants (e.g. `CLK_BUS_MCU_RISCV_CFG`) from a header introduced by another patch in the series?
3. If yes: are those constants available in `linux-next` (i.e. already merged)? If not, does the patch use raw hex values in the example, or does the cover letter state the dependency?
4. Was `make dt_binding_check` actually run (full DTC compilation), or just `yaml.safe_load()`? These are completely different. Only `make dt_binding_check` compiles the example DTS.
5. Is `dtschema` up to date? (`pip3 install dtschema --upgrade`)

**Expected answer:** `make dt_binding_check` passes with zero errors against a `linux-next`-based tree with all series patches applied in order.

---

## CHECK GROUP 15: DT Binding YAML Style (Krzk/Rob Policy)

These are not caught by automated tools — they are upstream maintainer style requirements:

1. `minItems == maxItems`? → Drop `minItems`, only use `maxItems`.
2. `description: |` on a one-liner? → Remove the `|` block literal marker.
3. Description just restates the property name (e.g. `clocks: description: bus clock`)? → Drop it.
4. `items:` lists use `- const:` per entry? (Not `enum:` arrays inline under `items:`)
5. `firmware-name` using `$ref: /schemas/types.yaml`? → Use `maxItems: 1` per existing drivers.
6. `memory-region` / `memory-region-names` using freeform `description:`? → Use `items:` with per-entry descriptions.
7. `status: true` present? → Drop it. Inherited from base schema.
8. Commit subject ends with "binding"? (e.g. `dt-bindings: remoteproc: add foo binding`) → Drop "binding" — the `dt-bindings:` prefix already says so.
9. `get_maintainer.pl` CC output in commit message body above `Signed-off-by:`? → Move it below the `---` separator.

---

## UPDATED FINAL GATE: Before Every `git send-email`

- [ ] `checkpatch.pl --strict` → 0 errors, 0 warnings
- [ ] `pip3 install dtschema --upgrade` run this submission cycle
- [ ] `make dt_binding_check` → 0 errors (full run, DT_SCHEMA_FILES unset)
- [ ] `make dtbs_check` on affected `.dtsi` → 0 errors
- [ ] DT binding examples use raw hex constants OR cross-patch dependency is documented
- [ ] Patch series based on `linux-next` or latest `-rc1` (noted in cover letter if different)
- [ ] All 15 check groups above answered with "Expected answer"
- [ ] KUnit tests added for any new pure-logic code
- [ ] Cover letter references the previous version's thread
- [ ] Full CC list on every patch (no split CC across patches)
- [ ] Commit messages explain WHY, not just what
- [ ] `get_maintainer.pl` output below `---` separator only, not in commit body
- [ ] No word "binding" at end of `dt-bindings:` commit subjects
