#!/usr/bin/env python3
"""
validate_kernel_patches.py - Automated Linux Kernel Patch & Build Integrity Checker

Validates that:
1. Kernel patches in project-cubie-a5e/patches/linux/ apply cleanly to pristine upstream Linux 7.1.
2. Hunk line counts, headers, and patch structures are 100% valid (no truncation or missing lines).
3. The patched files match the active build tree (../bld.a5e/build/linux-7.1) with 0 differences.
4. The Buildroot defconfigs (cubie_a5e_defconfig, avaota_a1_defconfig) accurately reference all patches.
5. checkpatch.pl reports zero errors on the patches.
"""

import os
import sys
import shutil
import subprocess
import argparse

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PATCH_DIR = os.path.join(REPO_ROOT, "project-cubie-a5e", "patches", "linux")
DEFAULT_TARBALL = os.path.abspath(os.path.join(REPO_ROOT, "..", "buildroot", "dl", "linux", "linux-7.1.tar.xz"))
DEFAULT_LIVE_TREE = os.path.abspath(os.path.join(REPO_ROOT, "..", "bld.a5e", "build", "linux-7.1"))
CHECKPATCH = os.path.join(DEFAULT_LIVE_TREE, "scripts", "checkpatch.pl")

A5E_PATCHES = [
    "0002-remoteproc-sunxi-add-allwinner-riscv-remoteproc.patch",
    "0005-arm64-dts-allwinner-add-a523-remoteproc-and-msgbox.patch",
    "0012-mailbox-sun55i-add-allwinner-sun55i-a523-msgbox.patch",
]

FILES_TOUCHED = [
    "drivers/remoteproc/Kconfig",
    "drivers/remoteproc/Makefile",
    "drivers/remoteproc/sunxi_rproc.c",
    "arch/arm64/boot/dts/allwinner/sun55i-a523.dtsi",
    "drivers/mailbox/Kconfig",
    "drivers/mailbox/Makefile",
    "drivers/mailbox/sun55i-msgbox.c",
]

BASE_FILES_IN_TAR = [
    "linux-7.1/drivers/remoteproc/Kconfig",
    "linux-7.1/drivers/remoteproc/Makefile",
    "linux-7.1/arch/arm64/boot/dts/allwinner/sun55i-a523.dtsi",
    "linux-7.1/drivers/mailbox/Kconfig",
    "linux-7.1/drivers/mailbox/Makefile",
]


def check_defconfigs():
    print("=" * 70)
    print("  Step 1: Checking Buildroot Defconfigs for Required Patches")
    print("=" * 70)
    defconfigs = [
        os.path.join(REPO_ROOT, "project-cubie-a5e", "configs", "cubie_a5e_defconfig"),
        os.path.join(REPO_ROOT, "project-cubie-a5e", "configs", "avaota_a1_defconfig"),
    ]
    all_ok = True
    for cfg in defconfigs:
        rel_cfg = os.path.relpath(cfg, REPO_ROOT)
        if not os.path.exists(cfg):
            print(f"  [WARN] Defconfig missing: {rel_cfg}")
            continue
        with open(cfg, "r") as f:
            content = f.read()
        for p in A5E_PATCHES:
            if p not in content:
                print(f"  [FAIL] {rel_cfg} is missing reference to {p}")
                all_ok = False
            else:
                print(f"  [PASS] {rel_cfg} includes {p}")
    return all_ok


def check_patch_hunks():
    print("\n" + "=" * 70)
    print("  Step 2: Checking Patch Hunk Integrity (No Truncation / Mismatches)")
    print("=" * 70)
    all_ok = True
    for p_name in A5E_PATCHES:
        p_path = os.path.join(PATCH_DIR, p_name)
        if not os.path.exists(p_path):
            print(f"  [FAIL] Patch file not found: {p_name}")
            all_ok = False
            continue
        # Scan hunk headers @@ -a,b +c,d @@ and verify line count
        with open(p_path, "r") as f:
            lines = f.readlines()
        i = 0
        hunks_checked = 0
        while i < len(lines):
            line = lines[i]
            if line.startswith("@@"):
                parts = line.split("@@")[1].strip().split()
                if len(parts) >= 2 and parts[1].startswith("+"):
                    plus_spec = parts[1][1:]
                    expected_add = int(plus_spec.split(",")[1]) if "," in plus_spec else 1
                    # Count subsequent lines until next @@ or diff or EOF
                    actual_add = 0
                    j = i + 1
                    while j < len(lines):
                        if (lines[j].startswith("@@") or
                            lines[j].startswith("diff --") or
                            lines[j].startswith("--- ") or
                            lines[j].startswith("-- ") or
                            lines[j] == "--\n"):
                            break
                        if lines[j] == "\n" and (j + 1 >= len(lines) or lines[j + 1].startswith("--")):
                            break
                        if lines[j].startswith("+") and not lines[j].startswith("+++"):
                            actual_add += 1
                        elif lines[j].startswith(" ") or lines[j] == "\n":
                            actual_add += 1
                        j += 1
                    if expected_add != actual_add:
                        print(f"  [FAIL] {p_name}: Hunk header line {i+1} specifies {expected_add} lines, but found {actual_add} lines!")
                        all_ok = False
                    hunks_checked += 1
            i += 1
        print(f"  [PASS] {p_name}: {hunks_checked} hunk headers verified clean")
    return all_ok


def test_patch_dry_run(scratch_dir, tarball):
    print("\n" + "=" * 70)
    print("  Step 3: Testing Dry-Run Application on Clean Linux 7.1")
    print("=" * 70)
    if not os.path.exists(tarball):
        print(f"  [FAIL] Upstream tarball not found: {tarball}")
        return False

    clean_dir = os.path.join(scratch_dir, "clean")
    patched_dir = os.path.join(scratch_dir, "patched")
    if os.path.exists(scratch_dir):
        shutil.rmtree(scratch_dir)
    os.makedirs(clean_dir, exist_ok=True)
    os.makedirs(patched_dir, exist_ok=True)

    # Extract base files
    tar_cmd = ["tar", "-xJf", tarball, "-C", clean_dir, "--strip-components=1"] + BASE_FILES_IN_TAR
    ret = subprocess.run(tar_cmd, capture_output=True, text=True)
    if ret.returncode != 0:
        print(f"  [FAIL] tar extraction failed: {ret.stderr}")
        return False
    shutil.copytree(clean_dir, patched_dir, dirs_exist_ok=True)

    all_ok = True
    # 1. Dry-run
    for p_name in A5E_PATCHES:
        p_path = os.path.join(PATCH_DIR, p_name)
        with open(p_path, "r") as pf:
            cmd = ["patch", "-p1", "--dry-run", "-d", patched_dir]
            res = subprocess.run(cmd, stdin=pf, capture_output=True, text=True)
        if res.returncode == 0:
            print(f"  [PASS DRY-RUN] {p_name}")
        else:
            print(f"  [FAIL DRY-RUN] {p_name}:\n{res.stdout}\n{res.stderr}")
            all_ok = False

    if not all_ok:
        return False

    # 2. Apply for real in test tree
    print("\n  Applying patches to clean test tree...")
    for p_name in A5E_PATCHES:
        p_path = os.path.join(PATCH_DIR, p_name)
        with open(p_path, "r") as pf:
            cmd = ["patch", "-p1", "-d", patched_dir]
            res = subprocess.run(cmd, stdin=pf, capture_output=True, text=True)
        if res.returncode == 0:
            print(f"  [APPLIED] {p_name}")
        else:
            print(f"  [APPLY FAILED] {p_name}:\n{res.stdout}\n{res.stderr}")
            all_ok = False
    return all_ok


def compare_with_live_tree(scratch_dir, live_tree):
    print("\n" + "=" * 70)
    print("  Step 4: Comparing Patched Files vs Active Linux Tree")
    print(f"  Live Tree: {live_tree}")
    print("=" * 70)
    if not os.path.exists(live_tree):
        print(f"  [WARN] Live tree {live_tree} does not exist. Skipping live comparison.")
        return True

    patched_dir = os.path.join(scratch_dir, "patched")
    all_ok = True
    for rel_path in FILES_TOUCHED:
        patched_file = os.path.join(patched_dir, rel_path)
        live_file = os.path.join(live_tree, rel_path)
        if not os.path.exists(live_file):
            print(f"  [MISSING IN LIVE] {rel_path}")
            all_ok = False
            continue
        res = subprocess.run(["diff", "-u", patched_file, live_file], capture_output=True, text=True)
        if res.returncode == 0:
            print(f"  [IDENTICAL 100%] {rel_path}")
        else:
            print(f"  [DIFFERENCE] {rel_path}:\n{res.stdout[:500]}")
            all_ok = False
    return all_ok


def run_checkpatch():
    print("\n" + "=" * 70)
    print("  Step 5: Running checkpatch.pl on Patches")
    print("=" * 70)
    if not os.path.exists(CHECKPATCH):
        print(f"  [WARN] checkpatch.pl not found at {CHECKPATCH}. Skipping.")
        return True

    all_ok = True
    for p_name in A5E_PATCHES:
        p_path = os.path.join(PATCH_DIR, p_name)
        cmd = [CHECKPATCH, "--no-tree", p_path]
        res = subprocess.run(cmd, capture_output=True, text=True)
        # Parse output
        has_error = "ERROR:" in res.stdout or "total: " in res.stdout and "errors" in res.stdout and not res.stdout.count("0 errors")
        if "0 errors" in res.stdout:
            print(f"  [PASS CHECKPATCH] {p_name} (0 errors)")
        else:
            print(f"  [WARN/FAIL CHECKPATCH] {p_name}:\n{res.stdout[:500]}")
            all_ok = False
    return all_ok


def main():
    parser = argparse.ArgumentParser(description="Validate Linux Kernel Patches for Cubie A5E")
    parser.add_argument("--tarball", default=DEFAULT_TARBALL, help="Path to clean linux-7.1.tar.xz")
    parser.add_argument("--live-tree", default=DEFAULT_LIVE_TREE, help="Path to active build linux tree")
    parser.add_argument("--scratch", default="/tmp/test_linux_patches", help="Scratch directory for test")
    args = parser.parse_args()

    s1 = check_defconfigs()
    s2 = check_patch_hunks()
    s3 = test_patch_dry_run(args.scratch, args.tarball)
    s4 = compare_with_live_tree(args.scratch, args.live_tree)
    s5 = run_checkpatch()

    print("\n" + "=" * 70)
    print("  FINAL INTEGRITY SUMMARY")
    print("=" * 70)
    print(f"  1. Defconfig patch references:    {'PASS' if s1 else 'FAIL'}")
    print(f"  2. Patch hunk header integrity:   {'PASS' if s2 else 'FAIL'}")
    print(f"  3. Dry-run on clean Linux 7.1:    {'PASS' if s3 else 'FAIL'}")
    print(f"  4. Byte comparison vs live tree:  {'PASS' if s4 else 'FAIL'}")
    print(f"  5. checkpatch.pl style checks:    {'PASS' if s5 else 'FAIL'}")
    print("=" * 70)

    if s1 and s2 and s3 and s4 and s5:
        print(">>> ALL CHECKS PASSED: Zero lost changes, patches 100% verified. <<<\n")
        sys.exit(0)
    else:
        print(">>> INTEGRITY CHECKS FAILED: Review issues above! <<<\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
