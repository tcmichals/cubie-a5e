#!/usr/bin/env python3
"""
make_perf_page.py - Build the IPC performance history page from the sweep history.

Reads test_sweep_history.json (written by run_full_sweep.py) and writes
docs/performance/index.html: one self-contained page (inline SVG, no external
scripts) that GitHub Pages serves from /docs.

    python3 tools/make_perf_page.py
"""

import html
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HISTORY = os.path.join(ROOT, "test_sweep_history.json")
OUT = os.path.join(ROOT, "docs", "performance", "index.html")

# One chart per metric: (key, title, what it measures, unit, decimals)
LATENCY = [
    ("p1_rpmsg_rtt_us", "Profile 1 · rpmsg over DDR", "ping_rpmsg, one ping at a time", "µs", 1),
    ("p2_rpmsg_rtt_us", "Profile 2 · rpmsg over SRAM", "ping_rpmsg, one ping at a time", "µs", 1),
    ("p1_dram_rtt_us", "Profile 1 · DRAM SPSC rings", "ping_dram, one message at a time", "µs", 1),
    ("p3_shm_rtt_us", "Profile 3 · SRAM slot, polling", "ping_shm", "µs", 2),
    ("p3_uio_irq_rtt_us", "Profile 3 · UIO doorbell interrupt", "ping_uio, sleeps on the IRQ", "µs", 1),
    ("p1_rpmsg_jitter_us", "Profile 1 · rpmsg jitter", "standard deviation of ping_rpmsg", "µs", 1),
]
THROUGHPUT = [
    ("p1_stress_rate_msgs_s", "Profile 1 · rpmsg stress, 16 in flight", "stress_rpmsg, 100,000 messages", "msg/s", 0),
    ("p2_stress_rate_msgs_s", "Profile 2 · rpmsg stress, 16 in flight", "stress_rpmsg, 100,000 messages", "msg/s", 0),
    ("p1_rpmsg_rate_msgs_s", "Profile 1 · rpmsg, one at a time", "ping_rpmsg", "msg/s", 0),
    ("p1_dram_w16_rate_msgs_s", "Profile 1 · DRAM rings, 16 in flight", "ping_dram -w 16", "msg/s", 0),
    ("p3_shm_rate_msgs_s", "Profile 3 · SRAM slot, polling", "ping_shm", "msg/s", 0),
]

# Changes that explain jumps in the charts: drawn before the first run at or after `at`
EVENTS = [
    {"at": "2026-10-14 00:00:00", "label": "KUnit trimmed 67 → 47"},
    {"at": "2026-10-15 04:50:00", "label": "Torn-index fix; full-payload checks"},
]

TEMPLATE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cubie A5E IPC Performance</title>
<meta name="description" content="Round-trip time and throughput of the Allwinner A527 E906 remoteproc, mailbox and UIO paths, per silicon sweep run.">
<style>
:root {
  color-scheme: light;
  --page: #f9f9f7; --surface: #fcfcfb; --ink: #0b0b0b; --ink-2: #52514e; --muted: #898781;
  --grid: #e1e0d9; --axis: #c3c2b7; --border: rgba(11,11,11,0.10);
  --series: #2a78d6; --series-wash: rgba(42,120,214,0.10);
  --good: #0ca30c; --critical: #d03b3b; --event: #898781;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    color-scheme: dark;
    --page: #0d0d0d; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --axis: #383835; --border: rgba(255,255,255,0.10);
    --series: #3987e5; --series-wash: rgba(57,135,229,0.12);
  }
}
:root[data-theme="dark"] {
  color-scheme: dark;
  --page: #0d0d0d; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7; --muted: #898781;
  --grid: #2c2c2a; --axis: #383835; --border: rgba(255,255,255,0.10);
  --series: #3987e5; --series-wash: rgba(57,135,229,0.12);
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--page); color: var(--ink);
  font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 1120px; margin: 0 auto; padding: 32px 16px 64px; }
h1 { font-size: 28px; margin: 0 0 4px; font-weight: 650; }
h2 { font-size: 18px; margin: 40px 0 12px; font-weight: 600; }
.sub { color: var(--ink-2); margin: 0; }
.sub code { font-size: 13px; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(170px, 1fr)); gap: 12px; margin-top: 24px; }
.tile, .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; }
.tile { padding: 14px 16px; }
.tile .label { color: var(--ink-2); font-size: 13px; }
.tile .value { font-size: 26px; font-weight: 600; margin-top: 2px; }
.tile .note { color: var(--muted); font-size: 12px; }
.runs { display: flex; flex-wrap: wrap; gap: 6px; }
.run { display: inline-flex; align-items: center; gap: 6px; padding: 3px 10px 3px 8px; border-radius: 999px;
  background: var(--surface); border: 1px solid var(--border); font-size: 13px; color: var(--ink-2); }
.run .dot { width: 14px; height: 14px; border-radius: 50%; display: inline-grid; place-items: center;
  color: #fff; font-size: 10px; font-weight: 700; }
.run.pass .dot { background: var(--good); } .run.fail .dot { background: var(--critical); }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(330px, 1fr)); gap: 12px; }
.card { padding: 14px 16px 8px; position: relative; }
.card h3 { margin: 0; font-size: 14px; font-weight: 600; }
.card .what { margin: 0 0 6px; color: var(--muted); font-size: 12px; }
svg.chart { display: block; width: 100%; height: 190px; outline: none; touch-action: none; overflow: visible; }
svg.chart:focus-visible { outline: 2px solid var(--series); outline-offset: 2px; border-radius: 6px; }
.tip { position: absolute; pointer-events: none; background: var(--surface); color: var(--ink);
  border: 1px solid var(--border); border-radius: 8px; padding: 6px 10px; font-size: 12px;
  box-shadow: 0 4px 16px rgba(0,0,0,0.12); white-space: nowrap; display: none; z-index: 2; }
.tip strong { font-size: 15px; display: block; }
.tip .key { display: inline-block; width: 12px; height: 2px; background: var(--series); vertical-align: middle; margin-right: 6px; }
.tip .meta { color: var(--ink-2); }
.events { color: var(--ink-2); font-size: 13px; margin: 8px 0 0; padding-left: 18px; }
.tablewrap { overflow-x: auto; background: var(--surface); border: 1px solid var(--border); border-radius: 12px; }
table { border-collapse: collapse; width: 100%; font-size: 13px; }
th, td { padding: 6px 10px; text-align: right; border-bottom: 1px solid var(--grid); white-space: nowrap;
  font-variant-numeric: tabular-nums; }
th { color: var(--ink-2); font-weight: 600; position: sticky; top: 0; background: var(--surface); }
th:first-child, td:first-child, th:nth-child(2), td:nth-child(2) { text-align: left; }
footer { color: var(--muted); font-size: 12px; margin-top: 32px; }
</style>
</head>
<body>
<main>
  <h1>Cubie A5E IPC performance</h1>
  <p class="sub">Radxa Cubie A5E (Allwinner A527): Linux ↔ XuanTie E906 over remoteproc/rpmsg, DRAM rings and UIO. One point per silicon sweep run.</p>
  <p class="sub" id="latest"></p>
  <div class="tiles" id="tiles"></div>

  <h2>Runs</h2>
  <div class="runs" id="runs"></div>
  <ol class="events" id="events"></ol>

  <h2>Round-trip time <span class="sub" style="font-size:13px">(lower is better)</span></h2>
  <div class="grid" id="latency"></div>

  <h2>Throughput <span class="sub" style="font-size:13px">(higher is better)</span></h2>
  <div class="grid" id="throughput"></div>

  <h2>All runs</h2>
  <div class="tablewrap"><table id="table"></table></div>

  <footer>Generated by <code>tools/make_perf_page.py</code> from <code>test_sweep_history.json</code>.
  Profile 3 polling throughput dropped on 2026-10-15 because every ping now writes and verifies the full
  484-byte payload in uncached SRAM; its round-trip time did not change.</footer>
</main>
<script>
const DATA = __DATA__;
const runs = DATA.runs, events = DATA.events;

function el(tag, attrs, text) {
  const e = document.createElementNS(tag === "svg" || DATA.svgTags.includes(tag) ? "http://www.w3.org/2000/svg" : "http://www.w3.org/1999/xhtml", tag);
  for (const k in (attrs || {})) e.setAttribute(k, attrs[k]);
  if (text !== undefined) e.textContent = text;
  return e;
}
function fmt(v, d) {
  if (v === null || v === undefined) return "–";
  return v.toLocaleString("en-US", { minimumFractionDigits: d, maximumFractionDigits: d });
}
function compact(v) {
  if (Math.abs(v) >= 1000) return (v / 1000).toLocaleString("en-US", { maximumFractionDigits: 1 }) + "K";
  return v.toLocaleString("en-US", { maximumFractionDigits: 1 });
}
function niceMax(v) {
  if (v <= 0) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(v))), n = v / p;
  return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * p;
}

// Header, tiles, run strip
const last = runs[runs.length - 1];
document.getElementById("latest").textContent =
  `Latest: run #${last.run_id}, ${last.timestamp} · ${last.overall_status} · kernel ${last.git_kernel} · tools ${last.git_cubie}`;
const tiles = document.getElementById("tiles");
for (const [key, label, unit, d] of DATA.tiles) {
  const t = el("div", { class: "tile" });
  t.append(el("div", { class: "label" }, label));
  const v = last.metrics[key];
  t.append(el("div", { class: "value" }, v === null || v === undefined ? "–" : fmt(v, d) + (unit ? " " + unit : "")));
  t.append(el("div", { class: "note" }, `run #${last.run_id}`));
  tiles.append(t);
}
{
  const t = el("div", { class: "tile" });
  t.append(el("div", { class: "label" }, "KUnit"));
  t.append(el("div", { class: "value" }, `${last.metrics.kunit_passed}/${last.metrics.kunit_total}`));
  t.append(el("div", { class: "note" }, "in-kernel tests passed"));
  tiles.append(t);
}
const strip = document.getElementById("runs");
for (const r of runs) {
  const pass = r.overall_status === "PASS";
  const s = el("span", { class: "run " + (pass ? "pass" : "fail"), title: `${r.timestamp} · kernel ${r.git_kernel}` });
  s.append(el("span", { class: "dot", "aria-hidden": "true" }, pass ? "✓" : "✕"));
  s.append(document.createTextNode(`#${r.run_id} ${pass ? "pass" : "fail"}`));
  strip.append(s);
}
const evList = document.getElementById("events");
for (const e of events) {
  const r = runs.find(x => x.run_id === e.before_run);
  evList.append(el("li", {}, `${e.label} (from run #${e.before_run}${r ? ", " + r.timestamp.slice(0, 10) : ""})`));
}

// One small line chart per metric
function chart(parent, [key, title, what, unit, d]) {
  const pts = runs.map((r, i) => ({ i, r, v: r.metrics[key] ?? null }));
  if (!pts.some(p => p.v !== null)) return;
  const card = el("div", { class: "card" });
  card.append(el("h3", {}, title));
  card.append(el("p", { class: "what" }, `${what} · ${unit}`));
  const measured = pts.filter(p => p.v !== null);
  if (measured.length === 1) card.append(el("p", { class: "what" }, `First measured in run #${measured[0].r.run_id}.`));
  const svg = el("svg", { class: "chart", tabindex: "0", role: "img", "aria-label": `${title}: ${what}, ${unit}, by run` });
  card.append(svg);
  parent.append(card);
  const W = Math.max(280, Math.round(svg.getBoundingClientRect().width)), H = 190, L = 44, R = 12, T = 14, B = 26;
  svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  const ymax = niceMax(Math.max(...pts.filter(p => p.v !== null).map(p => p.v)) * 1.08);
  const x = i => L + (runs.length === 1 ? (W - L - R) / 2 : i * (W - L - R) / (runs.length - 1));
  const y = v => T + (H - T - B) * (1 - v / ymax);
  for (let k = 0; k <= 4; k++) {
    const v = ymax * k / 4, yy = y(v);
    svg.append(el("line", { x1: L, x2: W - R, y1: yy, y2: yy, stroke: k ? "var(--grid)" : "var(--axis)", "stroke-width": 1, "vector-effect": "non-scaling-stroke" }));
    svg.append(el("text", { x: L - 6, y: yy + 4, "text-anchor": "end", "font-size": 11, fill: "var(--muted)" }, compact(v)));
  }
  for (const p of pts) {
    if (runs.length > 12 && p.i % Math.ceil(runs.length / 12)) continue;
    svg.append(el("text", { x: x(p.i), y: H - 8, "text-anchor": "middle", "font-size": 11, fill: "var(--muted)" }, "#" + p.r.run_id));
  }
  for (const e of events) {
    const i = runs.findIndex(r => r.run_id === e.before_run);
    if (i <= 0) continue;
    const xx = (x(i) + x(i - 1)) / 2;
    svg.append(el("line", { x1: xx, x2: xx, y1: T, y2: H - B, stroke: "var(--event)", "stroke-width": 1, "stroke-dasharray": "3 3" }));
    svg.append(el("text", { x: xx, y: T - 3, "text-anchor": "middle", "font-size": 10, fill: "var(--muted)" }, String(events.indexOf(e) + 1)));
  }
  // Line segments between consecutive runs that both have a value; a gap means "not measured"
  let seg = [];
  const flush = () => {
    if (seg.length > 1) {
      const dpath = seg.map((p, j) => (j ? "L" : "M") + x(p.i) + " " + y(p.v)).join(" ");
      svg.append(el("path", { d: dpath + ` L${x(seg[seg.length - 1].i)} ${y(0)} L${x(seg[0].i)} ${y(0)} Z`, fill: "var(--series-wash)", stroke: "none" }));
      svg.append(el("path", { d: dpath, fill: "none", stroke: "var(--series)", "stroke-width": 2, "stroke-linejoin": "round", "stroke-linecap": "round", "vector-effect": "non-scaling-stroke" }));
    }
    seg = [];
  };
  for (const p of pts) { if (p.v === null) flush(); else seg.push(p); }
  flush();
  const dots = [];
  for (const p of pts) {
    if (p.v === null) continue;
    const c = el("circle", { cx: x(p.i), cy: y(p.v), r: 4, fill: "var(--series)", stroke: "var(--surface)", "stroke-width": 2, "vector-effect": "non-scaling-stroke" });
    svg.append(c); dots.push(c);
  }
  const cross = el("line", { y1: T, y2: H - B, stroke: "var(--ink-2)", "stroke-width": 1, "vector-effect": "non-scaling-stroke", visibility: "hidden" });
  svg.append(cross);
  const tip = el("div", { class: "tip" });
  card.append(tip);

  let cur = -1;
  function show(i) {
    cur = i;
    const p = pts[i], box = svg.getBoundingClientRect(), cbox = card.getBoundingClientRect();
    const px = x(i) * box.width / W;
    cross.setAttribute("x1", x(i)); cross.setAttribute("x2", x(i)); cross.setAttribute("visibility", "visible");
    tip.replaceChildren();
    const strong = el("strong", {}); strong.append(el("span", { class: "key" }), document.createTextNode(p.v === null ? "not measured" : fmt(p.v, d) + " " + unit));
    tip.append(strong, el("span", { class: "meta" }, `run #${p.r.run_id} · ${p.r.timestamp.slice(0, 16)} · ${p.r.overall_status}`));
    tip.append(el("br"), el("span", { class: "meta" }, `kernel ${p.r.git_kernel} · tools ${p.r.git_cubie}`));
    tip.style.display = "block";
    const left = box.left - cbox.left + px + 12, room = cbox.width - tip.offsetWidth - 8;
    tip.style.left = Math.max(8, left > room ? box.left - cbox.left + px - tip.offsetWidth - 12 : left) + "px";
    tip.style.top = (box.top - cbox.top + 8) + "px";
  }
  function hide() { cross.setAttribute("visibility", "hidden"); tip.style.display = "none"; cur = -1; }
  svg.addEventListener("pointermove", ev => {
    const box = svg.getBoundingClientRect(), vx = (ev.clientX - box.left) * W / box.width;
    let best = 0; for (const p of pts) if (Math.abs(x(p.i) - vx) < Math.abs(x(best) - vx)) best = p.i;
    show(best);
  });
  svg.addEventListener("pointerleave", hide);
  svg.addEventListener("focus", () => show(pts.length - 1));
  svg.addEventListener("blur", hide);
  svg.addEventListener("keydown", ev => {
    if (ev.key === "ArrowLeft") { show(Math.max(0, (cur < 0 ? pts.length : cur) - 1)); ev.preventDefault(); }
    if (ev.key === "ArrowRight") { show(Math.min(pts.length - 1, cur + 1)); ev.preventDefault(); }
  });
}
function drawAll() {
  for (const [id, list] of [["latency", DATA.latency], ["throughput", DATA.throughput]]) {
    const g = document.getElementById(id); g.replaceChildren();
    for (const m of list) chart(g, m);
  }
}
drawAll();
let lastW = window.innerWidth;
window.addEventListener("resize", () => { if (window.innerWidth !== lastW) { lastW = window.innerWidth; drawAll(); } });

// Table view: every value, no hover needed
const table = document.getElementById("table");
const cols = [...DATA.latency, ...DATA.throughput];
const head = el("tr");
for (const h of ["Run", "Date", "Status", "KUnit", ...cols.map(c => `${c[1]} (${c[3]})`)]) head.append(el("th", {}, h));
const thead = el("thead"); thead.append(head); table.append(thead);
const tb = el("tbody"); table.append(tb);
for (const r of [...runs].reverse()) {
  const tr = el("tr");
  tr.append(el("td", {}, "#" + r.run_id), el("td", {}, r.timestamp.slice(0, 16)), el("td", {}, r.overall_status),
            el("td", {}, `${r.metrics.kunit_passed}/${r.metrics.kunit_total}`));
  for (const c of cols) tr.append(el("td", {}, fmt(r.metrics[c[0]], c[4])));
  tb.append(tr);
}
</script>
</body>
</html>
"""


def main():
    with open(HISTORY) as f:
        history = json.load(f)
    if not history:
        sys.exit("no runs in history")

    events = []
    for e in EVENTS:
        first = next((r for r in history if r["timestamp"] >= e["at"]), None)
        if first:
            events.append({"label": e["label"], "before_run": first["run_id"]})

    runs = [{k: r.get(k) for k in ("run_id", "timestamp", "git_kernel", "git_cubie", "overall_status", "metrics")}
            for r in history]
    data = {
        "runs": runs,
        "events": events,
        "latency": LATENCY,
        "throughput": THROUGHPUT,
        "tiles": [
            ("p1_rpmsg_rtt_us", "Profile 1 rpmsg RTT", "µs", 0),
            ("p2_rpmsg_rtt_us", "Profile 2 rpmsg RTT", "µs", 0),
            ("p3_shm_rtt_us", "Profile 3 polling RTT", "µs", 1),
            ("p3_uio_irq_rtt_us", "Profile 3 IRQ RTT", "µs", 0),
        ],
        "svgTags": ["line", "text", "path", "circle", "g", "rect"],
    }
    # Keep the JSON safe inside <script>: no "</" sequences
    blob = json.dumps(data, ensure_ascii=False).replace("</", "<\\/")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write(TEMPLATE.replace("__DATA__", blob))
    print(f"[PERF] Wrote {os.path.relpath(OUT, ROOT)} ({len(runs)} runs)")


if __name__ == "__main__":
    main()
