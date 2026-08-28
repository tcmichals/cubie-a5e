#!/usr/bin/env python3
"""Refresh the sunxi_rproc new-file hunk from a canonical driver source.

This intentionally updates only the new-file payload and its diffstat. It does
not hand-edit unified-diff lines, which avoids missing +/- prefixes.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

MARKER = "--- /dev/null\n+++ b/drivers/remoteproc/sunxi_rproc.c\n"
SUMMARY_RE = re.compile(
    r"( drivers/remoteproc/sunxi_rproc\.c \| )\d+( \+[^\n]*\n)"
)
INSERTIONS_RE = re.compile(r"(3 files changed, )\d+( insertions\(\+\))")


def refresh(patch_path: Path, source_path: Path) -> None:
    patch = patch_path.read_text()
    source = source_path.read_text()
    if MARKER not in patch:
        raise SystemExit(f"missing sunxi_rproc new-file marker in {patch_path}")
    lines = source.splitlines()
    prefix = patch.split(MARKER, 1)[0]
    prefix, summary_count = SUMMARY_RE.subn(
        rf"\g<1>{len(lines)}\g<2>", prefix, count=1
    )
    if summary_count != 1:
        raise SystemExit(f"missing driver diffstat in {patch_path}")
    prefix, insertion_count = INSERTIONS_RE.subn(
        rf"\g<1>{len(lines) + 10}\g<2>", prefix, count=1
    )
    if insertion_count != 1:
        raise SystemExit(f"missing insertion summary in {patch_path}")
    refreshed = (
        prefix
        + MARKER
        + f"@@ -0,0 +1,{len(lines)} @@\n"
        + "".join(f"+{line}\n" for line in lines)
    )
    patch_path.write_text(refreshed)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    refresh(args.patch, args.source)


if __name__ == "__main__":
    main()
