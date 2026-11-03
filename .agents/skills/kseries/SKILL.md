---
name: kseries
description: Reconcile upstream kernel review feedback from lore mailing list emails, enforce zero dropped comments, audit inter-version range-diffs, generate inline reviewer replies, and assemble formatted patchsets.
---

# Kernel Patch Series Lifecycle & Feedback Reconciliation Protocol (/kseries)

When this skill is activated (via `/kseries` or when preparing a new patch version `v<N>` or checking reviewer comments), execute a strict 5-stage reconciliation protocol to guarantee **zero reviewer comments are ignored or missed**, all changes are audited via `range-diff`, and the series is properly formatted for dispatch.

---

## The Golden Principle: Zero Ignored Feedback
> *"So you completely ignored my feedback and sent exactly the same."* — Krzysztof Kozlowski

Kernel maintainers volunteer significant time reviewing patches. Sending a new revision or an email reply without an explicit, verifiable resolution for **every single piece of feedback** damages developer trust and stalls patch acceptance.

Every comment must be resolved in one of two ways:
1. **`FIXED`**: Modified in code or DT binding schema, with a verifiable commit hash, file, and diff hunk.
2. **`EXPLAINED / DECIDED`**: Technically justified (e.g., citing physical datasheet, silicon readback, or `DESIGN_DECISIONS.md`) and formulated as an explicit, polite inline reply to the reviewer. For a High/Medium finding this needs the **owner's yes/no**, recorded with the date; an existing DESIGN_DECISIONS entry is a reason to ask, not to close. A precedent argument cites every peer, including those that do it differently.

> [!WARNING]
> v3 lesson (UPSTREAM_LESSONS_LEARNED §6): 4 of the 9 v3 findings had been found by our own reviews and closed as "left as is" or on one-sided precedent; 3 more were repeats of v2 comments marked fixed. Explaining a comment away is the exception.

---

## 5-Stage Lifecycle & Assembly Pipeline

### Stage 1: Inbound Review Ingestion & Atomization
1. **Scrape Public-Inbox Archives**:
   Ensure all inbound reviewer emails for the active revision are captured in `upstream-remoteproc/v<N>/emails/`:
   ```bash
   python3 upstream-remoteproc/scripts/fetch_lore_reviews.py --version v<N> --msgid <cover-or-patch-msgid>
   ```
   lore's search and Atom feeds return 403 to scripts; the thread mbox by Message-ID works. If the Message-ID is not recorded, take it from the Sashiko link (`sashiko.dev/#/patchset/<msgid>`) or ask the owner. Record it in `SUBMISSION_TRACKER.md` at send time.
2. **Atomize Every Review into Numbered Critique Tokens**:
   Do not group comments into vague summaries. Parse every `.eml` file and extract each specific critique into an atomic token:
   - `[KK-<N>]`: Krzysztof Kozlowski (Devicetree bindings, naming, schemas, threading)
   - `[RH-<N>]`: Rob Herring (Devicetree core schemas, include paths)
   - `[JB-<N>]`: Jassi Brar (Mailbox subsystem, FIFO pacing, locking)
   - `[MP-<N>]`: Mathieu Poirier (RemoteProc subsystem, VirtIO/RPMsg, teardown)
   - `[CY-<N>]`: Chen-Yu Tsai / Allwinner maintainers (SoC clocks, resets, DT)
   - `[SB-<N>]`: Sashiko bot / Kernel test robot (Static taint analysis, overflow, bounds)

---

### Stage 2: The 1-to-1 Resolution Audit Gate
Audit the extracted critique tokens against `upstream-remoteproc/v<N>/COMMENTS.md`, `TODO.md` §5.6a, and the actual commit history in `linux-cubie`.

Construct or update the reconciliation matrix:
| Critique ID | Reviewer | Patch | Issue / Comment | Resolution Status | Verified Commit / Justification |
| :--- | :--- | :---: | :--- | :---: | :--- |
| `KK-1` | Krzysztof | 1/7 | Do not attach/thread patchset to older threads | **FIXED** | Standalone top-level thread planned; no `--in-reply-to` |
| `KK-2` | Krzysztof | 1/7 | Interrupts: free-form text instead of constrained items | **FIXED** | Positional `items:`, all lines required, **no `minItems`** (`minItems: 1` was rejected again on v3) |
| `KK-3` | Krzysztof | 4/7 | `reg-names` using `enum:` allows arbitrary permutations | **FIXED** | Commit `15ff0fd60917`: positional `- const:` entries |

**Repeat check**: for every item from earlier versions marked FIXED, re-run its failure scenario on the new code, and mark items a reviewer reported again ("as reported on v2", "same comments as for v2") as **REPEAT**. A REPEAT is said plainly in the patch changelog and the reply.

> [!CAUTION]
> **GATE HALT**: If any critique item is marked `UNRESOLVED` or missing from the code/DTS/schema, **HALT IMMEDIATELY**. Do not generate patches, do not send email replies, and do not advance revision numbers until every item has a recorded commit or owner decision.

---

### Stage 3: Inter-Version Range-Diff Verification
Verify the code delta between the previous submitted revision and the new working branch:
```bash
# In linux-cubie:
git range-diff <base>..<old-series-branch> <base>..<new-series-branch>
```
1. **Verify Intended Changes**: Ensure every code change directly maps to a critique resolution or a recorded test hardening milestone.
2. **Detect Regressions / Accidental Churn**: Ensure no unrelated files or unintended reverts occurred during rebasing or squashing.
3. **Verify Commit Alignment**: Ensure the series structure remains coherent (e.g., DT bindings precede driver implementations; KUnit tests placed last per project decisions).

---

### Stage 4: Inline Email Reply Generator
For every reviewer email thread on `lore.kernel.org`, generate a point-by-point, polite inline reply draft:
- Quote the reviewer's exact lines using standard email markdown quotes (`> ...`).
- Trim all extraneous boilerplate or unneeded code context.
- Place the exact resolution directly beneath each quoted point.

**Example Draft**:
```text
On Thu, Oct 01, 2026 at 08:16:08AM +0200, Krzysztof Kozlowski wrote:
> Do not attach (thread) your patchsets to some other threads (unrelated
> or older versions). This buries them deep in the mailbox and might
> interfere with applying entire sets.

Understood, apologies for the threading mistake in v2. v3 will be
dispatched as a fresh, standalone top-level thread with its own cover letter.

> No, this has to be constrained/specific. See writing bindings and any
> existing examples. Also, just list the interrupts with minItems, instead
> of free form text.

Fixed in v3. Dropped the free-form text and converted 'interrupts' and
'interrupt-names' into a positional list; all four lines exist on every
A523, so all are required (no minItems):
  items:
    - const: arm
    - const: dsp
    - const: cpus
    - const: rv
```

---

### Stage 5: Patchset Assembly & Pre-Submission Linting
When all code fixes and test milestones pass on hardware:
1. **Per-Patch Changelog Placement**:
   Verify that each commit in `git log` contains a `Changes in v<N>:` section placed strictly **below the `---` cut line** (so it appears in the email but is not committed to git history):
   ```text
   ---
   Changes in v3:
     - Converted reg-names to positional list (Krzysztof Kozlowski)
     - Added debug reset to match CCU binding
   ---
   ```
2. **Cover Letter Assembly**:
   Ensure `v<N>/COVER_LETTER_DRAFT.txt`:
   - Summarizes the high-level delta between revisions.
   - Explicitly cites physical hardware validation results and test counts.
   - Acknowledges maintainers and reviewers by name for their feedback.
3. **Patch Export**:
   Export the patch set from `linux-cubie`:
   ```bash
   git format-patch -v<N> --cover-letter -o ../cubie-a5e/upstream-remoteproc/v<N>/ <base>..<series-branch>
   ```
4. **Pre-Submission Invariant Audit**:
   - Run `scripts/checkpatch.pl --strict v<N>/*.patch` (must pass 0 errors, 0 warnings).
   - Build checks: `W=1` (GCC and clang, `=y` and `=m`), `C=1` sparse, a `CONFIG_PM=n` build, `dt_binding_check`, `dtbs_check` (sparse and PM=n were missed before v3).
   - Run `/kreview` on the final series, including its concurrency pass; all owner questions answered.
   - Every box in `v<N>/PRESEND_CHECKLIST.md` ticked with evidence.
   - Verify `git log` metadata: strictly Author `Tim Michals <tcmichals@gmail.com>`.
   - **STRICT PROHIBITION**: Zero `Co-Authored-By` AI trailers or bot metadata.
   - Recipient Verification: Verify `git send-email` recipient list matches maintainer list from `SUBMISSION_TRACKER.md` (no generic keyword spam).
   - Threading Verification: Confirm `--in-reply-to` is NOT set.
