#!/bin/sh
# Run on Radxa Debian r6 (radxa-a733_bullseye_kde_r6, kernel 5.15.147-21-a733) as root.
# Encodes 10 frames of 1280x720 NV12 with the vendor demoVencoder and has libvenc print
# the programmed VE TOP, ISP (+0xA00) and AVC (+0xB00) registers (ve_printf_register),
# so they can be diffed against what cedrus programs. No devmem access is needed.
set -e
OUT=${1:-/root/venc-dump}
mkdir -p "$OUT"
cp /etc/cedarc.conf "$OUT/cedarc.conf.orig"
sed -i 's/^ve_printf_register = 0/ve_printf_register = 1/' /etc/cedarc.conf

# Same picture as the cedrus tests: mid-grey with a noise square, 10 identical frames.
python3 - "$OUT/in_1280x720_nv12.yuv" <<'EOF'
import os, sys
w, h = 1280, 720
y = bytearray([128]) * (w * h)
noise = os.urandom(256 * 256)
for r in range(256):
    y[(200 + r) * w + 400:(200 + r) * w + 656] = noise[r * 256:(r + 1) * 256]
uv = bytearray([128]) * (w * h // 2)
with open(sys.argv[1], "wb") as f:
    for _ in range(10):
        f.write(y); f.write(uv)
EOF

uname -a > "$OUT/uname.txt"
dmesg > "$OUT/dmesg.before"
demoVencoder -i "$OUT/in_1280x720_nv12.yuv" -n 10 -f 0 -o "$OUT/out.h264" \
	-s 1280x720 -d 1280x720 > "$OUT/venc.log" 2>&1 || echo "demoVencoder exit $?"
dmesg > "$OUT/dmesg.after"
cp "$OUT/cedarc.conf.orig" /etc/cedarc.conf

ls -l "$OUT"
od -A x -t x1z "$OUT/out.h264" | head -8
grep -c "reg[0-9a-f][0-9a-f]:" "$OUT/venc.log" || true
