#!/usr/bin/env python3
"""
watch_a733_upstream.py - Upstream Linux & U-Boot Patch Watcher for Allwinner A733 (sun60iw2)

This tool queries public mailing list archives (lore.kernel.org) and U-Boot Patchwork (patchwork.ozlabs.org)
for new patches, patch series revisions, and merge statuses related to Allwinner A733 (sun60i/sun60iw2).
"""

import sys
import json
import urllib.request
import urllib.parse
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

WATCH_TARGETS = [
    {
        "name": "Linux Kernel - linux-sunxi (lore.kernel.org)",
        "feed_url": "https://lore.kernel.org/linux-sunxi/?q=A733+OR+sun60i+OR+sun60iw2&x=A",
        "type": "lore_atom"
    },
    {
        "name": "Linux Kernel - linux-clk (lore.kernel.org)",
        "feed_url": "https://lore.kernel.org/linux-clk/?q=A733+OR+sun60i+OR+sun60iw2&x=A",
        "type": "lore_atom"
    },
    {
        "name": "U-Boot Patchwork (patchwork.ozlabs.org)",
        "api_url": "https://patchwork.ozlabs.org/api/1.3/patches/?project=uboot&q=A733",
        "type": "patchwork_api"
    }
]

KNOWN_PATCH_BASELINES = {
    "ccu": {
        "title": "clk: sunxi-ng: Add support for Allwinner A733 CCU and PRCM",
        "author": "Junhui Liu",
        "current_version": "v2",
        "status": "In Review (linux-clk / linux-sunxi)"
    },
    "pinctrl": {
        "title": "pinctrl: sunxi: a733: add initial support",
        "author": "Yixun Lan",
        "current_version": "v2",
        "status": "In Review (linux-sunxi / u-boot)"
    },
    "uboot_soc": {
        "title": "sunxi: Add support for A733 SoC",
        "author": "Yixun Lan",
        "current_version": "v2",
        "status": "In Review (U-Boot Patchwork)"
    }
}

def fetch_lore_atom(feed_url):
    """Fetch and parse Atom feed from lore.kernel.org."""
    try:
        req = urllib.request.Request(
            feed_url,
            headers={"User-Agent": "A733-Patch-Watcher/1.0 (Embedded Linux Flight Stack)"}
        )
        with urllib.request.urlopen(req, timeout=15) as response:
            xml_data = response.read()
            root = ET.fromstring(xml_data)
            
            ns = {"atom": "http://www.w3.org/2005/Atom"}
            entries = []
            for entry in root.findall("atom:entry", ns)[:5]:
                title = entry.find("atom:title", ns)
                author = entry.find("atom:author/atom:name", ns)
                updated = entry.find("atom:updated", ns)
                link = entry.find("atom:link", ns)
                
                entries.append({
                    "title": title.text.strip() if title is not None else "N/A",
                    "author": author.text.strip() if author is not None else "Unknown",
                    "updated": updated.text[:10] if updated is not None else "N/A",
                    "link": link.attrib.get("href", "") if link is not None else ""
                })
            return entries
    except Exception as e:
        return [{"title": f"Network / Feed Query Error: {e}", "author": "-", "updated": "-", "link": ""}]

def fetch_patchwork_api(api_url):
    """Fetch patch list from Patchwork REST API."""
    try:
        req = urllib.request.Request(
            api_url,
            headers={"User-Agent": "A733-Patch-Watcher/1.0"}
        )
        with urllib.request.urlopen(req, timeout=15) as response:
            data = json.loads(response.read().decode())
            entries = []
            for patch in data[:5]:
                entries.append({
                    "title": patch.get("name", "N/A"),
                    "author": patch.get("submitter", {}).get("name", "Unknown"),
                    "updated": patch.get("date", "")[:10],
                    "link": patch.get("web_url", "")
                })
            return entries
    except Exception as e:
        return [{"title": f"Patchwork API Error: {e}", "author": "-", "updated": "-", "link": ""}]

def print_banner():
    print("=" * 80)
    print("  🛰️  Radxa Cubie A7A (Allwinner A733 / sun60iw2) Upstream Patch Watcher")
    print("=" * 80)
    print(f"Timestamp: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%SZ')}\n")

def print_baselines():
    print("📌 Tracked Patch Series Baselines:")
    print("-" * 80)
    for key, info in KNOWN_PATCH_BASELINES.items():
        print(f"  • [{info['current_version']}] {info['title']}")
        print(f"    Author: {info['author']} | Status: {info['status']}")
    print("-" * 80 + "\n")

def main():
    print_banner()
    print_baselines()

    for target in WATCH_TARGETS:
        print(f"📡 Querying: {target['name']}...")
        if target["type"] == "lore_atom":
            results = fetch_lore_atom(target["feed_url"])
        else:
            results = fetch_patchwork_api(target["api_url"])

        for item in results:
            print(f"  [{item['updated']}] {item['title'][:70]}")
            if item.get('author') and item['author'] != '-':
                print(f"    Author: {item['author']} | Link: {item['link']}")
        print()

    print("=" * 80)
    print("💡 To schedule automatic notifications on upstream updates, use:")
    print("   /schedule CronExpression=\"0 9 * * *\" Prompt=\"Run tools/watch_a733_upstream.py\"")
    print("=" * 80)

if __name__ == "__main__":
    main()
