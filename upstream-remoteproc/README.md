# Allwinner sun55i RemoteProc & Mailbox Upstream Submission Workspace

This workspace consolidates all upstream Linux kernel submissions, Devicetree binding reviews, revision diffs, automated review scrapers, reviewer tracking, and lessons learned for the **Allwinner sun55i (A523, A527, T527) and sun60i (A733) RemoteProc and Message Box drivers**.

---

## Quick Navigation

* 📊 **[Upstream Submission Tracker (SUBMISSION_TRACKER.md)](SUBMISSION_TRACKER.md)**  
  Version status (v1, v2, v3), Lore mailing list thread URLs, and dispatch instructions.
* 🧠 **[Upstream Lessons Learned & Rules Ledger (UPSTREAM_LESSONS_LEARNED.md)](UPSTREAM_LESSONS_LEARNED.md)**  
  Detailed analysis of reviewer feedback (Krzysztof Kozlowski, Rob Herring, Jassi Brar, Bjorn Andersson), anti-patterns identified, and the mandatory Linux kernel rules learned.
* 🛡️ **[v3 Verification & Audit Report (v3/AUDIT.md)](v3/AUDIT.md)**  
  Clean 5-tier adversarial audit results and complete 23-item verification matrix.
* 📬 **[Archived Review Feedback](v2/COMMENTS.md)**  
  Verbatim mailing list reviews in [v1/COMMENTS.md](v1/COMMENTS.md) and [v2/COMMENTS.md](v2/COMMENTS.md).

---

## Directory Overview

```text
upstream-remoteproc/
├── README.md                   # Workspace overview (this file)
├── SUBMISSION_TRACKER.md       # Upstream version dashboard & Lore status
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
    └── AUDIT.md                # v3 verification matrix & adversarial audit report (Status: CLEAN)
```
