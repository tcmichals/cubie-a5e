# Allwinner sun55i RemoteProc & Mailbox Upstream Submission Tracker

This dashboard tracks all upstream Linux kernel submissions, mailing list threads, Devicetree reviews, reviewer feedback, and verification statuses for the Allwinner sun55i (A523, A527, T527) and sun60i (A733) RemoteProc and Message Box drivers.

---

## 1. Upstream Patch Series Status

| Version | Submission Date | Thread URL / Message ID | Status & Key Changes |
| :---: | :---: | :--- | :--- |
| **RFC v1** | 2026-09-22 | [`20260922034711.190253-1-tcmichals@gmail.com`](https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/) | **Superseded**<br>Initial RFC submission (7 patches). Reviewed by Krzysztof Kozlowski, Rob Herring, and Chen-Yu Tsai. |
| **v2** | 2026-09-26 | [`20260927002021.797069-1-tcmichals@gmail.com`](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/) | **Reviewed**<br>Added 68 in-tree KUnit tests. Flagged for: `last_tx_done` pacing (`M2`), big-endian mock MMIO (`K2`), DT `reg-names` enum (`D1`), and unconstrained interrupts (`D2`). |
| **v3** | *Not sent* | **Standalone top-level thread**<br>*(no `--in-reply-to`, per Krzysztof Kozlowski)* | **Fixes done on `cubie-linux-7.1`, board test pending**<br>• First v3 draft reviewed (v3/review/CLAUDE_REVIEW.md, "not ready"); fixes D1–D16 in [TODO.md](../TODO.md) §5.6a.<br>• Decisions in [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md): one mailbox channel, listed memory regions, shared remap bit 1, host msgbox bank only, KUnit last.<br>• Next: board test → fold into `next-sun55i-rproc-msgbox` → re-export → final review → send (owner approval). |

---

## 2. Key Documentation & Reference Links

* **[Gemini Pro 2M Context Audit Bundle](GEMINI_PRO_AUDIT_BUNDLE.md)**:  
  Self-contained 2M-token audit bundle containing complete driver code, DT bindings, KUnit suites, and adversarial review prompt.
* **[Upstream Lessons Learned & Rules Ledger](UPSTREAM_LESSONS_LEARNED.md)**:  
  Detailed breakdown of why specific patterns were rejected by maintainers (Krzysztof Kozlowski, Rob Herring, Jassi Brar, Bjorn Andersson) and the exact code patterns required.
* **[Design Decisions](DESIGN_DECISIONS.md)**:  
  The owner's decisions for the drivers and bindings. Check before acting on a review comment.
* **[v3 Pre-Send Review](v3/review/CLAUDE_REVIEW.md)**:  
  Review of the first v3 draft (findings F1-F12); the fixes are tracked as D1-D16 in [TODO.md](../TODO.md) §5.6a.
* **[v2 Review Comments](v2/COMMENTS.md)** & **[v2 Audit Report](v2/AUDIT.md)**:  
  Archived verbatim emails and flagged issues from v2 review cycle.
* **[v1 Review Comments](v1/COMMENTS.md)**:  
  Archived verbatim emails from RFC v1.
* **[Lore Email Ingest Pipeline Guide (EMAIL_INGEST_GUIDE.md)](EMAIL_INGEST_GUIDE.md)**:  
  Architecture, scripts, and workflow for pulling Lore email archives and generating COMMENTS.md.

---

## 3. Directory Layout

```text
upstream-remoteproc/
├── README.md                   # Directory landing page & overview
├── SUBMISSION_TRACKER.md       # Upstream version dashboard & Lore status (this file)
├── GEMINI_PRO_AUDIT_BUNDLE.md  # 2M-token self-contained code & audit prompt bundle
├── UPSTREAM_LESSONS_LEARNED.md # Engineering rules & anti-pattern learning ledger
├── scripts/                    # Review fetching and adversarial audit tools
│   ├── fetch_lore_reviews.py   # Scrapes Lore threads and populates v<N>/emails/
│   ├── run_adversarial_audit.py # Multi-stage 5-tier kernel static audit harness
│   └── sashiko_protocols.md    # Multi-stage review specifications
├── v1/                         # RFC v1 patch series (Sep 22, 2026)
│   ├── 0000-cover-letter.patch .. 0005-*.patch
│   ├── emails/                 # 11 raw review .eml files from Lore
│   └── COMMENTS.md             # Verbatim parsed feedback from reviewers
├── v2/                         # v2 patch series (Sep 26, 2026)
│   ├── v2-0000-cover-letter.patch .. v2-0007-*.patch
│   ├── v1_to_v2_drivers_and_bindings.diff
│   ├── emails/                 # 8 raw review .eml files from Lore
│   ├── COMMENTS.md             # Verbatim parsed feedback from Krzysztof & Sashiko
│   └── AUDIT.md                # v2 audit report (flagged M2, K2)
└── v3/                         # v3 patch series workspace
    ├── v3-*.patch              # exported series (re-exported after the fixes are folded in)
    ├── COVER_LETTER_DRAFT.txt  # cover letter draft until export
    └── review/                 # pre-send reviews and the scoring answer key
```

---

## 4. Upstream Submission Workflow (Submitting v3)

Every step happens in `linux-cubie` (`/home/tcmichals/projects/cubie/linux-cubie`):

1. **Board test**: the fixes on `cubie-linux-7.1` pass the full test sweep on the Cubie A5E, plus one non-RT boot with `CONFIG_DEBUG_ATOMIC_SLEEP=y`.
2. **Fold and export**: fold the fixes into `next-sun55i-rproc-msgbox` (KUnit patches last), delete the old `v3/v3-*.patch`, then:
   ```bash
   git format-patch -v3 --cover-letter -o ../cubie-a5e/upstream-remoteproc/v3/ 5c4d4169604b..next-sun55i-rproc-msgbox
   ```
   (`5c4d4169604b` = linux-next 20260921, the series base.)
3. **Cover letter**: paste `v3/COVER_LETTER_DRAFT.txt` into `v3-0000-cover-letter.patch` and fill in the Testing section from step 1.
4. **Dispatch as Independent Thread** (recipients = v2 list, see `TODO.md` T3; no get_maintainer keyword-only matches):
   ```bash
   # DO NOT use --in-reply-to!  Send only with the owner's go-ahead.
   git send-email \
       --to="Jassi Brar <jassisinghbrar@gmail.com>" \
       --to="Bjorn Andersson <andersson@kernel.org>" \
       --to="Mathieu Poirier <mathieu.poirier@linaro.org>" \
       --cc="Rob Herring <robh@kernel.org>" \
       --cc="Krzysztof Kozlowski <krzk+dt@kernel.org>" \
       --cc="Conor Dooley <conor+dt@kernel.org>" \
       --cc="Chen-Yu Tsai <wens@kernel.org>" \
       --cc="Jernej Skrabec <jernej.skrabec@gmail.com>" \
       --cc="Samuel Holland <samuel@sholland.org>" \
       --cc=linux-sunxi@lists.linux.dev \
       --cc=linux-remoteproc@vger.kernel.org \
       --cc=devicetree@vger.kernel.org \
       --cc=linux-arm-kernel@lists.infradead.org \
       --cc=linux-kernel@vger.kernel.org \
       v3/*.patch
   ```
