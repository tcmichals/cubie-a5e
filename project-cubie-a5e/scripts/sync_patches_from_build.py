#!/usr/bin/env python3
"""Sync new-file sections of Buildroot kernel patches from the live build tree.

For each patch in PATCH_DIR, finds sections created from /dev/null, compares
the embedded content against the corresponding file in BUILD_TREE, and with
--write regenerates the hunk (content, @@ line count, diffstat left untouched).
"""
import re
import sys
from pathlib import Path

PATCH_DIR = Path("/home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/project-cubie-a5e/patches/linux")
BUILD_TREE = Path("/home/tcmichals/ssdData/projects/home/CubieA5E/bld.a7a/build/linux-7.1")

SECTION_RE = re.compile(
    r"(?P<head>--- /dev/null\n\+\+\+ b/(?P<path>\S+)\n)"
    r"(?P<hunk>@@ -0,0 \+1,(?P<count>\d+) @@\n)"
    r"(?P<body>(?:\+[^\n]*\n|\+\n)*)",
)

def embedded_content(body: str) -> str:
    return "".join(line[1:] + "\n" for line in body.splitlines())

def rebuild_body(text: str) -> str:
    return "".join("+" + line + "\n" for line in text.splitlines())

def main(write: bool) -> int:
    stale = 0
    for patch in sorted(PATCH_DIR.glob("*.patch")):
        original = patch.read_text()
        updated = original
        for m in SECTION_RE.finditer(original):
            rel = m.group("path")
            tree_file = BUILD_TREE / rel
            if not tree_file.exists():
                print(f"  MISSING in build tree: {rel} ({patch.name})")
                continue
            tree_text = tree_file.read_text()
            if embedded_content(m.group("body")) == tree_text:
                continue
            stale += 1
            print(f"  STALE: {rel} ({patch.name})")
            if write:
                nlines = len(tree_text.splitlines())
                new_section = (
                    m.group("head")
                    + f"@@ -0,0 +1,{nlines} @@\n"
                    + rebuild_body(tree_text)
                )
                updated = updated.replace(m.group(0), new_section)
        if write and updated != original:
            patch.write_text(updated)
            print(f"  WROTE: {patch.name}")
    print(f"{stale} stale section(s).")
    return 0

if __name__ == "__main__":
    sys.exit(main("--write" in sys.argv))
