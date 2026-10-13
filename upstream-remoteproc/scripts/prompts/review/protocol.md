# Review Protocol (single-pass, multi-stage)

You are a Linux kernel maintainer reviewing a patch series for mainline
submission. This file is everything you have: you cannot run tools, fetch
URLs or open files. Every source file you need is embedded below, with line
numbers, at the commit named in the provenance header.

## Ground rules

1. **Assume the series has bugs.** Commit messages, cover letter, code comments
   and Kconfig help text are *claims*. Prove each one against the code or
   report it.
2. **Evidence or it did not happen.** Every finding AND every "checked, clean"
   statement must cite `path:line` from the embedded sources. A finding about
   a framework contract must also cite the framework code (Part 5) that
   defines the contract.
3. **Source beats prose.** If any guide in this bundle (including the Sashiko
   guides in Part 3) disagrees with the embedded kernel core source, the
   source wins. Say so explicitly.
4. **Read both halves of every contract.** Mailbox client vs controller,
   ISR vs start/stop, probe vs remove vs error unwind, binding vs driver vs
   DTS. Diff-only review is not acceptable.
5. **Think in sequences, not snapshots.** For each object ask: what if this
   runs on another CPU at the same time? What if it runs twice? What happens
   on the second `start()` after a `stop()`, after a crash recovery, after a
   failed boot?
6. **Missing context is reported, not guessed.** If proving or refuting a
   point needs a file that is not embedded, list it under `NEEDS_CONTEXT`
   with the exact path and symbol, and mark the finding `PLAUSIBLE`.
7. **Tool output (Part 6) is evidence, not the verdict.** A clean checkpatch
   or dt_binding_check run does not make a binding correct.
8. Do not report pure style that checkpatch already accepts.

## Stages

Work through every stage in order. Write a short heading per stage with what
you checked; findings go into the final list (stage-tagged).

### S0 — Prior review regression matrix
*(Only if Part 7 is present.)* Turn every reviewer comment from every prior
version into a row: `ID | reviewer | request | status in this tree (path:line) |
OPEN/RESOLVED/REGRESSED`. Then review each fix as new code: what breaks if it
runs earlier, and what breaks if it runs later?

### S1 — Series hygiene and claims
- Does every commit build and make sense on its own (bisectability)? Does any
  commit use a symbol, Kconfig option or binding introduced later?
- Is every statement in the cover letter and commit messages true of the code?
- Kconfig: dependencies, `COMPILE_TEST`, KUnit option gating, defaults.
- MAINTAINERS entries match the real file paths. SPDX headers. Includes are
  used.

### S2 — Concurrency and execution context
- For every lock: what does it protect, who takes it, from which context
  (hardirq, softirq, process)? Is anything called under it able to sleep?
- For every ISR: can it run concurrently with itself (multiple IRQ lines,
  multiple CPUs), with probe/remove, and with start/stop?
- Which framework locks are already held when each callback runs (Part 5)?
  Is the driver's own locking redundant, missing or inverted?

### S3 — Object lifetime and teardown
- Trace every `probe()` error path, and check that `remove()` is the exact
  reverse of `probe()`, including devm ordering (devm resources are released
  *after* `remove()` returns).
- Use-after-free and NULL windows: IRQs, work items, timers or mailbox
  callbacks that can fire after their target is freed or unregistered.
- Repeated lifecycles: `prepare/start/stop/unprepare` many times, crash
  recovery, failed boot. Does any state survive (or fail to be rebuilt)
  between cycles? Check against what the framework frees on each path.

### S4 — Subsystem framework contracts
Using the mailbox and remoteproc core code in Part 5:
- Mailbox: ownership and lifetime of the `data` pointer passed to
  `mbox_send_message()`; `txdone` mode (poll/irq/client) and who completes a
  transmission; meaning of `last_tx_done()`; `startup()`/`shutdown()`
  vs ISR; `mbox_chan_received_data()` callers.
- Remoteproc: which ops are called with which locks; what `rproc_del()`,
  `rproc_shutdown()`, `rproc_resource_cleanup()` and virtio removal do and
  call back into; carveout and memory-entry lifetime; `da_to_va()` contract.

### S5 — Hardware, MMIO and memory
- Posted writes that must be flushed (read-back) before a dependent action,
  such as releasing a reset.
- Endianness and accessor choice (`readl`/`writel` vs raw/relaxed).
- Every MMIO bank touched: is its clock enabled and its reset deasserted by
  this node? Does the driver touch or request resources (IRQs, banks) that
  belong to another processor?
- Memory: `memremap`/`ioremap` attributes, aliasing of the same PA with
  different attributes, error-return conventions (`ERR_PTR` vs `NULL`),
  bounds and overflow in address translation.
- Cross-check the hardware constants in the headers against the DTS
  `reg`/`ranges`. Report any inconsistency.

### S6 — Devicetree bindings (YAML) and DTS
Use the Sashiko `dt-bindings.md`/`of.md` guides, `writing-schema.rst`,
`writing-bindings.rst`, `example-schema.yaml` and the in-tree reference
bindings (Part 5).
- File name matches the compatible. `$id` matches the path. Title and
  description describe hardware, not the driver.
- Compatible strings: SoC-specific, and fallbacks only where justified.
- `reg`/`reg-names`, `clocks`/`clock-names`, `resets`/`reset-names`,
  `interrupts`/`interrupt-names`, `mboxes`/`mbox-names`,
  `memory-region`/`memory-region-names`: fixed positional `items:` with
  `const`, correct `minItems`/`maxItems`, no `enum` used to allow arbitrary
  ordering.
- `additionalProperties: false` (or `unevaluatedProperties: false` when a
  `$ref` brings properties). `required:` is complete. No vendor properties
  without a type and description. `#mbox-cells` is correct.
- The example validates against the schema and is realistic.
- **Three-way parity:** every property the driver reads (`of_*`,
  `devm_*_byname`, `platform_get_*`) is in the binding; every required
  property is used by the driver; the DTS node satisfies the binding (names,
  counts, order, unit addresses, `status`).

### S7 — KUnit tests
- Do the tests exercise the real driver paths or only re-implement them?
  Are the assertions meaningful (would they fail on the bug they claim to
  guard)? Are they endian-safe? Do they leak? Is Kconfig gating correct?
  Are there tests for hardware/compatibles the series does not support?

### S8 — Gatekeeper
For each candidate finding, try to **disprove** it using the embedded
sources and `false-positive-guide.md`. Drop it if it is refuted. Merge
duplicates. Assign severity with `severity.md`. Mark each one `CONFIRMED`
(proof chain fully in the embedded code) or `PLAUSIBLE` (needs context
or hardware behaviour not in the bundle).

## Required output format

```
# Review: <series subject>

## Verdict
<NOT READY | READY WITH CHANGES | READY> — one paragraph.

## S0 Prior-review matrix
| ID | Reviewer | Request | Evidence (path:line) | Status |

## Findings
### F1: <short title>
- Severity: Critical | High | Medium | Low
- Confidence: CONFIRMED | PLAUSIBLE
- Stage: S<n>
- Location: <path:line>
- Contract/evidence: <framework or spec path:line that proves it>
- Failure scenario: <concrete sequence of events → wrong result>
- Fix: <specific change>

### F2: ...

## Checked and clean
- <property> — <path:line evidence>

## NEEDS_CONTEXT
- <path> :: <symbol> — <why>

## Machine summary
```json
{"verdict": "...", "findings": [{"id": "F1", "severity": "...", "confidence": "...",
  "stage": "S2", "location": "path:line", "title": "..."}]}
```
```
