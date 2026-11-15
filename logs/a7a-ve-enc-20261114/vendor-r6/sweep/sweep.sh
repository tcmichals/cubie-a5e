#!/bin/sh
# Vendor encoder register sweep (Radxa r6). One directory per run, snapshot per frame.
B=/root/sweep; mkdir -p $B; cd $B
python3 - <<'PY'
import os
def gen(w,h,n,name):
    y=bytearray([128])*(w*h); nz=os.urandom(256*256)
    for r in range(256): y[(200+r)*w+400:(200+r)*w+656]=nz[r*256:(r+1)*256]
    uv=bytearray([128])*(w*h//2)
    with open(name,"wb") as f:
        for _ in range(n): f.write(y); f.write(uv)
gen(1280,720,8,"in720.yuv"); gen(1920,1080,8,"in1080.yuv")
PY
run() { # name fmt size extra...
	n=$1; f=$2; sz=$3; shift 3
	mkdir -p $B/$n; cd $B/$n
	LD_PRELOAD=/root/vedump2.so demoVencoder -i $B/in$sz.yuv -n 8 -f $f -o out.bin -s $sz -d $sz "$@" > log.txt 2>&1
	echo "$n rc=$? $(ls regs_*.bin 2>/dev/null | wc -l) snaps $(stat -c %s out.bin 2>/dev/null) bytes"
	cd $B
}
for f in 0 3; do
	run f${f}_720_def $f 720
	run f${f}_1080_def $f 1080
	for q in 20 40; do
		run f${f}_720_qp$q $f 720 -qp_init $q -i_qp_min $q -i_qp_max $q -p_qp_min $q -p_qp_max $q
	done
	run f${f}_720_kp3 $f 720 -kp 3
	run f${f}_720_b500 $f 720 -b 500
done
rm -f in720.yuv in1080.yuv
tar czf /root/sweep.tgz -C /root sweep; ls -l /root/sweep.tgz; md5sum /root/sweep.tgz
