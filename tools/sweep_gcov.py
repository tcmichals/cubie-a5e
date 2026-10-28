#!/usr/bin/env python3
"""
sweep_gcov.py - line coverage of the message box and remoteproc drivers on the board

Used by run_full_sweep.py. The sweep reboots the board between profiles, and a
reboot clears the kernel's gcov counters, so the counters are copied off the
board before every reboot (one snapshot per profile). At the end, gcov runs on
the host against the build's .gcno files and the snapshots are combined: a line
counts as covered if any profile executed it.

UML KUnit coverage (host, tools/../TODO.md TEST-COV) covers everything except
probe()/remove() and the real MMIO accessors; this run is what measures those.

Kernel build (out of tree, Buildroot's .config, as .agents/rules/testing-strict-pass.md says):

    scripts/config --file $O/.config -e GCOV_KERNEL -e DEBUG_FS
    make O=$O ARCH=arm64 CROSS_COMPILE=... LOCALVERSION= olddefconfig
    make O=$O ARCH=arm64 CROSS_COMPILE=... LOCALVERSION= \\
         GCOV_PROFILE_sun55i-msgbox.o=y GCOV_PROFILE_sunxi_rproc.o=y Image

Only those two objects are instrumented. Then run the sweep with
    --gcov-build-dir $O [--gcov-tool <cross gcov of the same gcc>]
(or GCOV_BUILD_DIR / GCOV_TOOL in .cubie.env).

Coverage from UML KUnit and from the board, combined line by line (same source):

    sweep_gcov.py uml --build-dir <kunit.py build dir> --out uml_coverage.json
    sweep_gcov.py merge test_sweep_results.json uml_coverage.json
"""

import argparse
import base64
import glob
import json
import os
import re
import shutil
import subprocess
import tempfile

DRIVERS = [
    "drivers/mailbox/sun55i-msgbox.c",
    "drivers/remoteproc/sunxi_rproc.c",
]

GCOV_LINE = re.compile(r'^\s*([^:]+):\s*(\d+):(.*)$')


def default_gcov_tool():
    """The cross gcov next to the Buildroot cross compiler, if there is one."""
    here = os.path.dirname(os.path.abspath(__file__))
    for pattern in ("../bld.a5e/host/bin/aarch64*-gcov",
                    "../../bld.a5e/host/bin/aarch64*-gcov"):
        found = sorted(glob.glob(os.path.join(here, pattern)))
        if found:
            return found[0]
    return shutil.which("aarch64-linux-gnu-gcov")


class BoardCoverage:
    def __init__(self, run_ssh, build_dir, gcov_tool):
        self.run_ssh = run_ssh
        self.build_dir = os.path.abspath(build_dir) if build_dir else None
        self.gcov_tool = gcov_tool or default_gcov_tool()
        self.snapshots = []		# [(label, {driver: gcda bytes})]
        self.board_paths = {}		# driver -> .gcda path in debugfs
        self.skip_reason = None
        if not self.build_dir or not os.path.isdir(self.build_dir):
            self.skip_reason = "no --gcov-build-dir (kernel build with GCOV_KERNEL)"
        elif not self.gcov_tool:
            self.skip_reason = "no cross gcov found (--gcov-tool)"

    def _find_on_board(self):
        self.run_ssh("mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug")
        for drv in DRIVERS:
            name = os.path.basename(drv)[:-2] + ".gcda"
            _, out, _ = self.run_ssh(f"find /sys/kernel/debug/gcov -name '{name}' 2>/dev/null", timeout=20)
            path = out.strip().splitlines()[0] if out.strip() else None
            if path:
                self.board_paths[drv] = path
        if len(self.board_paths) != len(DRIVERS):
            missing = [d for d in DRIVERS if d not in self.board_paths]
            self.skip_reason = ("kernel on the board is not instrumented for "
                                + ", ".join(missing) + " (see tools/sweep_gcov.py)")

    def snapshot(self, label):
        """Copy the current counters off the board (call before every reboot)."""
        if self.skip_reason:
            return
        if not self.board_paths:
            self._find_on_board()
            if self.skip_reason:
                print(f"  [COVERAGE] skipped: {self.skip_reason}")
                return
        # A reboot unmounts debugfs, and not every profile mounts it again
        self.run_ssh("mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug")
        data = {}
        for drv, path in self.board_paths.items():
            code, out, _ = self.run_ssh(f"base64 '{path}'", timeout=30)
            if code == 0 and out.strip():
                data[drv] = base64.b64decode(out)
        self.snapshots.append((label, data))
        print(f"  [COVERAGE] counters saved after '{label}' ({len(data)}/{len(DRIVERS)} drivers)")

    def _gcov_lines(self, drv, gcda, workdir):
        """Run gcov for one snapshot; returns {line: (count or None, source)}."""
        obj = os.path.join(self.build_dir, os.path.dirname(drv))
        stem = os.path.basename(drv)[:-2]
        gcno = os.path.join(obj, stem + ".gcno")
        if not os.path.isfile(gcno):
            raise FileNotFoundError(f"{gcno} missing (built without GCOV_PROFILE_{stem}.o=y?)")
        shutil.copy(gcno, workdir)
        with open(os.path.join(workdir, stem + ".gcda"), "wb") as f:
            f.write(gcda)
        subprocess.run([self.gcov_tool, "-o", workdir, os.path.basename(drv)],
                       cwd=workdir, capture_output=True, text=True, check=True)
        lines = {}
        with open(os.path.join(workdir, os.path.basename(drv) + ".gcov")) as f:
            for raw in f:
                m = GCOV_LINE.match(raw)
                if not m or int(m.group(2)) == 0:
                    continue
                cnt = m.group(1).strip().rstrip("*")
                if cnt == "-":
                    count = None			# not executable
                elif cnt in ("#####", "====="):
                    count = 0
                else:
                    count = int(cnt)
                lines[int(m.group(2))] = (count, m.group(3))
        return lines

    def report(self):
        """Combine the snapshots per driver. Returns a dict for the JSON report."""
        if self.skip_reason:
            return {"status": "SKIPPED", "reason": self.skip_reason}
        if not self.snapshots:
            return {"status": "SKIPPED", "reason": "no counters were collected"}
        result = {"status": "COLLECTED", "gcov_tool": self.gcov_tool, "drivers": {}}
        for drv in DRIVERS:
            union, per_label, source = {}, {}, {}
            for label, data in self.snapshots:
                if drv not in data:
                    continue
                with tempfile.TemporaryDirectory() as tmp:
                    try:
                        lines = self._gcov_lines(drv, data[drv], tmp)
                    except (OSError, subprocess.CalledProcessError) as e:
                        result["drivers"][drv] = {"error": str(e)}
                        break
                hit = 0
                for ln, (count, text) in lines.items():
                    if count is None:
                        continue
                    source[ln] = text
                    union[ln] = union.get(ln, 0) + count
                    hit += count > 0
                executable = sum(1 for c, _ in lines.values() if c is not None)
                per_label[label] = (hit, executable)
            if "error" in result["drivers"].get(drv, {}):
                continue
            executable = len(union)
            covered = sum(1 for c in union.values() if c > 0)
            result["drivers"][drv] = {
                "lines_covered": covered,
                "lines_executable": executable,
                "percent": round(100.0 * covered / executable, 2) if executable else 0.0,
                "per_profile": {k: {"covered": h, "executable": e} for k, (h, e) in per_label.items()},
                "uncovered": [{"line": ln, "source": source[ln].strip()}
                              for ln in sorted(union) if union[ln] == 0],
                "line_counts": {str(ln): c for ln, c in sorted(union.items())},
                "source": {str(ln): t for ln, t in source.items()},
            }
        return result


def print_report(cov, title="ON THE BOARD (gcov, all profiles combined)"):
    print("\n" + "=" * 76)
    print(f"        DRIVER LINE COVERAGE {title}")
    print("=" * 76)
    if cov.get("status") != "COLLECTED":
        print(f"  Skipped: {cov.get('reason')}")
        print("=" * 76)
        return
    for drv, d in cov["drivers"].items():
        if "error" in d:
            print(f"  {drv}: \033[91m{d['error']}\033[0m")
            continue
        print(f"  {drv}: {d['lines_covered']}/{d['lines_executable']} lines ({d['percent']}%)")
        for ln in d["uncovered"]:
            print(f"      {ln['line']:>5}: {ln['source']}")
    print("=" * 76)


def write_markdown(f, cov):
    f.write("\n## Driver Line Coverage on the Board (gcov)\n\n")
    if cov.get("status") != "COLLECTED":
        f.write(f"Skipped: {cov.get('reason')}\n\n")
        return
    f.write("Counters saved before every reboot and combined: a line is covered if any profile ran it. "
            "UML KUnit covers the rest of the drivers; this run measures probe(), remove() and the "
            "hardware accessors.\n\n")
    f.write("| Driver | Lines covered | Percent |\n| :--- | ---: | ---: |\n")
    for drv, d in cov["drivers"].items():
        if "error" in d:
            f.write(f"| `{drv}` | error: {d['error']} | |\n")
        else:
            f.write(f"| `{drv}` | {d['lines_covered']}/{d['lines_executable']} | {d['percent']}% |\n")
    for drv, d in cov["drivers"].items():
        if "error" in d or not d["uncovered"]:
            continue
        f.write(f"\n<details>\n<summary>{drv}: {len(d['uncovered'])} lines not reached</summary>\n\n")
        f.write("| Line | Source |\n| ---: | :--- |\n")
        for ln in d["uncovered"]:
            src = ln["source"].replace("|", "\\|")
            f.write(f"| {ln['line']} | `{src}` |\n")
        f.write("\n</details>\n")
    f.write("\n")


def _local_reader(build_dir):
    """run_ssh stand-in that serves .gcda files from a local build (UML KUnit)."""
    def run(cmd, timeout=30, report_timeout=True):
        for drv in DRIVERS:
            gcda = os.path.join(build_dir, drv[:-2] + ".gcda")
            name = os.path.basename(gcda)
            if cmd.startswith("find") and name in cmd and os.path.isfile(gcda):
                return 0, gcda + "\n", ""
            if cmd.startswith("base64") and gcda in cmd:
                with open(gcda, "rb") as f:
                    return 0, base64.b64encode(f.read()).decode(), ""
        return 0, "", ""
    return run


def merge(reports):
    """Combine coverage dicts line by line; a line is covered if any run covered it."""
    merged = {"status": "COLLECTED", "drivers": {}}
    for drv in DRIVERS:
        counts, source, runs = {}, {}, []
        for name, cov in reports:
            d = (cov or {}).get("drivers", {}).get(drv)
            if not d or "line_counts" not in d:
                continue
            runs.append(name)
            for ln, c in d["line_counts"].items():
                counts[int(ln)] = counts.get(int(ln), 0) + c
            source.update({int(k): v for k, v in d["source"].items()})
        if not counts:
            continue
        covered = sum(1 for c in counts.values() if c > 0)
        merged["drivers"][drv] = {
            "runs": runs,
            "lines_covered": covered,
            "lines_executable": len(counts),
            "percent": round(100.0 * covered / len(counts), 2),
            "uncovered": [{"line": ln, "source": source[ln].strip()}
                          for ln in sorted(counts) if counts[ln] == 0],
        }
    return merged


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    sub = parser.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("uml", help="coverage of a UML KUnit build (kunit.py --build_dir)")
    u.add_argument("--build-dir", required=True)
    u.add_argument("--gcov-tool", default="gcov")
    u.add_argument("--out", required=True)
    m = sub.add_parser("merge", help="combine coverage JSON files (sweep results or uml output)")
    m.add_argument("files", nargs="+")
    args = parser.parse_args()

    if args.cmd == "uml":
        cov = BoardCoverage(_local_reader(args.build_dir), args.build_dir, args.gcov_tool)
        cov.snapshot("uml-kunit")
        result = cov.report()
        with open(args.out, "w") as f:
            json.dump(result, f, indent=2)
        print_report(result, "UNDER UML KUNIT")
        return 0 if result.get("status") == "COLLECTED" else 1

    reports = []
    for path in args.files:
        with open(path) as f:
            data = json.load(f)
        reports.append((os.path.basename(path), data.get("driver_coverage", data)))
    merged = merge(reports)
    print_report(merged, "COMBINED: " + ", ".join(name for name, _ in reports))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
