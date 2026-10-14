#!/usr/bin/env python3
"""
score_review.py: Benchmark and score review reports against the v3 REVIEW_ANSWER_KEY.
Usage:
    python3 scripts/score_review.py <path_to_review_output.md_or_txt>
"""

import sys
import os
import re

BENCHMARK_ITEMS = [
    {
        "id": "ISSUE-01",
        "title": "disable_irq() called under spinlock / sleeping under spinlock",
        "keywords": [r"disable_irq", r"spin_lock", r"might_sleep", r"synchronize_irq", r"atomic"],
        "required_citation": r"kernel/irq/manage\.c|synchronize_irq",
    },
    {
        "id": "ISSUE-02",
        "title": "Carveouts lost after first shutdown / missing from .prepare()",
        "keywords": [r"carveout", r"prepare", r"rproc_resource_cleanup", r"rproc_add_carveout"],
        "required_citation": r"remoteproc_core\.c|rproc_resource_cleanup|stm32_rproc",
    },
    {
        "id": "ISSUE-03",
        "title": "Inverted teardown: mailbox freed before rproc_del()",
        "keywords": [r"rproc_del", r"mbox_free_channel", r"teardown", r"kick.*free|free.*kick"],
        "required_citation": r"remoteproc_core\.c|rproc_del|rproc_shutdown",
    },
    {
        "id": "ISSUE-04",
        "title": "Requesting foreign / peer mailbox interrupts",
        "keywords": [r"platform_irq_count", r"index 0", r"regs\[0\]", r"foreign|spurious", r"IRQ_NONE"],
        "required_citation": r"platform_get_irq.*0|regs\[0\]",
    },
    {
        "id": "ISSUE-05",
        "title": "Linux claims the other processors' msgbox bank clocks/resets (D12)",
        "keywords": [r"CLK_BUS_MCU_DSP_MSGBOX|CLK_BUS_R_MSGBOX|CLK_BUS_MCU_RISCV_MSGBOX", r"remote bank|other processor|foreign bank", r"reset.*unbind|unbind.*reset"],
        "required_citation": r"reset_control_assert|devm_reset|dt-bindings/reset",
    },
    {
        "id": "ISSUE-06",
        "title": "Memory region collision (dram vs vram strstr overwrite)",
        "keywords": [r"strstr", r"dram.*vram|vram.*dram", r"memory-region-names", r"overwrite"],
        "required_citation": r"strcmp|memory-region-names",
    },
    {
        "id": "ISSUE-07",
        "title": "devm_memremap() error check (ERR_PTR vs NULL)",
        "keywords": [r"memremap", r"ERR_PTR", r"IS_ERR", r"NULL"],
        "required_citation": r"IS_ERR|ERR_PTR",
    },
    {
        "id": "ISSUE-08",
        "title": "Undocumented DT fallback lookups ('sram', 'sram-for-cpux')",
        "keywords": [r"sram-for-cpux", r"undocumented|fallback", r"remap.*0x364"],
        "required_citation": r"sram-for-cpux|SUNXI_REMAP_CTRL_OFFSET",
    },
    {
        "id": "ISSUE-09",
        "title": "A733 / sun60i dead code and unsupported tests",
        "keywords": [r"A733|sun60i", r"E902", r"a733_e902_cfg"],
        "required_citation": r"A733|sun60i|E902",
    },
    {
        "id": "ISSUE-10",
        "title": "Binding filename mismatch / unverified maintainers",
        "keywords": [r"sun55i-a523-rproc", r"maintainer", r"Samuel Holland|Jernej Skrabec"],
        "required_citation": r"allwinner,sun55i-a523-rproc\.yaml|MAINTAINERS",
    },
]

KNOWN_FALSE_POSITIVES = [
    (r"rproc_del.*must.*(?:last|after.*mbox_free)", "Demanding rproc_del() be called after freeing mailbox channels (inverted teardown bug)"),
    (r"spin_lock.*guard.*disable_irq|disable_irq.*under.*spin_lock.*correct", "Claiming disable_irq() should run under a spinlock"),
    (r"all.*4.*interrupts.*must.*be.*requested", "Claiming host should request peer coprocessor mailbox interrupts"),
]

def score_report(content):
    detected = []
    missing = []
    citations = 0

    for item in BENCHMARK_ITEMS:
        # Check if any keyword matches
        found = False
        for kw in item["keywords"]:
            if re.search(kw, content, re.IGNORECASE):
                found = True
                break
        if found:
            has_cite = bool(re.search(item["required_citation"], content, re.IGNORECASE))
            if has_cite:
                citations += 1
            detected.append((item["id"], item["title"], has_cite))
        else:
            missing.append((item["id"], item["title"]))

    false_positives = []
    for fp_re, fp_desc in KNOWN_FALSE_POSITIVES:
        if re.search(fp_re, content, re.IGNORECASE):
            false_positives.append(fp_desc)

    total_items = len(BENCHMARK_ITEMS)
    recall = (len(detected) / total_items) * 100.0

    print("=========================================================")
    print("           REVIEW BENCHMARK SCORING REPORT               ")
    print("=========================================================")
    print(f"Total Benchmark Issues: {total_items}")
    print(f"Issues Detected:        {len(detected)} / {total_items} ({recall:.1f}% Recall)")
    print(f"Citations Verified:     {citations} / {len(detected)}")
    print(f"Known False Positives:  {len(false_positives)}")
    print("---------------------------------------------------------")

    print("\n✅ DETECTED ISSUES:")
    for i_id, title, cite in detected:
        cite_str = " (with kernel citation)" if cite else " (missing core citation)"
        print(f"  [+] {i_id}: {title}{cite_str}")

    if missing:
        print("\n❌ MISSED ISSUES:")
        for i_id, title in missing:
            print(f"  [-] {i_id}: {title}")

    if false_positives:
        print("\n⚠️ FALSE POSITIVES / HARMFUL RECOMMENDATIONS DETECTED:")
        for fp in false_positives:
            print(f"  [!] {fp}")

    print("\n=========================================================")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/score_review.py <review_file>")
        sys.exit(1)

    path = sys.argv[1]
    if not os.path.exists(path):
        print(f"Error: file not found: {path}")
        sys.exit(1)

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        data = f.read()

    score_report(data)
