#!/usr/bin/env python3
"""Build the GitHub wiki pages for the full sweep from test_sweep_history.json.

Writes wiki/Full-Sweep-Tests.md (generated tables and Mermaid charts plus the
hand-written tools/sweep_wiki_notes.md). run_full_sweep.py calls it after each
run; tools/push_wiki.sh publishes wiki/ to the GitHub wiki.
"""
import json
import os
import statistics

BASE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HISTORY = os.path.join(BASE, "test_sweep_history.json")
NOTES = os.path.join(BASE, "tools", "sweep_wiki_notes.md")
OUT = os.path.join(BASE, "wiki", "Full-Sweep-Tests.md")

# Metrics comparable from run 8 on (earlier ones used another benchmark method)
FIRST_COMPARABLE = 8
BASELINE_RUNS = 10

# (key, label, unit, digits)
METRICS = [
    ("p1_rpmsg_rtt_us", "rpmsg ping round trip, vrings in DRAM (Profile 1)", "µs", 1),
    ("p1_rpmsg_jitter_us", "rpmsg ping jitter, DRAM", "µs", 1),
    ("p1_rpmsg_rate_msgs_s", "rpmsg ping rate, DRAM", "msg/s", 0),
    ("p2_rpmsg_rtt_us", "rpmsg ping round trip, vrings in SRAM (Profile 2)", "µs", 1),
    ("p2_rpmsg_rate_msgs_s", "rpmsg ping rate, SRAM", "msg/s", 0),
    ("p1_stress_rate_msgs_s", "rpmsg stress, DRAM, 16 in flight", "msg/s", 0),
    ("p2_stress_rate_msgs_s", "rpmsg stress, SRAM, 16 in flight", "msg/s", 0),
    ("p1_dram_rtt_us", "ping, firmware in DRAM", "µs", 1),
    ("p1_dram_w16_rate_msgs_s", "ping rate, firmware in DRAM, 16 in flight", "msg/s", 0),
    ("p3_shm_rtt_us", "UIO direct to SRAM, polling (Profile 3)", "µs", 1),
    ("p3_shm_rate_msgs_s", "UIO direct, rate", "msg/s", 0),
    ("p3_shm_bandwidth_mb_s", "UIO direct, bandwidth", "MB/s", 1),
    ("p3_uio_irq_rtt_us", "UIO with the mailbox interrupt", "µs", 1),
    ("p3_uio_py_rtt_us", "UIO from Python", "µs", 1),
    ("mbx_timer_ping_rtt_us", "message box ping, TX done by poll", "µs", 1),
    ("mbx_timer_ping_p99_us", "message box ping p99, poll", "µs", 1),
    ("mbx_isr_ping_rtt_us", "message box ping, TX done by write interrupt", "µs", 1),
    ("mbx_isr_ping_p99_us", "message box ping p99, write interrupt", "µs", 1),
    ("mbx_timer_freertos_rtt_us", "FreeRTOS firmware ping, poll", "µs", 1),
    ("mbx_isr_freertos_rtt_us", "FreeRTOS firmware ping, write interrupt", "µs", 1),
]

FLAVOR_KEYS = ["p1_rpmsg_rtt_us", "p2_rpmsg_rtt_us", "p1_rpmsg_rate_msgs_s",
               "p2_stress_rate_msgs_s", "p3_uio_irq_rtt_us", "p3_uio_py_rtt_us",
               "mbx_isr_ping_p99_us"]

LABEL = {k: (label, unit, d) for k, label, unit, d in METRICS}


def fmt(v, digits):
    if v is None:
        return "–"
    return f"{v:,.{digits}f}"


def flavor(run):
    return run.get("kernel_flavor", "rt")


def value(run, key):
    v = run.get("metrics", {}).get(key)
    return v if isinstance(v, (int, float)) else None


def kunit(run):
    m = run.get("metrics", {})
    if m.get("kunit_total") is None:
        return "–"
    return f"{m.get('kunit_passed', 0)}/{m['kunit_total']}"


def chart(title, runs, key, unit):
    pts = [(r["run_id"], value(r, key)) for r in runs if value(r, key) is not None]
    if len(pts) < 2:
        return ""
    lo = min(v for _, v in pts)
    hi = max(v for _, v in pts)
    pad = max((hi - lo) * 0.2, hi * 0.02)
    xs = ", ".join(f'"#{i}"' for i, _ in pts)
    ys = ", ".join(f"{v:.1f}" for _, v in pts)
    return ("```mermaid\nxychart-beta\n"
            f'    title "{title}"\n'
            f"    x-axis [{xs}]\n"
            f'    y-axis "{unit}" {max(lo - pad, 0):.0f} --> {hi + pad:.0f}\n'
            f"    line [{ys}]\n```\n")


def main():
    with open(HISTORY) as f:
        history = json.load(f)
    history.sort(key=lambda r: r["run_id"])
    last = history[-1]
    passed = sum(r.get("overall_status") == "PASS" for r in history)
    flavors = sorted({flavor(r) for r in history})

    rt_pass = [r for r in history if flavor(r) == "rt" and r.get("overall_status") == "PASS"
               and r["run_id"] >= FIRST_COMPARABLE]
    baseline = rt_pass[-BASELINE_RUNS:]

    o = []
    o.append("# Full Sweep Tests\n")
    o.append("Every full sweep (`tools/run_full_sweep.py`) on the Radxa Cubie A5E, from the first "
             "run to now: the E906 remoteproc, message box, rpmsg and UIO paths, KUnit on the "
             "board, and the performance of each IPC path. Generated from "
             "[`test_sweep_history.json`](https://github.com/tcmichals/cubie-a5e/blob/main/"
             "test_sweep_history.json) by `tools/make_sweep_wiki.py`; do not edit by hand.\n")
    o.append(f"**{len(history)} runs** from {history[0]['timestamp'][:10]} to "
             f"{last['timestamp'][:10]}: {passed} PASS, {len(history) - passed} FAIL. "
             f"Kernel flavors: {', '.join(f'`{x}`' for x in flavors)}. "
             f"Latest: run #{last['run_id']} on `{last.get('git_kernel', '?')}` "
             f"({flavor(last)}), **{last.get('overall_status')}**, KUnit {kunit(last)}.\n")

    o.append(f"## Current performance (PREEMPT_RT)\n")
    o.append(f"Median and range over the last {len(baseline)} passing RT runs "
             f"(#{baseline[0]['run_id']}–#{baseline[-1]['run_id']}).\n")
    o.append("| Path | Median | Min | Max | Spread | Latest |")
    o.append("|---|---:|---:|---:|---:|---:|")
    for key, label, unit, d in METRICS:
        vals = [value(r, key) for r in baseline if value(r, key) is not None]
        if not vals:
            continue
        med = statistics.median(vals)
        spread = 100 * (max(vals) - min(vals)) / med if med else 0
        o.append(f"| {label} | {fmt(med, d)} {unit} | {fmt(min(vals), d)} | {fmt(max(vals), d)} "
                 f"| {spread:.0f}% | {fmt(value(last, key), d)} |")
    o.append("")

    o.append("## Kernel flavors\n")
    o.append("Latest passing run of each flavor.\n")
    latest = {}
    for r in history:
        if r.get("overall_status") == "PASS":
            latest[flavor(r)] = r
    cols = [f for f in ("rt", "preempt", "rt+lockdep", "preempt+lockdep") if f in latest]
    o.append("| Path | " + " | ".join(f"`{c}` (#{latest[c]['run_id']})" for c in cols) + " |")
    o.append("|---|" + "---:|" * len(cols))
    for key in FLAVOR_KEYS:
        label, unit, d = LABEL[key]
        o.append(f"| {label} | " + " | ".join(
            f"{fmt(value(latest[c], key), d)} {unit}" for c in cols) + " |")
    o.append("")

    o.append("## Trends (PREEMPT_RT, passing runs)\n")
    all_rt_pass = [r for r in history if flavor(r) == "rt" and r.get("overall_status") == "PASS"]
    o.append(chart("rpmsg round trip, DRAM vrings", all_rt_pass, "p1_rpmsg_rtt_us", "µs"))
    o.append("Runs before #8 used TX done by the 1 ms poll with the old FIFO pacing; "
             "the spikes are messages that waited a full poll period.\n")
    o.append(chart("rpmsg round trip, SRAM vrings", rt_pass, "p2_rpmsg_rtt_us", "µs"))
    o.append(chart("rpmsg stress, SRAM vrings", rt_pass, "p2_stress_rate_msgs_s", "msg/s"))
    o.append(chart("UIO with the mailbox interrupt", rt_pass, "p3_uio_irq_rtt_us", "µs"))

    with open(NOTES) as f:
        o.append(f.read().rstrip() + "\n")

    o.append("## Every run\n")
    o.append("| Run | Date | Kernel | Flavor | Result | KUnit | DRAM RTT | SRAM RTT | "
             "SRAM stress | UIO IRQ |")
    o.append("|---:|---|---|---|---|---:|---:|---:|---:|---:|")
    for r in reversed(history):
        status = r.get("overall_status")
        o.append(f"| #{r['run_id']} | {r['timestamp'][:16]} | `{r.get('git_kernel', '?')[:12]}` "
                 f"| {flavor(r)} | {'PASS' if status == 'PASS' else '**FAIL**'} | {kunit(r)} "
                 f"| {fmt(value(r, 'p1_rpmsg_rtt_us'), 1)} | {fmt(value(r, 'p2_rpmsg_rtt_us'), 1)} "
                 f"| {fmt(value(r, 'p2_stress_rate_msgs_s'), 0)} "
                 f"| {fmt(value(r, 'p3_uio_irq_rtt_us'), 1)} |")
    o.append("")
    o.append("RTT in µs, stress in msg/s. Full per-run data: "
             "[`test_sweep_history.md`](https://github.com/tcmichals/cubie-a5e/blob/main/"
             "test_sweep_history.md).\n")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(o))
    print(f"[WIKI] {OUT}")


if __name__ == "__main__":
    main()
