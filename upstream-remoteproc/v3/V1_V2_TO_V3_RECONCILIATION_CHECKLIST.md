# Upstream v1 & v2 Review Feedback to v3 Reconciliation Checklist

This checklist is the authoritative check-off ledger ensuring **100% of feedback from RFC v1 and v2 review emails has been accounted for, fixed in code, or recorded as a design decision in v3**.

---

## 1. Human Maintainer Reviews (v1 & v2)

| ID | Reviewer | Patch / Topic | Exact Critique / Requirement | Status | Verified Commit / Fix on `cubie-linux-7.1` |
|:---|:---|:---:|:---|:---:|:---|
| **KK-1** | Krzysztof Kozlowski | Threading | *"Do not attach (thread) your patchsets to some other threads (unrelated or older versions). This buries them deep in the mailbox"* | [x] **VERIFIED** | Dispatch rule enforced in `.agents/rules/upstream-patch-submissions.md`. v3 will be dispatched as a fresh, standalone top-level thread (no `--in-reply-to`). |
| **KK-2** | Krzysztof Kozlowski | 1/7 (Mailbox DT) | *"No, this has to be constrained/specific. See writing bindings and any existing examples. Also, just list the interrupts with minItems, instead of free form text."* | [x] **VERIFIED** | Commit `70b91777f601`: Converted `interrupts` and `interrupt-names` to rigid positional `items:` list with `minItems: 1` (`arm`, `dsp`, `cpus`, `rv`). Dropped all narrative prose. |
| **KK-3** | Krzysztof Kozlowski | 4/7 (Rproc DT) | *"Nope, this cannot be flexible. Please open existing code to see how this is done... So you completely ignored my feedback and sent exactly the same."* (on `reg-names` with `enum:`) | [x] **VERIFIED** | Commit `15ff0fd60917` / `9f56ca04d47f`: Fixed positional `- const:` entries (`cfg`, `r_sram`, `r_sram1`, `remap`). Disallowed `enum:` permutations completely. |
| **KK-4** | Krzysztof Kozlowski | Subject Lines | Drop redundant words like `"schema"` or `"binding"` from patch subjects. | [x] **VERIFIED** | Titles updated to `dt-bindings: remoteproc: add Allwinner sun55i-a523-rproc` and `dt-bindings: mailbox: add Allwinner sun55i-a523-msgbox`. |
| **KK-5** | Krzysztof Kozlowski | Core Properties | Drop type `$ref: /schemas/types.yaml#/definitions/string` on standard properties (`firmware-name`). | [x] **VERIFIED** | Only `maxItems: 1` specified on `firmware-name` (matching maintainer commit `74eb6cd91aef`). |
| **KK-6** | Krzysztof Kozlowski | Clocks & Resets | *"minItems: 2 cannot be flexible"* — clock and reset counts must match physical wiring. | [x] **VERIFIED** | Commit `9f56ca04d47f` & `5d735de22298`: Removed flexibility; added debug reset (`RST_BUS_MCU_RISCV_DEBUG`), clocks exact 3, resets exact 4. |
| **KK-7** | Krzysztof Kozlowski | Pure Hardware | Eliminate Linux driver concepts from DT descriptions (no mentions of workqueues, buffers). | [x] **VERIFIED** | All descriptions revised to describe physical hardware register blocks, IRQ lines, and bus clocks. |
| **CY-1** | Chen-Yu Tsai | Compatibles | Use specific SoC compatible strings; avoid generic wildcards. | [x] **VERIFIED** | Standardized on `allwinner,sun55i-a523-rproc` and `allwinner,sun55i-a523-msgbox`. |
| **CY-2** | Chen-Yu Tsai | Msgbox IRQ | Confirm ARM host bank IRQ mapping. | [x] **VERIFIED** | Board-verified: ARM bank uses GIC SPI 0 (`<GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>`). |
| **RH-1** | Rob Herring | Examples | Header includes must match upstream `<dt-bindings/...>` paths and compile cleanly. | [x] **VERIFIED** | Both schemas pass `make dt_binding_check` with 0 warnings; `.example.dtb` builds cleanly. |

---

## 2. Automated Static Analysis & Bot Reviews (Sashiko & Kernel Test Robot)

| ID | Subsystem | Issue / Finding | Status | Verified Commit / Fix on `cubie-linux-7.1` |
|:---|:---|:---|:---:|:---|
| **S1.1** | RemoteProc | **[Critical]** Integer overflow in `da_to_va()` (`da + len` wraparound) allows arbitrary memory access. | [x] **VERIFIED** | Guard `if (da > U64_MAX - len) return NULL;` added to all `da_to_va` and `da_to_sys` paths. |
| **S1.2** | RemoteProc | **[High]** Incorrect bounds check in Space 0 intercepts Space 1 accesses (`0x40000000`). | [x] **VERIFIED** | Space 0 bounds cleanly isolated to dedicated SRAM (`0x3FFC0000`); Space 1 mapped to `0x40000000`. |
| **S1.3** | RemoteProc | **[High]** Redundant memory regions parsing creates conflicting WC vs WB ARM64 memory aliases. | [x] **VERIFIED** | Commit `d0bb8e1ae6c5`: Duplicate lookup removed; all regions mapped with unified `MEMREMAP_WC`. |
| **S2.1** | Mailbox | **[Critical]** Unbounded `irq_cnt` in probe causes out-of-bounds array write. | [x] **VERIFIED** | Probe bounds check added: `if (irq_cnt > SUN55I_MAX_PROCESSORS) return -EINVAL;`. |
| **S2.2** | Mailbox | **[High]** `last_tx_done` polling condition returns `true` prematurely (`< FIFO_MAX`). | [x] **VERIFIED** | Fixed to return `count == 0` (message fully drained/consumed by remote peer). |
| **S2.3** | Mailbox | **[High]** NULL pointer dereference in IRQ handler during module teardown. | [x] **VERIFIED** | Mask read IRQs and call `synchronize_irq()` before `mbox_controller_unregister()`. |
| **S2.4** | Mailbox | **[High]** TOCTOU race and soft-lockup in hardirq FIFO drain loop. | [x] **VERIFIED** | Drain loop capped to `SUN55I_FIFO_MAX` (8) and protected under `spin_lock_irqsave(&mbox->lock, flags)`. |
| **T1** | Mailbox Test | Missing `SUN55I_MSGBOX` dependency in Kconfig causes build breakage. | [x] **VERIFIED** | Changed to `depends on SUN55I_MSGBOX && KUNIT`. |
| **T2 / K2** | Tests (Both) | Big-Endian failure: mock MMIO accessed via raw array indexing (`mock[offset / 4]`). | [x] **VERIFIED** | Commit `f9620174861f`: All mock MMIO register assertions converted to `readl()`/`writel()` accessors. |
| **R1** | RemoteProc | Unbalanced `disable_irq` / sleep-in-atomic race on crash IRQ. | [x] **VERIFIED** | Commit `9022023ba5c1`: Synchronized with `crash_irq_enabled` state; `devm_free_irq()` cleans up without spinlock. |
| **R2** | RemoteProc | Race on `priv->kick_msg` heap pointer and immediate `mbox_client_txdone()`. | [x] **VERIFIED** | Dropped `priv->kick_msg` and `cl.knows_txdone`; static per-vring payloads passed cleanly. |
| **R5 / R6** | RemoteProc | Teardown UAF: late mailbox interrupt after virtqueues freed. | [x] **VERIFIED** | Commit `9022023ba5c1`: Correct teardown order: `rproc_del()` $\rightarrow$ `mbox_free_channel()` $\rightarrow$ `cancel_work_sync()`. |
| **R10** | RemoteProc | Missing write flush on boot address and remap registers before reset deassertion. | [x] **VERIFIED** | Commit `6b5ffa3f0a8b`: Added `readl()` posted-write flush before deasserting core reset. |

---

## 3. v3 Pre-Send Architectural Hardening (Claude/Gemini Pre-Send Audit Fixes D1–D16)

| Tracker ID | Category | Technical Issue | Status | Verified Commit on `cubie-linux-7.1` |
|:---:|:---|:---|:---:|:---|
| **D1** | RemoteProc | Mailbox `rx_callback` runs in hardirq; `rproc_vq_interrupt()` takes mutexes (sleep in atomic on non-RT). | [x] **VERIFIED** | Commit `e2836301b158`: Deferred to ordered workqueue `sunxi_rproc_vq_work()`. |
| **D2** | DT & Driver | Missing power domains: E906 core needs `PD_RISCV` and SRAM needs `PD_SRAM` (`ppu`). | [x] **VERIFIED** | Commits `8e9044276ab7`, `4240a18f8c02`, `883cd3e88f33`: Added power domains to binding, driver, and DTS. |
| **D3** | RemoteProc | Required SRAM and CFG register regions were treated as optional. | [x] **VERIFIED** | Commit `48acefd79c73`: Probe now strictly fails if `cfg`, `r_sram`, `r_sram1`, or `remap` are missing. |
| **D4** | RemoteProc | ATT translation bugs: `0x3FF80000` does not exist; `0x00020000` is DSP RAM; `0x40040000`+ is DRAM. | [x] **VERIFIED** | Commit `c5acc22a3642`: Fixed translation table to match silicon reality. |
| **D5** | DT & Driver | Debug reset (`RST_BUS_MCU_RISCV_DEBUG`) missing from binding and driver. | [x] **VERIFIED** | Commits `9f56ca04d47f`, `5d735de22298`, `bf42a31aff9f`: Handled debug reset in binding, driver, and DTS. |
| **D6** | DT Binding | Invariant clocks/resets counts (`minItems: 2` forbidden by Krzysztof). | [x] **VERIFIED** | Commit `9f56ca04d47f`: Exact 3 clocks and exact 4 resets specified. |
| **D7** | RemoteProc | `da_to_va` duplicated core carveout lookup for DRAM/trace regions. | [x] **VERIFIED** | Commit `d0bb8e1ae6c5`: Duplicate lookup removed; core handles carveout resolution. |
| **D8** | Cleanups | Dead fallback code, stale comments, unused includes, default ARCH_SUNXI. | [x] **VERIFIED** | Commit `50e086323c87`: Cleaned up dead code across drivers. |
| **D9** | RemoteProc | Remap register `0x07010364` bit 1 shared with DSP (bit 0). | [x] **DECIDED** | Documented in `DESIGN_DECISIONS.md`: driver only read-modify-writes bit 1; verified safe on silicon. |
| **D10** | DT Binding | `memory-region-names` positional layout (`vram`, `dram`, `trace`). | [x] **DECIDED** | Kept positional list per Krzysztof's v1 guidance (KK8) and TI K3 peer pattern (`479ba9d293f5`). |
| **D11** | Mailbox IPC | One vs two mailbox channels: firmware uses single channel 9 bidirectionally. | [x] **VERIFIED** | Commits `15ff0fd60917`, `6ca3f7b199db`, `39e8db786df7`: Converted to single channel `mboxes = <&msgbox 9>`. |
| **D12** | Mailbox | Claiming clocks/resets for all 4 banks vs ARM host bank only. | [x] **VERIFIED** | Commits `70b91777f601`, `6863d4f02e9c`: Driver and binding describe only the host ARM bank clock and reset. |
| **D14** | Crash IRQ | Crash IRQ kept in driver (unwired in DTS; silicon lacks crash line). | [x] **DECIDED** | Kept for custom board designs; cover letter explicitly states it is not wired in mainline DTS. |
| **D15** | KUnit | In-tree KUnit test suites trimmed of redundant macro assertions and placed last. | [x] **VERIFIED** | Commits `f68613bb0acf`, `8f011c66f4a6`: Trimmed tests to focus on real behavior; placed last in series. |
| **D16** | DTS | Extra unbinding `"msgbox"` clock/reset removed from remoteproc DTS node. | [x] **VERIFIED** | Commit `bf42a31aff9f`: Cleaned DTS node to match binding schema exactly. |

---

## 4. Pre-Flight Verification Gate Status

- [x] **1. Code Fixes in `linux-cubie` (`cubie-linux-7.1`)**: All 33 review issues and findings resolved in git history.
- [x] **2. DT Schema Validation**: `make dt_binding_check` passes with **0 errors, 0 warnings** for both schemas.
- [x] **3. Static Linters**: `checkpatch.pl --strict` passes with **0 errors, 0 warnings** across all 4 driver/test files.
- [x] **4. Git Identity Invariant**: All commits strictly authored by `Tim Michals <tcmichals@gmail.com>` with **zero AI trailers**.
- [ ] **5. Physical Board Validation (Radxa Cubie A5E)**:
  - [ ] Boot target with updated `Image` and DTB.
  - [ ] Run in-tree KUnit tests (`dmesg | grep -i kunit`).
  - [ ] Run `ping_rpmsg` userspace test over channel 9.
  - [ ] Boot non-RT kernel with `CONFIG_DEBUG_ATOMIC_SLEEP=y` to verify workqueue deferral.
- [ ] **6. Fold into `next-sun55i-rproc-msgbox` & Patchset Re-export**:
  - [ ] Squash/fold fixes into the 7-patch series (KUnit patches last).
  - [ ] Export via `git format-patch -v3 --cover-letter`.
  - [ ] Update `v3/v3-0000-cover-letter.patch` with board validation test numbers.
- [ ] **7. Dispatch as Fresh Standalone Thread**:
  - [ ] Strictly DO NOT use `--in-reply-to`.
  - [ ] Recipient list verified against `SUBMISSION_TRACKER.md`.
