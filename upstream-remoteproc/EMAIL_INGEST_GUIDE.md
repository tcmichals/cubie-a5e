# Lore Review Ingest Pipeline & Comments.md Generation Guide

This guide documents the exact architecture, scripts, and workflows used to pull review emails from `lore.kernel.org` public-inbox archives, store them as raw `.eml` artifacts, and generate verified [COMMENTS.md](v2/COMMENTS.md) matrices.

---

## 1. Why Direct Web Scraping Fails vs. Why Public-Inbox Works

When attempting to review kernel mailing list feedback:
1. **Interactive Web Browsers & Headless Agents Fail**:
   - `lore.kernel.org` runs Cloudflare / Anubis proof-of-work (PoW) anti-bot challenges on interactive HTML URLs (such as `.../T/#t` thread views).
   - Headless browser agents and standard scrapers fail to execute the challenge JavaScript or get blocked by 403 Forbidden responses.
   - Local headless browser bridges (e.g. Chrome DevTools Protocol) can experience port/socket exhaustion or localhost resolution conflicts.
2. **The Public-Inbox Solution**:
   - `lore.kernel.org` runs [public-inbox](https://public-inbox.org/), which is natively designed for command-line tools like `b4`, `curl`, and `git-am`.
   - Every thread on any mailing list exposes a raw gzipped mbox archive endpoint:
     ```text
     https://lore.kernel.org/<list>/<message-id>/t.mbox.gz
     ```
   - Requesting this endpoint using a clean, standard HTTP client (e.g., Python `urllib` or `curl` with `User-Agent: curl/7.88.1`) bypasses HTML challenges completely and downloads the entire raw email thread in seconds.

---

## 2. Ingest Architecture & Directory Structure

```text
upstream-remoteproc/
├── scripts/
│   └── fetch_lore_reviews.py      # Automated pipeline script
├── v1/
│   ├── emails/                    # 11 raw .eml review emails
│   │   ├── 09_20260922035409...eml
│   │   ├── ...
│   │   └── 16_CAGb2v66_AaPn...eml (Chen-Yu Tsai)
│   └── COMMENTS.md                # Parsed review matrix & action items
├── v2/
│   ├── emails/                    # 8 raw .eml review emails
│   │   ├── 28_20260927002758...eml
│   │   ├── ...
│   │   ├── 34_20261001-sens...eml (Krzysztof Kozlowski - threading)
│   │   └── 35_20261001-armo...eml (Krzysztof Kozlowski - reg-names)
│   └── COMMENTS.md                # Parsed review matrix & action items
└── EMAIL_INGEST_GUIDE.md          # This document
```

---

## 3. How `fetch_lore_reviews.py` Works

The ingest script is located at [scripts/fetch_lore_reviews.py](scripts/fetch_lore_reviews.py). Its execution flow is:

1. **Target Identification**:
   - Takes `--version` (e.g., `v1`, `v2`, `v3`) and the Message-ID (`--msgid`) of the cover letter or first patch in the series.
2. **Download Thread Archive**:
   - Requests `https://lore.kernel.org/linux-sunxi/<msgid>/t.mbox.gz` using an HTTP `GET` with header `User-Agent: curl/7.88.1`.
3. **Stream Decompression**:
   - Uses Python's standard `gzip.decompress()` to expand the gzipped stream into a temporary standard Unix mbox format.
4. **Mbox Parsing & Filtering**:
   - Opens the mbox using Python's standard `mailbox.mbox` class.
   - Iterates through every message:
     - **Outgoing Submissions Skipped**: Checks `msg.get("From")`; if it matches the patch author (`Tim Michals`), it is skipped so only inbound reviewer feedback is captured.
     - **Inbound Reviews Saved**: Extracts the `Message-ID`, cleans special characters (`[^a-zA-Z0-9._-]`), and writes the raw message bytes to `v<N>/emails/<idx>_<clean_msgid>.eml`.
5. **Scratch Cleanup**:
   - Removes any temporary `.mbox` files, leaving only clean individual `.eml` files in `v<N>/emails/`.

---

## 4. How `COMMENTS.md` is Generated & Structured

For each patch series version, [COMMENTS.md](v2/COMMENTS.md) is constructed from the extracted `.eml` files according to strict review integrity rules:

### Integrity Rule 1: Verbatim Quote Preservation
Reviewer comments must **never be paraphrased, softened, or hallucinated**. Maintainer quotes (from human reviewers and bot reviewers) are transcribed verbatim into blockquotes (`> ...` or code fences).

### Integrity Rule 2: Message-ID & File Linkage
Every entry in `COMMENTS.md` must cite:
- **Author & Affiliation**: e.g., `Krzysztof Kozlowski <krzk@kernel.org>`
- **Date & Message-ID**: e.g., `<20261001-armored-wild-saiga-c5c3ae@quoll>`
- **Lore Permanent Link**: Direct URL to lore.kernel.org message
- **Raw Email File**: Link to the local `.eml` file under `v<N>/emails/`

### Integrity Rule 3: Separation of Human vs. Automated Reviews
- **Maintainer Reviews**: Placed first under Section 1 (e.g., Chen-Yu Tsai, Krzysztof Kozlowski, Rob Herring).
- **Automated AI/Bot Reviews**: Placed under Section 2 (e.g., Sashiko Bot, Kernel Test Robot).

### Integrity Rule 4: Action Items & Resolution Ledger
Every issue raised is tagged with a resolution status and a concrete explanation of the code, DeviceTree schema, or documentation change made in the next revision.

---

## 5. Verification Audit of Existing Ingests

Both [v1/COMMENTS.md](v1/COMMENTS.md) and [v2/COMMENTS.md](v2/COMMENTS.md) were audited against the raw `.eml` archives:

### RFC v1 Review Archive (11 Emails Captured)
- **Cover Thread**: `20260922034711.190253-1-tcmichals@gmail.com`
- **Captured Inbound Messages**:
  1. `09_...eml`: Sashiko bot on Patch 3/7 (KUnit mailbox test)
  2. `10_...eml`: Sashiko bot on Patch 6/7 (KUnit remoteproc test)
  3. `11_...eml`: Sashiko bot on Patch 1/7 (Mailbox DT binding)
  4. `12_...eml`: Sashiko bot on Patch 4/7 (RemoteProc DT binding)
  5. `13_...eml`: Sashiko bot on Patch 5/7 (RemoteProc driver)
  6. `14_...eml`: Sashiko bot on Patch 7/7 (DTS nodes)
  7. `15_...eml`: Sashiko bot on Patch 2/7 (Mailbox driver)
  8. `16_CAGb2v66...eml`: **Chen-Yu Tsai (`wens@kernel.org`)** (Full series distribution, compatible strings)
  9. `17_c13dd4d8...eml`: **Krzysztof Kozlowski (`krzk@kernel.org`)** (Mailbox DT binding schema)
  10. `18_14f2baec...eml`: **Krzysztof Kozlowski (`krzk@kernel.org`)** (RemoteProc DT binding schema)
  11. `19_17900811...eml`: **Rob Herring (`robh@kernel.org`)** (dt_binding_check build errors)
- **Status in v1/COMMENTS.md**: **100% complete and verified**. All 11 emails have dedicated, verbatim sections.

### v2 Review Archive (8 Emails Captured)
- **Cover Thread**: `20260927002021.797069-1-tcmichals@gmail.com`
- **Captured Inbound Messages**:
  1. `28_...eml`: Sashiko bot on Patch 4/7 (RemoteProc DT binding)
  2. `29_...eml`: Sashiko bot on Patch 1/7 (Mailbox DT binding)
  3. `30_...eml`: Sashiko bot on Patch 3/7 (KUnit mailbox test)
  4. `31_...eml`: Sashiko bot on Patch 6/7 (KUnit remoteproc test)
  5. `32_...eml`: Sashiko bot on Patch 2/7 (Mailbox driver)
  6. `33_...eml`: Sashiko bot on Patch 5/7 (RemoteProc driver)
  7. `34_20261001-sensible...eml`: **Krzysztof Kozlowski (`krzk@kernel.org`)** (Patch 1/7: Strict prohibition against threading new series into older threads)
  8. `35_20261001-armored...eml`: **Krzysztof Kozlowski (`krzk@kernel.org`)** (Patch 4/7: Critique of enum in `reg-names`)
- **Status in v2/COMMENTS.md**: **100% complete and verified**. All 8 emails have dedicated, verbatim sections and action items.

---

## 6. How to Run the Ingest for Future Versions (e.g. v3)

When reviews for v3 begin arriving on `linux-sunxi@lists.linux.dev` or `linux-remoteproc@vger.kernel.org`:

### Step 1: Find the Lore Message-ID
Locate the Message-ID of your v3 cover letter (e.g., from your sent email header or via `lore.kernel.org/linux-sunxi/`).

### Step 2: Run the Ingest Script
```bash
cd /home/tcmichals/projects/cubie/cubie-a5e/upstream-remoteproc
python3 scripts/fetch_lore_reviews.py --version v3 --msgid <v3-cover-letter-message-id>
```

### Step 3: Verify Captured Emails
```bash
ls -la v3/emails/
```

### Step 4: Parse Emails into `v3/COMMENTS.md`
Run the extraction script or create `v3/COMMENTS.md` following the template:
1. List maintainer feedback verbatim in Section 1.
2. List automated bot feedback in Section 2.
3. Record line numbers, commit IDs, and root causes for every comment.
4. Update `SUBMISSION_TRACKER.md` with links to the new review notes.
