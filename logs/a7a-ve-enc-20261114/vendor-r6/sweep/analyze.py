#!/usr/bin/env python3
"""Classify every vendor-programmed VE_ENC register from the sweep snapshots and compare
with the registers cedrus writes. Usage: analyze.py <sweep dir> <cedrus dir> > table.md"""
import struct, sys, re, glob, os
sw, ced = sys.argv[1], sys.argv[2]
def load(run):
    return [struct.unpack("<2048I", open(f, "rb").read()) for f in sorted(glob.glob(f"{sw}/{run}/regs_*.bin"))]
runs = {r: load(r) for r in sorted(os.listdir(sw)) if os.path.isdir(f"{sw}/{r}")}
# cedrus register names -> offsets
txt = open(f"{ced}/cedrus_regs.h").read()
base = {"VE_ENGINE_ENC_ISP_BASE": 0xa00, "VE_ENGINE_ENC_H264_BASE": 0xb00}
names = {}
for m in re.finditer(r"#define\s+(VE_\w+_REG)\s+\((VE_ENGINE_ENC_\w+_BASE)\s*\+\s*(0x[0-9a-fA-F]+)\)", txt):
    names.setdefault(base[m.group(2)] + int(m.group(3), 16), m.group(1))
names[0x800] = "VE_MODE_REG (top)"; names[0x804] = "VE_RESET_REG (top)"
src = "".join(open(f).read() for f in glob.glob(f"{ced}/cedrus_enc*.c") + [f"{ced}/cedrus.c"])
written = {o for o, n in names.items() if re.search(r"cedrus_write\([^;]*\b" + re.escape(n.split(" ")[0]) + r"\b", src)}
written |= {0x800, 0x804}
def isaddr(v): return 0x00f00000 <= v <= 0x00ffffff or 0xf0000000 <= v
for fmt, title in (("f0", "H.264"), ("f3", "H.265")):
    d = runs[f"{fmt}_720_def"]
    print(f"\n## {title}: vendor register image (720p default, frame 0 = IDR, frame 1 = P)\n")
    print("| Offset | cedrus name | cedrus writes | frame 0 | frame 1 | varies with | 1080p frame 0 |")
    print("| :--- | :--- | :--- | :--- | :--- | :--- | :--- |")
    for o in list(range(0x800, 0x900, 4)) + list(range(0xa00, 0xc00, 4)):
        i = o // 4
        vals = {r: [f[i] for f in fr] for r, fr in runs.items() if r.startswith(fmt)}
        if all(v in (0, 0xdeadbeef) for vv in vals.values() for v in vv): continue
        var = []
        if len(set(d[k][i] for k in range(len(d)))) > 1: var.append("frame")
        for key, lab in (("qp20", "QP"), ("qp40", "QP"), ("1080_def", "size"), ("kp3", "key period"), ("b500", "bitrate")):
            r = runs[f"{fmt}_720_{key}" if key != "1080_def" else f"{fmt}_1080_def"]
            if r[0][i] != d[0][i] and lab not in var: var.append(lab)
        if isaddr(d[0][i]) and o >= 0xa70: var = ["address"] + [v for v in var if v == "frame"]
        n = names.get(o, "")
        w = "yes" if o in written else ("no" if o >= 0xa00 or o in (0x800, 0x804) else "no")
        print(f"| `+0x{o:03x}` | {('`'+n+'`') if n else '—'} | {w} | `{d[0][i]:08x}` | `{d[1][i]:08x}` | {', '.join(var) or 'constant'} | `{runs[fmt+'_1080_def'][0][i]:08x}` |")
miss = sorted(o for o in written if o >= 0xa00 and all(f[o // 4] in (0, 0xdeadbeef) for f in runs["f0_720_def"]))
print("\n## Registers cedrus writes that the vendor leaves 0 (H.264)\n")
print(", ".join(f"`+0x{o:03x}` `{names[o]}`" for o in miss) or "none")
