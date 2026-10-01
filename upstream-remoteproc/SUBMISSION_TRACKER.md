# Allwinner sun55i RemoteProc & Mailbox Upstream Submission Tracker

This dashboard tracks all upstream Linux kernel submissions, mailing list threads, Devicetree reviews, reviewer feedback, and verification statuses for the Allwinner sun55i (A523, A527, T527) and sun60i (A733) RemoteProc and Message Box drivers.

---

## 1. Upstream Patch Series Status

| Version | Submission Date | Thread URL / Message ID | Status & Key Changes |
| :---: | :---: | :--- | :--- |
| **RFC v1** | 2026-09-22 | [`20260922034711.190253-1-tcmichals@gmail.com`](https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/) | **Superseded**<br>Initial RFC submission (7 patches). Reviewed by Krzysztof Kozlowski, Rob Herring, and Chen-Yu Tsai. |
| **v2** | 2026-09-26 | [`20260927002021.797069-1-tcmichals@gmail.com`](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/) | **Reviewed**<br>Added 68 in-tree KUnit tests. Flagged for: `last_tx_done` pacing (`M2`), big-endian mock MMIO (`K2`), DT `reg-names` enum (`D1`), and unconstrained interrupts (`D2`). |
| **v3** | *Ready for Dispatch* | **Standalone Top-Level Thread**<br>*(Do NOT thread as in-reply-to v2 per Krzysztof Kozlowski)* | **Code & Schemas 100% Clean**<br>• All 18 Sashiko/maintainer driver & test issues resolved.<br>• DT bindings converted to strict positional lists.<br>• `make dt_binding_check` passed (0 errors, 0 warnings).<br>• `make linux-rebuild` passed on physical silicon target. |

---

## 2. Key Documentation & Reference Links

* **[Upstream Lessons Learned & Rules Ledger](UPSTREAM_LESSONS_LEARNED.md)**:  
  Detailed breakdown of why specific patterns were rejected by maintainers (Krzysztof Kozlowski, Rob Herring, Jassi Brar, Bjorn Andersson) and the exact code patterns required.
* **[Comprehensive Review Issue Tracker](reviews/REVIEW_TRACKER.md)**:  
  Item-by-item status matrix tracking issues M1–M4, T1–T3, R1–R10, K1–K3, and D1–D3.
* **[Sashiko Adversarial Audit Results](reviews/AUDIT_V3_RESULTS.md)**:  
  Clean 5-stage adversarial audit verification report (0 issues detected).

---

## 3. Directory Layout

```text
upstream-remoteproc/
├── README.md                   # Directory landing page
├── SUBMISSION_TRACKER.md       # Upstream version dashboard & Lore status (this file)
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

---

## 4. Upstream Submission Workflow (Submitting v3)

When ready to send v3 to `linux-sunxi@lists.linux.dev`, `linux-remoteproc@vger.kernel.org`, and `linux-mailbox`:

1. **Verify Silicon Smoke Test**: Ensure test suite runs cleanly on the physical Radxa Cubie A5E board.
2. **Generate Patches**:
   ```bash
   git format-patch -v3 --cover-letter -o v3/ origin/master..HEAD
   ```
3. **Draft Cover Letter**:
   * Summarize the 18 driver fixes and the 2 DT binding positional list fixes.
   * Include the Lore reference to v2 (`20260927002021.797069-1-tcmichals@gmail.com`).
4. **Dispatch as Independent Thread**:
   ```bash
   # DO NOT use --in-reply-to!
   git send-email --to="linux-sunxi@lists.linux.dev" \
                  --to="linux-remoteproc@vger.kernel.org" \
                  --cc="devicetree@vger.kernel.org" \
                  --cc="krzk+dt@kernel.org" \
                  --cc="robh@kernel.org" \
                  v3/*.patch
   ```
