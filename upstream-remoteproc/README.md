# Allwinner sun55i RemoteProc & Mailbox Upstream Submission Workspace

This workspace consolidates all upstream Linux kernel submissions, Devicetree binding reviews, revision diffs, automated review scrapers, reviewer tracking, and lessons learned for the **Allwinner sun55i (A523, A527, T527) and sun60i (A733) RemoteProc and Message Box drivers**.

---

## Quick Navigation

* 📊 **[Upstream Submission Tracker (SUBMISSION_TRACKER.md)](SUBMISSION_TRACKER.md)**  
  Version status (v1, v2, v3), Lore mailing list thread URLs, and dispatch instructions.
* 🧠 **[Upstream Lessons Learned & Rules Ledger (UPSTREAM_LESSONS_LEARNED.md)](UPSTREAM_LESSONS_LEARNED.md)**  
  Detailed analysis of reviewer feedback (Krzysztof Kozlowski, Rob Herring, Jassi Brar, Bjorn Andersson), anti-patterns identified, and the mandatory Linux kernel rules learned.
* 🔍 **[Granular Issue Resolution Matrix (reviews/REVIEW_TRACKER.md)](reviews/REVIEW_TRACKER.md)**  
  Line-by-line issue tracking for findings M1–M4, T1–T3, R1–R10, K1–K3, and D1–D3.

---

## Directory Overview

```text
upstream-remoteproc/
├── README.md                   # Workspace overview (this file)
├── SUBMISSION_TRACKER.md       # Upstream version dashboard & Lore status
├── UPSTREAM_LESSONS_LEARNED.md # Engineering rules & anti-pattern learning ledger
├── scripts/                    # Review fetching and adversarial audit tools
│   ├── fetch_lore_reviews.py   # Scrapes Lore threads and reviewer tags
│   └── run_adversarial_audit.py # Multi-stage 5-tier kernel static audit harness
├── reviews/                    # Review prompts, issue trackers, and audit reports
│   ├── REVIEW_TRACKER.md       # Granular issue status matrix (M1-M4, R1-R10, D1-D3)
│   ├── AUDIT_V3_RESULTS.md     # Automated adversarial audit report (Status: CLEAN)
│   └── GEMINI_PRO_REMOTEPROC_MSGBOX_AUDIT_BUNDLE.md # Full 2M review audit bundle
├── v1/                         # RFC v1 patch series (Sep 22, 2026)
├── v2/                         # v2 patch series + cover letter (Sep 26, 2026)
└── v3/                         # v3 patch series (generated post-silicon verification)
```
