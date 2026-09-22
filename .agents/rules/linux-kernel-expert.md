# Linux Kernel Expert Coding Standard

This project targets **mainline Linux kernel upstream submission**.
We are kernel developers. The bar is correctness, not just functionality.
Code that "works on my hardware" is not acceptable — code must be provably correct under all kernel-documented contracts.

---

## 1. Memory and Pointer Safety — Zero Tolerance

- **No integer overflow in address arithmetic.** Any `da + len`, `base + offset`, or `phys + size` expression on a `u64` or `size_t` MUST be overflow-checked before use.
  ```c
  /* Correct */
  if (da > U64_MAX - len)
      return NULL;
  if (da >= region_phys && (da + len) <= (region_phys + region_size))
  ```
- **No passing stack-local variables to async kernel APIs.** If a function returns before the kernel has consumed a pointer argument (e.g. `mbox_send_message()` with `tx_block=false`, DMA descriptors, timer callbacks), the pointed-to data MUST be in stable storage (struct member, static, or heap).
- **No ERR_PTR dereference.** Every pointer returned from a kernel `_get`, `_request`, or `_alloc` API that can return `ERR_PTR` MUST be checked with `IS_ERR()` before any use, including NULL comparisons.
  ```c
  /* Wrong */
  if (priv->rx_chan)
      mbox_free_channel(priv->rx_chan);
  /* Correct */
  if (priv->rx_chan && !IS_ERR(priv->rx_chan))
      mbox_free_channel(priv->rx_chan);
  ```

---

## 2. Kernel Object Lifecycle — Strict Ordering

- **`INIT_WORK` / `INIT_LIST_HEAD` / `mutex_init` before any code path that could reach the corresponding `cancel_work_sync` / `list_del` / `mutex_destroy`.** Error paths in `probe()` that call cleanup helpers MUST NOT run before initialization.
- **Teardown order in `remove()` is the exact reverse of `probe()`.** Specifically for remoteproc/mailbox drivers:
  1. `rproc_del()` — stops the remote core and all VirtIO traffic
  2. `cancel_work_sync()` — drains workqueue after IRQ source is dead
  3. Free channels, unmap memory, release clocks/resets
- **Never `cancel_work_sync()` before stopping the hardware interrupt source.** A live interrupt re-queuing work between `cancel_work_sync()` return and `kfree()` is a use-after-free.

---

## 3. Hardware Register Access — Bus Safety

- **Never write to a hardware register block while its AXI/AHB bus is held in reset.** Reset must be deasserted before MMIO access. This causes synchronous external aborts on ARM64.
- **Deassert reset → write boot vector → release execution** (in that exact order for remoteproc cores). Do not write `STA_ADD_REG` before `reset_control_deassert()`.
- **No simultaneous WC and WB aliases to the same physical address.** ARM64 requires consistent memory type across all PTE entries for the same PA. `ioremap_wc()` and `memremap(MEMREMAP_WB)` on the same range is undefined hardware behavior.

---

## 4. Device Tree Bindings — Schema First

- **DT bindings must pass `make dt_binding_check` with zero errors/warnings** before any driver code is written or submitted. Schema defines the ABI.
- **All `required:` properties must be validated** — use `minItems`/`maxItems`, explicit `enum:` lists, and `items:` constraints. Never leave `description:` only with no schema enforcement.
- **One `compatible` string per unique hardware block.** If A523/A527/T527 are the same die, they get one compatible string. Separate strings require documented hardware differences. No catch-all fallback strings.
- **DTS node addresses must be in ascending order** within each `simple-bus` node.

---

## 5. Error Path Completeness

- **Every error path must unwind exactly the resources acquired up to the failure point** — no more, no less. Use the Linux kernel goto ladder pattern.
- **`dev_err_probe()` for all probe-time errors** that propagate to the driver core.
- **IRQ registration failures are fatal.** A `dev_warn()` on `request_irq()` failure that allows the driver to continue is wrong — an unhandled interrupt can storm the CPU.
- **Reset control must be re-asserted on all probe error paths** that previously deasserted it.

---

## 6. Concurrency and Race Conditions

- **Interrupt handlers and workqueue functions run concurrently with teardown.** Any shared state accessed from both IRQ context and process context requires a lock or explicit ordering barrier.
- **Read-modify-write on MMIO interrupt status registers has TOCTOU.** Between the read and the clear-write, new bits may have been set. Always re-check status after clearing, or use write-1-to-clear semantics.
- **`spin_lock_irqsave()` for any shared state touched from IRQ context.** `spin_lock()` alone is insufficient on SMP when the same lock is acquired in an IRQ handler.

---

## 7. Upstream Submission Standards

- **`checkpatch.pl --strict` must report 0 errors and 0 warnings** before any patch is committed or sent.
- **`make dt_binding_check` must report 0 errors** for all added or modified binding YAML files.
- **All CC lists must include every relevant maintainer and mailing list** for every patch in the series.
- **Commit messages must explain the WHY**, not just the what. Reference hardware manuals, TRM sections, or kernel documentation when behavior is non-obvious.
- **KUnit tests must accompany new logic** that can be exercised in pure-logic form (address translation, routing tables, register offset macros). Tests are not optional extras.
- **The workflow is: edit source in `linux-cubie` → `git rebase -i --autosquash` → `git format-patch` → update `patches-upstream-rfc/` and `project-cubie-a5e/patches/linux/`.** Patch files are always OUTPUT from git — never hand-edited as text files.

---

## 8. The Standard

> **"Does this code have any path that a kernel reviewer with a week to read it could flag as a bug?"**
>
> If the answer is yes, fix it first.
>
> We are not writing firmware that runs once on our board. We are writing code that runs on millions of devices maintained by people who were not in the room when it was written. It must be correct — not just today, but provably correct under all documented kernel contracts, across all error paths, under preemption, across CPU topologies, and during module unload under load.

---

## 9. Kernel Base and Toolchain Requirements

- **Patches must be based on `linux-next` or the latest `-rc1`**, not a release tag. Rob Herring's `dt_binding_check` CI runs against `linux-next`. Patches based on older trees will fail his bot even if they pass locally.
- **If your patch series introduces a new clock/reset header** (e.g. a new CCU driver), the DT binding example DTS must either:
  - Use raw hex constants instead of symbolic macros, OR
  - Note the cross-series dependency in the patch cover letter: `"Depends on: [PATCH 3/7] clk: sunxi-ng: add A523 CCU"`
- **`make dt_binding_check` is mandatory** — not `yaml.safe_load()`. These are completely different:
  - `yaml.safe_load()` = YAML syntax only
  - `make dt_binding_check` = schema validation + full DTC compilation of the embedded example including macro expansion
- **Keep `dtschema` up to date**: `pip3 install dtschema --upgrade` before every submission cycle.
- **DT_SCHEMA_FILES shortcut for development**, but must be unset for final check:
  ```bash
  # Fast during development:
  make dt_binding_check DT_SCHEMA_FILES=Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
  # Full check before submission (must pass):
  make dt_binding_check
  ```

---

## 10. DT Binding YAML Style (Krzysztof/Rob Policy)

These are upstream DT binding maintainer style requirements — no automated tool enforces them:

- **`minItems` is redundant when `min == max`** — use only `maxItems` when the count is fixed.
- **`description: |` block literal** — only use `|` when you need to preserve newlines in multi-line descriptions. Short one-liners use plain strings without `|`.
- **Drop descriptions that just restate the property name** — `clocks: description: bus clock` adds nothing. Drop it.
- **`items:` lists use `- const:` per entry**, not inline `enum:` arrays under `items:`.
- **`firmware-name`**: Use `maxItems: 1` pattern from existing drivers, not `$ref: /schemas/types.yaml#/definitions/string`.
- **`memory-region` / `memory-region-names`**: Enumerate allowed values with `items:` sub-entries, not freeform `description:`.
- **Never redeclare `status: true`** — it is inherited from the base schema.
- **Commit subjects for DT bindings**: `dt-bindings: subsystem: add foo bar` — do NOT add the word "binding" at the end; the `dt-bindings:` prefix already states that.
- **CC entries from `get_maintainer.pl`** go BELOW the `---` separator in the commit message, never in the body above `Signed-off-by:`.
