# Allwinner sun55i RemoteProc & Mailbox Upstream Submission Workspace

This workspace consolidates all upstream kernel submissions, Devicetree binding reviews, revision diffs, automated review scrapers, and reviewer tracking for the Allwinner sun55i (A523, A527, T527) and sun60i (A733) RemoteProc and Message Box drivers.

---

## 1. Directory Structure

```text
upstream-remoteproc/
├── README.md               # Main series overview, lore URLs, and workflow documentation
├── scripts/                # Python & shell utilities for review downloading and verification
│   ├── fetch_lore_reviews.py   # Lore & Sashiko review email ingest and tracker generator
│   └── test_build.sh           # Native / cross-compile driver verification script
├── reviews/                # Prompts, tracking dashboards, and incoming email reviews
│   ├── GEMINI_PRO_REMOTEPROC_MSGBOX_AUDIT_BUNDLE.md  # Comprehensive deep-audit prompt
│   ├── REVIEW_TRACKER.md       # Up-to-date issue status & severity matrix across versions
│   └── incoming_emails/        # Raw incoming reviewer emails (.txt, .eml)
├── lore_emails/            # Downloaded / archived raw Lore thread messages (.eml / .mbox)
├── v1/                     # RFC v1 patch series (7 patches + cover letter, Sep 22 2026)
├── v2/                     # v2 patch series + changelogs (7 patches + cover letter, Sep 25 2026)
└── v3/                     # v3 patch series (incorporating 17 Sashiko AI + human review fixes)
```

---

## 2. Upstream Submission Threads on Lore

| Version | Submission Date | Message ID / Thread URL | Status |
| :---: | :---: | :--- | :---: |
| **RFC v1** | 2026-09-22 | [`20260922034711.190253-1-tcmichals@gmail.com`](https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/) | Superseded |
| **v2** | 2026-09-25 | [`20260927002021.797069-1-tcmichals@gmail.com`](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/) | Reviewed (17 issues found & fixed) |
| **v3** | *In Preparation* | Threaded under v1/v2 on `linux-remoteproc` & `linux-sunxi` | **Code Ready (100% checkpatch & build clean)** |

---

## 3. Workflow & Review Management

### Ingesting New Reviewer Emails
Whenever a review email arrives from `sashiko-bot` or maintainers (Chen-Yu Tsai, Rob Herring, Krzysztof Kozlowski, Bjorn Andersson, Mathieu Poirier):
1. Save the email text to `upstream-remoteproc/lore_emails/<message-id>.eml` or `reviews/incoming_emails/review_<name>.txt`.
2. Run:
   ```bash
   python3 scripts/fetch_lore_reviews.py
   ```
3. `reviews/REVIEW_TRACKER.md` will update automatically with parsed findings (`[High]`, `[Medium]`, `[Low]`), reviewer tags (`Reviewed-by:`, `Acked-by:`), and issue resolution status.
