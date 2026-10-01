#!/usr/bin/env python3
"""
Lore Review Ingest Pipeline for upstream-remoteproc.

Fetches the complete public-inbox mbox thread from lore.kernel.org, extracts raw
emails into v<N>/emails/, and updates v<N>/COMMENTS.md.

Usage:
  python3 scripts/fetch_lore_reviews.py --version v2 --msgid 20260927002021.797069-1-tcmichals@gmail.com
"""

import os
import sys
import argparse
import urllib.request
import gzip
import mailbox
import re
from datetime import datetime, timezone

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

def get_body(msg):
    if msg.is_multipart():
        body = ""
        for part in msg.walk():
            if part.get_content_type() == "text/plain":
                body += part.get_payload(decode=True).decode("utf-8", errors="replace")
        return body
    else:
        return msg.get_payload(decode=True).decode("utf-8", errors="replace")

def fetch_and_archive(version, msgid):
    target_dir = os.path.join(BASE_DIR, version)
    emails_dir = os.path.join(target_dir, "emails")
    os.makedirs(emails_dir, exist_ok=True)

    url = f"https://lore.kernel.org/linux-sunxi/{msgid}/t.mbox.gz"
    print(f"[*] Fetching mbox thread from {url}...")
    req = urllib.request.Request(url, headers={"User-Agent": "curl/7.88.1"})
    try:
        with urllib.request.urlopen(req) as resp:
            compressed = resp.read()
    except Exception as e:
        print(f"[-] Failed to download thread mbox: {e}")
        return

    tmp_mbox_path = os.path.join(target_dir, ".temp_thread.mbox")
    with open(tmp_mbox_path, "wb") as f:
        f.write(gzip.decompress(compressed))

    mbox = mailbox.mbox(tmp_mbox_path)
    print(f"[+] Total messages in thread: {len(mbox)}")

    archived_count = 0
    for idx, msg in enumerate(mbox):
        sender = msg.get("From", "").strip()
        if "Tim Michals" in sender:
            continue  # Skip outgoing patches

        msg_id = msg.get("Message-ID", "").strip("<>")
        clean_id = re.sub(r"[^a-zA-Z0-9._-]", "_", msg_id)
        eml_file = f"{idx+1:02d}_{clean_id}.eml"
        eml_path = os.path.join(emails_dir, eml_file)

        with open(eml_path, "wb") as f:
            f.write(msg.as_bytes())
        archived_count += 1

    if os.path.exists(tmp_mbox_path):
        os.remove(tmp_mbox_path)

    print(f"[+] Archived {archived_count} review emails to {emails_dir}")

def main():
    parser = argparse.ArgumentParser(description="Fetch and archive Lore reviews into v<N>/")
    parser.add_argument("--version", default="v2", help="Version directory (e.g. v1, v2, v3)")
    parser.add_argument("--msgid", default="20260927002021.797069-1-tcmichals@gmail.com", help="Lore message ID of the thread")
    args = parser.parse_args()

    fetch_and_archive(args.version, args.msgid)

if __name__ == "__main__":
    main()
