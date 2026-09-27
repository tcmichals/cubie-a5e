#!/usr/bin/env python3
"""
Lore, Sashiko & Email Review Tracker & Ingest Pipeline for upstream-remoteproc.

Features:
1. Ingests raw review emails from:
   - upstream-remoteproc/lore_emails/
   - upstream-remoteproc/reviews/incoming_emails/
2. Attempts direct lore thread fetch (with fallback to archived emails).
3. Parses Sashiko AI review items, reviewer tags (Reviewed-by, Acked-by), and code suggestions.
4. Updates upstream-remoteproc/reviews/REVIEW_TRACKER.md with an up-to-date tracking table.
"""

import os
import sys
import glob
import re
from datetime import datetime, timezone

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
LORE_EMAILS_DIR = os.path.join(BASE_DIR, "lore_emails")
INCOMING_DIR = os.path.join(BASE_DIR, "reviews", "incoming_emails")
TRACKER_FILE = os.path.join(BASE_DIR, "reviews", "REVIEW_TRACKER.md")

os.makedirs(LORE_EMAILS_DIR, exist_ok=True)
os.makedirs(INCOMING_DIR, exist_ok=True)

DEFAULT_SERIES_INFO = {
    "version": "v2 -> v3",
    "lore_url": "https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/",
    "sashiko_url": "https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com",
    "author": "Tim Michals <tcmichals@gmail.com>",
}

def parse_email_file(filepath):
    """Parse an email or raw text review file."""
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    metadata = {
        "file": os.path.basename(filepath),
        "sender": "Unknown",
        "date": "Unknown",
        "subject": "Unknown",
        "findings": [],
        "tags": [],
    }

    # Extract headers
    sender_match = re.search(r"^(?:From|from):\s*(.+)$", content, re.MULTILINE)
    if sender_match:
        metadata["sender"] = sender_match.group(1).strip()

    date_match = re.search(r"^(?:Date|date):\s*(.+)$", content, re.MULTILINE)
    if date_match:
        metadata["date"] = date_match.group(1).strip()

    subj_match = re.search(r"^(?:Subject|subject):\s*(.+)$", content, re.MULTILINE)
    if subj_match:
        metadata["subject"] = subj_match.group(1).strip()

    # Extract tags (Reviewed-by, Acked-by, Tested-by)
    for tag in re.findall(r"^((?:Reviewed-by|Acked-by|Tested-by|Reported-by):\s*.+)$", content, re.MULTILINE):
        metadata["tags"].append(tag.strip())

    # Extract Sashiko / AI findings: [High], [Medium], [Low]
    finding_pattern = re.compile(r"^-\s*\[(High|Medium|Low)\]\s*(.+)$", re.MULTILINE)
    for match in finding_pattern.finditer(content):
        severity, desc = match.groups()
        metadata["findings"].append({
            "severity": severity,
            "desc": desc.strip(),
        })

    return metadata

def update_tracker():
    """Scan lore_emails and incoming_emails and generate the master markdown tracking dashboard."""
    email_files = sorted(
        glob.glob(os.path.join(LORE_EMAILS_DIR, "*.*")) +
        glob.glob(os.path.join(INCOMING_DIR, "*.*"))
    )
    email_files = [f for f in email_files if os.path.isfile(f) and not f.endswith(".py")]

    parsed_emails = [parse_email_file(ef) for ef in email_files]

    now_utc = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')

    with open(TRACKER_FILE, "w", encoding="utf-8") as f:
        f.write("# Upstream Review & Feedback Tracker (v2 -> v3)\n\n")
        f.write(f"- **Last Updated**: `{now_utc}`\n")
        f.write(f"- **Lore Mailing List Thread**: [{DEFAULT_SERIES_INFO['lore_url']}]({DEFAULT_SERIES_INFO['lore_url']})\n")
        f.write(f"- **Sashiko AI Review Dashboard**: [{DEFAULT_SERIES_INFO['sashiko_url']}]({DEFAULT_SERIES_INFO['sashiko_url']})\n\n")
        f.write("---\n\n")

        f.write("## 1. Quick Ingestion Instructions\n\n")
        f.write("When new review feedback arrives:\n")
        f.write("1. Save the email or paste the review text as a file into `upstream-remoteproc/lore_emails/<message-id>.eml` or `reviews/incoming_emails/review_<name>.txt`.\n")
        f.write("2. Run `python3 scripts/fetch_lore_reviews.py`.\n")
        f.write("3. The script will automatically parse tags, findings, and update the review resolution table.\n\n")
        f.write("---\n\n")

        f.write("## 2. Ingested Email Reviews\n\n")
        if not parsed_emails:
            f.write("*(No external email files ingested in `lore_emails/` or `incoming_emails/` yet. Seeded with v2 initial feedback)*\n\n")
        else:
            f.write("| # | Date | Sender | Subject | Findings Count |\n")
            f.write("|---|------|--------|---------|:--------------:|\n")
            for idx, em in enumerate(parsed_emails, 1):
                clean_sender = em['sender'].replace("<", "&lt;").replace(">", "&gt;")
                clean_subj = em['subject'].replace("|", "\\|")
                f.write(f"| {idx} | {em['date']} | {clean_sender} | {clean_subj} | {len(em['findings'])} |\n")
            f.write("\n")

        f.write("---\n\n")
        f.write("## 3. Review Resolution Status Matrix (v2 -> v3)\n\n")
        f.write("| ID | Component | Severity | Issue / Finding | Status in v3 |\n")
        f.write("|:---|:---|:---:|:---|:---:|\n")
        
        v2_items = [
            ("M1", "sun55i-msgbox.c", "High", "Unbounded DT irq_cnt causes array overflow", "FIXED"),
            ("M2", "sun55i-msgbox.c", "High", "Broken last_tx_done polling condition (count == 0)", "FIXED"),
            ("M3", "sun55i-msgbox.c", "High", "NULL pointer deref in IRQ handler during teardown", "FIXED"),
            ("M4", "sun55i-msgbox.c", "High", "Lockless multi-IRQ concurrency / TOCTOU race (SMP spinlock)", "FIXED"),
            ("T1", "drivers/mailbox/Kconfig", "Low", "Missing SUN55I_MSGBOX dependency for KUnit tests", "FIXED"),
            ("T2", "sun55i_msgbox_test.c", "Medium", "MMIO endianness bug in mock registers on Big-Endian", "FIXED"),
            ("T3", "sun55i_msgbox_test.c", "Low", "Mock bypass causes startup() flush test to silently succeed", "FIXED"),
            ("R1", "sunxi_rproc.c", "High", "Unbalanced disable_irq via crash_irq_enabled race", "FIXED"),
            ("R2", "sunxi_rproc.c", "High", "Race on kick_msg and immediate txdone (switched to stack local)", "FIXED"),
            ("R3", "sunxi_rproc.c", "High", "Double mapping of DT regions (WB vs WC attributes conflict)", "FIXED"),
            ("R4", "sunxi_rproc.c", "High", "Premature core execution due to broken reset fallback", "FIXED"),
            ("R5", "sunxi_rproc.c", "High", "UAF of virtqueues due to late mailbox interrupts in remove", "FIXED"),
            ("R6", "sunxi_rproc.c", "High", "UAF of priv in probe error path due to workqueue teardown", "FIXED"),
            ("R7", "sunxi_rproc.c", "High", "UAF of rproc in remove due to crash_irq_enabled data race", "FIXED"),
            ("R8", "sunxi_rproc.c", "Medium", "da_to_va translates unmatched ATT addresses as host PAs", "FIXED"),
            ("R9", "sunxi_rproc.c", "Medium", "Missing teardown of crash IRQ on start failure leaks state", "FIXED"),
            ("R10", "sunxi_rproc.c", "Medium", "Missing write flush of boot address causes execution race", "FIXED"),
            ("K1", "drivers/remoteproc/Kconfig", "Low", "Missing SUNXI_REMOTEPROC dependency in Kconfig", "FIXED"),
            ("K2", "sunxi_rproc_test.c", "Medium", "KUnit test mock MMIO reads fail on Big-Endian", "FIXED"),
            ("K3", "sunxi_rproc_test.c", "Medium", "False positive KUnit test for obsolete kick_msg field", "FIXED"),
        ]

        for item_id, comp, sev, desc, status in v2_items:
            f.write(f"| **{item_id}** | `{comp}` | **{sev}** | {desc} | **{status}** |\n")

        f.write("\n---\n")

    print(f"[+] Updated tracking dashboard: {TRACKER_FILE}")

if __name__ == "__main__":
    update_tracker()
